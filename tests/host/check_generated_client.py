#!/usr/bin/env python3
"""End-to-end generated application client tests over a fake SDK session."""

from __future__ import annotations

import argparse
import asyncio
from copy import deepcopy
import importlib
import json
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest

import solar_remote


def make_manifest(raw: dict, image: bytes) -> solar_remote.Manifest:
    manifest = solar_remote.Manifest(tuple(raw["protocol"]), image)
    for collection in (
        "schemas",
        "data",
        "actions",
        "topics",
        "streams",
        "links",
        "capabilities",
        "in_stream_groups",
    ):
        setattr(manifest, collection, deepcopy(raw.get(collection, [])))
    manifest.catalog = solar_remote.ManifestCatalog.from_raw(
        schemas=manifest.schemas,
        data=manifest.data,
        actions=manifest.actions,
        topics=manifest.topics,
        streams=manifest.streams,
        links=manifest.links,
        capabilities=manifest.capabilities,
        in_stream_groups=manifest.in_stream_groups,
    )
    return manifest


class FakeSubscription:
    def __init__(self, values):
        self.values = iter(values)
        self.closed = False

    def __aiter__(self):
        return self

    async def __anext__(self):
        try:
            return next(self.values)
        except StopIteration:
            raise StopAsyncIteration from None

    async def aclose(self):
        self.closed = True


class FakeInbound:
    def __init__(self, target):
        self.target = target
        self.sent = []
        self.closed = False

    async def __aenter__(self):
        return self

    async def __aexit__(self, *_):
        self.closed = True

    async def send(self, value):
        self.sent.append(value)


class FakeSession:
    def __init__(self, manifest, models, build_id):
        self.manifest = manifest
        self.models = models
        self.server_information = SimpleNamespace(build_id=build_id)
        self.gain = 1.0
        self.enabled = False
        self.calls = []
        self.inputs = []
        self.dynamic = object()

    async def get(self, endpoint):
        self.calls.append(("get", endpoint))
        descriptor = self.manifest.require_catalog().data.by_id(endpoint)
        if descriptor.name == "system.state":
            return self.models.RobotState(enabled=self.enabled)
        return self.models.DriveKpValue(self.gain)

    async def set(self, endpoint, value):
        self.calls.append(("set", endpoint, value))
        descriptor = self.manifest.require_catalog().data.by_id(endpoint)
        if descriptor.name == "system.state":
            self.enabled = value.enabled
            return
        self.gain = value.value

    async def call(self, endpoint, request):
        self.calls.append(("call", endpoint, request))
        return {"sequence": 42}

    async def stream(self, endpoint, **policy):
        self.calls.append(("stream", endpoint, policy))
        return FakeSubscription(
            [
                {"roll": 0.1, "pitch": 0.2, "yaw": 0.3},
                {"roll": 0.4, "pitch": 0.5, "yaw": 0.6},
            ]
        )

    def in_stream(self, endpoint, **policy):
        self.calls.append(("in_stream", endpoint, policy))
        value = FakeInbound(endpoint)
        self.inputs.append(value)
        return value

    def robot(self):
        return self.dynamic


class GeneratedClientTests(unittest.IsolatedAsyncioTestCase):
    @classmethod
    def setUpClass(cls):
        output = Path(OPTIONS.output).resolve()
        sys.path.insert(0, str(output / "python"))
        cls.package = importlib.import_module(OPTIONS.package)
        cls.models = cls.package.models
        cls.raw = json.loads((output / "manifest.json").read_text())
        image = output / "manifest.fixture.bin"
        if not image.exists():
            image = output / "manifest.bin"
        cls.image = image.read_bytes()

    def session(self, *, raw=None, image=None, build_id=None):
        return FakeSession(
            make_manifest(raw or self.raw, image or self.image),
            self.models,
            self.package.BUILD_ID if build_id is None else build_id,
        )

    async def test_native_parameter_action_and_dynamic_access(self):
        session = self.session()
        robot = await self.package.Robot.bind(session)
        self.assertIs(robot.dynamic, session.dynamic)
        self.assertEqual(await robot.parameters.drive.kp.get(), 1.0)
        await robot.parameters.drive.kp.set(1.25)
        dynamic_result, typed_result = await asyncio.gather(
            session.get(robot.parameters.drive.kp.id),
            robot.parameters.drive.kp.get(),
        )
        self.assertEqual(dynamic_result.value, 1.25)
        self.assertEqual(typed_result, 1.25)
        response = await robot.actions.system.ping()
        self.assertIsInstance(response, self.models.PingResponse)
        self.assertEqual(response.sequence, 42)
        state = await robot.data.system.state.get()
        self.assertFalse(state.enabled)
        await robot.data.system.state.set({"enabled": True})
        self.assertTrue((await robot.data.system.state.get()).enabled)

    async def test_output_and_input_streams(self):
        session = self.session()
        robot = await self.package.Robot.bind(session)
        catalog = session.manifest.require_catalog()
        self.assertIsNotNone(catalog.streams.get("imu.euler"))
        self.assertIsNone(catalog.data.get("imu.euler"))
        self.assertIsNotNone(catalog.streams.get("drive.command"))
        self.assertIsNone(catalog.data.get("drive.command"))
        received = []
        subscription = robot.streams.imu.euler.subscribe(frequency=20)
        async with subscription as samples:
            async for frame in samples:
                received.append(frame.value)
        self.assertEqual(len(received), 2)
        self.assertIsInstance(received[0], self.models.Euler)

        command = self.models.DriveCommand(
            throttle=0.5,
            differential=-0.1,
            mode=self.models.OperatingMode.MANUAL,
        )
        async with robot.streams.drive.command.open(frequency=50) as producer:
            await producer.send(command)
        self.assertEqual(session.inputs[0].sent, [command])
        self.assertTrue(session.inputs[0].closed)

    async def test_exact_and_compatible_binding(self):
        different_image = self.image + b"additive"
        with self.assertRaises(solar_remote.InterfaceMismatch) as caught:
            await self.package.Robot.bind(self.session(image=different_image))
        self.assertIn("interface digest", str(caught.exception))

        compatible = deepcopy(self.raw)
        compatible["data"].append(
            {
                "id": 0x7FFFFFF0,
                "name": "navigation.velocity",
                "description": "",
                "version": 1,
                "schema": compatible["data"][0]["schema"],
                "capability_mask": 1,
            }
        )
        session = self.session(raw=compatible, image=different_image)
        robot = await self.package.Robot.bind(
            session,
            interface_policy=solar_remote.InterfacePolicy.COMPATIBLE,
        )
        self.assertIs(robot.session, session)

    async def test_exact_build_is_separate_from_interface(self):
        session = self.session(build_id=self.package.BUILD_ID + 1)
        await self.package.Robot.bind(session)
        with self.assertRaises(solar_remote.InterfaceMismatch) as caught:
            await self.package.Robot.bind(
                session,
                build_policy=solar_remote.BuildPolicy.EXACT,
            )
        self.assertIn("build identity", str(caught.exception))

    async def test_generated_models_enforce_authored_bounds(self):
        with self.assertRaises(ValueError):
            self.models.DriveKpValue(10.5)
        with self.assertRaises(ValueError):
            self.models.PingResponse(sequence=-1)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    parser.add_argument("--package", required=True)
    OPTIONS, remaining = parser.parse_known_args()
    unittest.main(argv=[sys.argv[0], *remaining])
