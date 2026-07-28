from __future__ import annotations

import asyncio

import pytest

from solar_station import StationClient


@pytest.mark.asyncio
async def test_semantic_client_api_covers_remote_and_host_operations(
    running_server,
) -> None:
    host, factory = running_server
    async with StationClient(host.config.socket_path, name="public-api") as station:
        status = await station.status()
        assert status["robot"]["state"] == "online"
        assert status["discovery"]["source"] == "explicit"
        assert (await station.get("pid.kp"))["value"] == 1.0
        assert (await station.set("pid.kp", {"value": 2.5}))["updated"] is True
        assert (await station.get("pid.kp"))["value"] == 2.5
        assert await station.call("control.reset", {}) == {}
        assert factory.sessions[-1].actions == [("control.reset", {})]
        ping = await station.connection.ping()
        assert ping.round_trip_ms == 2.0
        assert ping.station_round_trip_ns is not None

        configured = await station.configure_stream("imu.euler", frequency=20)
        assert configured["state"] == "active"
        assert (await station.list_streams())[0]["endpoint_name"] == "imu.euler"

        subscription = await station.watch("imu.euler")
        async with subscription:
            await (
                factory.sessions[-1]
                .subscriptions["imu.euler"]
                .emit({"roll": 1.0, "pitch": 2.0, "yaw": 3.0})
            )
            event = await asyncio.wait_for(subscription.__anext__(), 1)
            assert event["value"]["yaw"] == 3.0

        recording = await station.start_recording("public-api")
        assert recording["active"] is True
        assert (await station.show_recording("public-api"))["name"] == "public-api"
        assert any(
            item["name"] == "public-api" for item in await station.list_recordings()
        )
        assert (await station.stop_recording("public-api"))["active"] is False


@pytest.mark.asyncio
async def test_typed_resources_return_models_and_frames(running_server) -> None:
    host, factory = running_server
    async with StationClient(host.config.socket_path, name="typed-api") as station:
        connection = await station.connection.status()
        assert connection.state.value == "online"
        assert connection.transport == "fake"

        gain = station.robot.data["pid.kp"]
        current = await gain.get()
        assert current.value == 1.0
        await gain.set(type(current)(value=3.5))
        assert (await gain.get()).value == 3.5

        source = station.sources["imu.euler"]
        configured = await source.configure(frequency=20)
        assert configured.frequency == 20
        async with await source.subscribe() as frames:
            await factory.sessions[-1].subscriptions["imu.euler"].emit(
                {"roll": 1.0, "pitch": 2.0, "yaw": 3.0}
            )
            frame = await asyncio.wait_for(frames.__anext__(), 1)
            assert frame.value.yaw == 3.0
            assert frame.endpoint.name == "imu.euler"
