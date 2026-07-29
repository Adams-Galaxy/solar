from __future__ import annotations

import asyncio
from dataclasses import dataclass
from pathlib import Path
from typing import Any
from uuid import uuid4

import pytest
from solar_remote import PingResult

from solar_station.config import StationConfig
from solar_station.server.runtime import StationHost


class FakeManifest:
    def __init__(self):
        self.image = b"fake-station-manifest"
        self.digest = b"\x42" * 32
        self.schemas = [
            {
                "id": 1,
                "name": "solar.Empty",
                "shape": "object",
                "codec": "cbor",
                "fields": [],
            },
            {
                "id": 100,
                "name": "fixture.Scalar",
                "shape": "object",
                "codec": "cbor",
                "fields": [
                    {
                        "id": 1,
                        "name": "value",
                        "kind": "float",
                        "width": 32,
                        "required": True,
                        "schema": 0,
                    }
                ],
            },
            {
                "id": 101,
                "name": "fixture.Euler",
                "shape": "object",
                "codec": "cbor",
                "fields": [
                    {
                        "id": 1,
                        "name": "roll",
                        "kind": "float",
                        "width": 32,
                        "required": True,
                        "schema": 0,
                    },
                    {
                        "id": 2,
                        "name": "pitch",
                        "kind": "float",
                        "width": 32,
                        "required": True,
                        "schema": 0,
                    },
                    {
                        "id": 3,
                        "name": "yaw",
                        "kind": "float",
                        "width": 32,
                        "required": True,
                        "schema": 0,
                    },
                ],
            },
        ]
        self.data = [
            {
                "id": 10,
                "name": "pid.kp",
                "schema": 100,
                "capabilities": 0,
            },
            {
                "id": 11,
                "name": "imu.euler",
                "schema": 101,
                "capabilities": 0,
            },
        ]
        self.actions = [
            {
                "id": 20,
                "name": "control.reset",
                "request_schema": 1,
                "response_schema": 1,
                "error_schema": 1,
            }
        ]
        self.topics: list[dict[str, Any]] = []
        self.streams: list[dict[str, Any]] = []
        self.links: list[dict[str, Any]] = []
        self.capabilities = [
            {"domain": "data", "endpoint": 10, "kind": "query"},
            {"domain": "data", "endpoint": 10, "kind": "update"},
            {"domain": "data", "endpoint": 11, "kind": "query"},
            {
                "domain": "data",
                "endpoint": 11,
                "kind": "out_stream",
                "maximum_rate_hz": 200,
                "maximum_batch": 8,
            },
        ]

    def to_dict(self) -> dict[str, Any]:
        return {
            "format": 2,
            "protocol": [1, 0],
            "manifest_sha256": self.digest.hex(),
            "schemas": self.schemas,
            "data": self.data,
            "actions": self.actions,
            "topics": self.topics,
            "streams": self.streams,
            "links": self.links,
            "capabilities": self.capabilities,
        }


class FakeSubscription:
    def __init__(self):
        self.queue: asyncio.Queue[Any] = asyncio.Queue()
        self.loss_count = 0
        self.closed = False

    def __aiter__(self) -> FakeSubscription:
        return self

    async def __anext__(self) -> Any:
        value = await self.queue.get()
        if value is StopAsyncIteration:
            raise StopAsyncIteration
        return value

    async def emit(self, value: Any) -> None:
        await self.queue.put(value)

    async def aclose(self) -> None:
        self.closed = True
        await self.queue.put(StopAsyncIteration)


@dataclass
class FakeInformation:
    build_id: int = 1234


class FakeSession:
    def __init__(self, target: str, manifest: FakeManifest | None = None):
        self.target = target
        self.manifest = manifest or FakeManifest()
        self.server_information = FakeInformation()
        self.values: dict[str, Any] = {"pid.kp": {"value": 1.0}}
        self.actions: list[tuple[str, Any]] = []
        self.subscriptions: dict[str, FakeSubscription] = {}
        self._closed = True
        self._reader: asyncio.Future[None] | None = None

    async def open(self) -> None:
        self._closed = False
        self._reader = asyncio.get_running_loop().create_future()

    async def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        if self._reader is not None and not self._reader.done():
            self._reader.set_result(None)
        for subscription in list(self.subscriptions.values()):
            await subscription.aclose()

    async def query(self, endpoint: str | int) -> Any:
        if endpoint == "imu.euler" or endpoint == 11:
            return {"roll": 0.1, "pitch": 0.2, "yaw": 0.3}
        key = "pid.kp" if endpoint == 10 else str(endpoint)
        if key not in self.values:
            raise KeyError(endpoint)
        return self.values[key]

    async def get(self, endpoint: str | int) -> Any:
        return await self.query(endpoint)

    async def update(self, endpoint: str | int, value: Any) -> bytes:
        key = "pid.kp" if endpoint == 10 else str(endpoint)
        if key != "pid.kp":
            raise KeyError(endpoint)
        self.values[key] = value
        return b""

    async def set(self, endpoint: str | int, value: Any) -> None:
        await self.update(endpoint, value)

    async def action(self, endpoint: str | int, value: Any = None) -> dict[str, Any]:
        if endpoint not in ("control.reset", 20):
            raise KeyError(endpoint)
        self.actions.append(("control.reset", value))
        return {}

    async def call(self, endpoint: str | int, value: Any = None) -> dict[str, Any]:
        return await self.action(endpoint, value)

    async def stream(
        self,
        endpoint: str | int,
        *,
        frequency: float | None = None,
        batch: int = 1,
    ) -> FakeSubscription:
        if endpoint not in ("imu.euler", 11):
            raise KeyError(endpoint)
        subscription = FakeSubscription()
        self.subscriptions["imu.euler"] = subscription
        return subscription

    async def watch(self, *args: Any, **kwargs: Any) -> FakeSubscription:
        return await self.stream(*args, **kwargs)

    async def topic(self, endpoint: str | int) -> FakeSubscription:
        return await self.stream(endpoint)

    async def ping(self) -> PingResult:
        return PingResult(
            nonce=42,
            round_trip_ns=2_000_000,
            remote_processing_ns=100_000,
            remote_receive_us=100,
            remote_send_us=200,
        )


class FakeSessionFactory:
    def __init__(self):
        self.sessions: list[FakeSession] = []
        self.manifests: asyncio.Queue[FakeManifest] = asyncio.Queue()

    def __call__(self, target: str, cache: Path | None, timeout: float) -> FakeSession:
        del cache, timeout
        manifest = (
            self.manifests.get_nowait()
            if not self.manifests.empty()
            else FakeManifest()
        )
        session = FakeSession(target, manifest)
        self.sessions.append(session)
        return session

    def use_next(self, manifest: FakeManifest) -> None:
        self.manifests.put_nowait(manifest)


async def wait_until(predicate: Any, deadline_seconds: float = 2.0) -> None:
    async with asyncio.timeout(deadline_seconds):
        while not predicate():  # noqa: ASYNC110
            await asyncio.sleep(0.005)


@pytest.fixture
async def running_server(tmp_path: Path):
    factory = FakeSessionFactory()
    # Darwin limits AF_UNIX paths to 103 bytes; pytest's nested temp paths exceed it.
    socket_path = Path("/tmp") / f"station-test-{uuid4().hex}.sock"
    config = StationConfig(
        socket_path=socket_path,
        database_path=tmp_path / "station.sqlite3",
        websocket_host=None,
        remote_target="fake://robot",
        console_target=None,
        reconnect_initial=0.01,
        reconnect_maximum=0.05,
    )
    server = StationHost(config, session_factory=factory)
    await server.start()
    await wait_until(lambda: server.connection.state.value == "online")
    try:
        yield server, factory
    finally:
        await server.close()
