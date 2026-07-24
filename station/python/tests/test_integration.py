from __future__ import annotations

import asyncio
from pathlib import Path

import pytest
from conftest import wait_until

from solar_station.client import StationClient
from solar_station.errors import StationError
from solar_station.models import SourceKind


@pytest.mark.asyncio
async def test_request_routing_selective_fanout_and_global_source(
    running_server,
) -> None:
    server, factory = running_server
    first = StationClient(server.config.socket_path, name="first")
    second = StationClient(server.config.socket_path, name="second")
    async with first, second:
        assert await first.request("get", endpoint="pid.kp") == {"value": 1.0}
        await first.request("set", endpoint="pid.kp", value={"value": 1.25})
        assert await second.request("get", endpoint="pid.kp") == {"value": 1.25}
        assert await first.request("call", endpoint="control.reset", value={}) == {}

        configured = await first.request(
            "stream",
            endpoint="imu.euler",
            frequency=100,
            batch=1,
            stop=False,
        )
        assert configured["state"] == "active"
        first_imu = await first.subscribe("imu.euler")
        second_logs = await second.subscribe("logs.console")
        try:
            await (
                factory.sessions[-1]
                .subscriptions["imu.euler"]
                .emit({"roll": 1.0, "pitch": 2.0, "yaw": 3.0})
            )
            imu_event = await asyncio.wait_for(first_imu.__anext__(), 1)
            assert imu_event["source"] == "imu.euler"
            assert imu_event["value"]["yaw"] == 3.0
            assert second_logs.queue.empty()

            await server.events.publish(
                "logs.console", SourceKind.LOG, "hello from robot"
            )
            log_event = await asyncio.wait_for(second_logs.__anext__(), 1)
            assert log_event["value"] == "hello from robot"
            assert first_imu.queue.empty()
        finally:
            await first_imu.close()
            await second_logs.close()

        streams = await second.request("streams")
        assert len(streams) == 1
        assert streams[0]["frequency"] == 100


@pytest.mark.asyncio
async def test_watch_rejects_noncontinuous_parameter(running_server) -> None:
    server, factory = running_server
    async with StationClient(server.config.socket_path) as client:
        with pytest.raises(StationError) as failure:
            await client.subscribe("pid.kp")
        assert failure.value.code == "unsupported_capability"

        passive_watch = await client.subscribe("imu.euler")
        assert server.sources.sources == {}
        assert factory.sessions[-1].subscriptions == {}
        await passive_watch.close()


@pytest.mark.asyncio
async def test_recording_persistence_export_and_reconnect_restore(
    running_server, tmp_path: Path
) -> None:
    server, factory = running_server
    async with StationClient(server.config.socket_path) as client:
        await client.request(
            "stream",
            endpoint="imu.euler",
            frequency=20,
            batch=1,
            stop=False,
        )
        await client.request("record_start", name="test_run")
        await (
            factory.sessions[-1]
            .subscriptions["imu.euler"]
            .emit({"roll": 0.4, "pitch": 0.5, "yaw": 0.6})
        )
        await wait_until(
            lambda: server.sources.sources["imu.euler"].last_live_sequence is not None
        )
        await server.events.publish("logs.console", SourceKind.LOG, "recorded log")
        stopped = await client.request("record_stop", name=None)
        assert stopped["name"] == "test_run"
        output = tmp_path / "test_run.jsonl"
        exported = await client.request(
            "record_export",
            name="test_run",
            format="jsonl",
            output=str(output),
        )
        assert exported["events"] == 2
        assert "recorded log" in output.read_text()

        original_count = len(factory.sessions)
        await client.request("reconnect")
        await wait_until(lambda: len(factory.sessions) > original_count)
        await wait_until(lambda: server.connection.state.value == "online")
        assert "imu.euler" in factory.sessions[-1].subscriptions
        assert server.sources.sources["imu.euler"].state.value == "active"


@pytest.mark.asyncio
async def test_logs_are_available_while_robot_is_reconnecting(running_server) -> None:
    server, _ = running_server
    await server.events.publish("logs.console", SourceKind.LOG, "persistent line")
    await server.connection.request_reconnect()
    async with StationClient(server.config.socket_path) as client:
        logs = await client.request("logs", head=None, tail=10)
        assert [event["value"] for event in logs][-1] == "persistent line"
