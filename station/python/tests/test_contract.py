from __future__ import annotations

import asyncio
import json
import logging
from pathlib import Path
from types import SimpleNamespace
from uuid import uuid4

import pytest
from conftest import FakeManifest, FakeSessionFactory, wait_until

from solar_station.client import StationClient
from solar_station.config import StationConfig
from solar_station.errors import StationError
from solar_station.models import SourceKind
from solar_station.protocol import IPC_VERSION, read_message, write_message
from solar_station.server import discovery
from solar_station.server.runtime import StationHost


def _socket() -> Path:
    return Path("/tmp") / f"station-contract-{uuid4().hex}.sock"


def _config(
    tmp_path: Path,
    *,
    socket_path: Path | None = None,
    database_path: Path | None = None,
    console_target: str | None = None,
    queue_depth: int = 256,
) -> StationConfig:
    return StationConfig(
        socket_path=socket_path or _socket(),
        database_path=database_path or tmp_path / "station.sqlite3",
        remote_target="fake://robot",
        console_target=console_target,
        reconnect_initial=0.01,
        reconnect_maximum=0.05,
        client_event_queue=queue_depth,
    )


async def _start(
    config: StationConfig, factory: FakeSessionFactory | None = None
) -> tuple[StationHost, FakeSessionFactory]:
    factory = factory or FakeSessionFactory()
    server = StationHost(config, session_factory=factory)
    await server.start()
    await wait_until(lambda: server.connection.state.value == "online")
    return server, factory


async def _raw_client(
    server: StationHost, name: str
) -> tuple[asyncio.StreamReader, asyncio.StreamWriter]:
    reader, writer = await asyncio.open_unix_connection(server.config.socket_path)
    await write_message(
        writer, {"type": "hello", "version": IPC_VERSION, "client": name}
    )
    assert (await read_message(reader))["type"] == "hello_response"
    return reader, writer


@pytest.mark.asyncio
async def test_auto_discovery_prefers_usb_and_pairs_transports(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    usb_targets = [
        SimpleNamespace(target="serial:///dev/console", vid=0x16C0, pid=0x0483),
        SimpleNamespace(target="serial:///dev/remote", vid=0x16C0, pid=0x0483),
    ]
    monkeypatch.setattr(
        "solar_station.discovery.discover_usb",
        lambda **_: usb_targets,
    )
    assert (
        await discovery.discover_remote_target(vid=None, pid=None)
        == "serial:///dev/remote"
    )
    assert (
        await discovery.discover_console_target(
            "serial:///dev/remote", vid=None, pid=None
        )
        == "serial:///dev/console"
    )
    assert (
        await discovery.discover_console_target(None, vid=None, pid=None)
        == "serial:///dev/console"
    )

    monkeypatch.setattr("solar_station.discovery.discover_usb", lambda **_: [])

    async def reachable(_: str, **__: object) -> bool:
        return True

    monkeypatch.setattr(discovery, "tcp_target_reachable", reachable)
    assert (
        await discovery.discover_remote_target(vid=None, pid=None)
        == discovery.SIMULATOR_REMOTE_TARGET
    )
    assert (
        await discovery.discover_console_target(None, vid=None, pid=None)
        == "tcp://127.0.0.1:47001"
    )


@pytest.mark.asyncio
async def test_auto_discovery_uses_composite_interface_labels(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    usb_targets = [
        SimpleNamespace(
            target="serial:///dev/first",
            vid=0x16C0,
            pid=0x0483,
            interface="Solar Console",
        ),
        SimpleNamespace(
            target="serial:///dev/second",
            vid=0x16C0,
            pid=0x0483,
            interface="Solar Remote",
        ),
    ]
    monkeypatch.setattr(
        "solar_station.discovery.discover_usb",
        lambda **_: usb_targets,
    )
    assert (
        await discovery.discover_remote_target(vid=None, pid=None)
        == "serial:///dev/second"
    )
    assert (
        await discovery.discover_console_target(
            "serial:///dev/second", vid=None, pid=None
        )
        == "serial:///dev/first"
    )


@pytest.mark.asyncio
async def test_auto_discovery_selects_and_pairs_solar_bridge(
    monkeypatch: pytest.MonkeyPatch,
    tmp_path: Path,
) -> None:
    monkeypatch.setattr("solar_station.discovery.discover_usb", lambda **_: [])

    async def bridge_status(*_: object, **__: object) -> discovery.BridgeStatus:
        return discovery.BridgeStatus(
            host="robot-bridge.local",
            reachable=True,
            mode="active",
            device="connected",
            generation=7,
            serial="TEST-SERIAL",
            detail="paired",
        )

    monkeypatch.setattr(discovery, "probe_bridge", bridge_status)
    config = StationConfig(
        socket_path=_socket(),
        database_path=tmp_path / "bridge.sqlite3",
        bridge_host="robot-bridge.local",
    )
    resolver = discovery.TransportDiscovery(config)
    assert await resolver.resolve_remote() == "tcp://robot-bridge.local:47000"
    assert (
        await resolver.resolve_console("tcp://robot-bridge.local:47000")
        == "tcp://robot-bridge.local:47001"
    )
    snapshot = resolver.to_wire()
    assert snapshot["source"] == "bridge"
    assert snapshot["bridge"]["available"] is True
    assert snapshot["bridge"]["generation"] == 7
    assert snapshot["bridge"]["serial"] == "TEST-SERIAL"


@pytest.mark.asyncio
async def test_bridge_absence_is_preserved_when_no_transport_is_available(
    monkeypatch: pytest.MonkeyPatch,
    tmp_path: Path,
) -> None:
    monkeypatch.setattr("solar_station.discovery.discover_usb", lambda **_: [])

    async def bridge_status(*_: object, **__: object) -> discovery.BridgeStatus:
        return discovery.BridgeStatus(
            host="bridge.local",
            reachable=True,
            mode="active",
            device="absent",
            generation=4,
            detail="matching interfaces are absent",
        )

    async def unreachable(*_: object, **__: object) -> bool:
        return False

    monkeypatch.setattr(discovery, "probe_bridge", bridge_status)
    monkeypatch.setattr(discovery, "tcp_target_reachable", unreachable)
    resolver = discovery.TransportDiscovery(
        StationConfig(
            socket_path=_socket(),
            database_path=tmp_path / "absent.sqlite3",
        )
    )
    assert await resolver.resolve_remote() is None
    assert "reachable but its device is absent" in resolver.unavailable_detail()
    assert resolver.to_wire()["bridge"]["reachable"] is True


@pytest.mark.asyncio
async def test_bridge_status_probe_validates_protocol() -> None:
    async def serve(_: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        writer.write(
            json.dumps(
                {
                    "protocol": 1,
                    "mode": "active",
                    "device": "connected",
                    "generation": 3,
                    "serial": "SERIAL",
                    "detail": "ready",
                }
            ).encode()
            + b"\n"
        )
        await writer.drain()
        writer.close()
        await writer.wait_closed()

    server = await asyncio.start_server(serve, "127.0.0.1", 0)
    try:
        port = server.sockets[0].getsockname()[1]
        status = await discovery.probe_bridge("127.0.0.1", port=port, wait_seconds=1)
    finally:
        server.close()
        await server.wait_closed()
    assert status is not None
    assert status.available
    assert status.generation == 3


@pytest.mark.asyncio
async def test_reconnect_waits_for_outcome_and_times_out(
    running_server,
) -> None:
    server, _ = running_server

    class OfflineSession:
        manifest = None
        server_information = None
        _reader = None

        async def open(self) -> None:
            raise ConnectionError("fixture is offline")

        async def close(self) -> None:
            pass

    server.connection.session_factory = lambda *_: OfflineSession()
    async with StationClient(server.config.socket_path) as client:
        with pytest.raises(StationError) as failure:
            await client.request_with_timeout(
                "reconnect",
                request_timeout=0.2,
                timeout=0.05,
            )
    assert failure.value.code == "request_timeout"
    assert failure.value.details["last_error"] == "fixture is offline"


@pytest.mark.asyncio
async def test_server_emits_basic_lifecycle_logs(
    tmp_path: Path, caplog: pytest.LogCaptureFixture
) -> None:
    caplog.set_level(logging.INFO, logger="solar_station")
    server, _ = await _start(_config(tmp_path))
    await server.close()
    messages = [record.getMessage() for record in caplog.records]
    assert any(message.startswith("Starting (socket=") for message in messages)
    assert "Ready; accepting local clients" in messages
    assert "Connected to fake://robot (build=1234, manifest=424242424242)" in messages
    assert "Stopped cleanly" in messages


@pytest.mark.asyncio
async def test_database_and_socket_have_one_live_owner(tmp_path: Path) -> None:
    database = tmp_path / "station.sqlite3"
    first, _ = await _start(_config(tmp_path, database_path=database))
    try:
        second = StationHost(
            _config(tmp_path, database_path=database),
            session_factory=FakeSessionFactory(),
        )
        with pytest.raises(StationError) as failure:
            await second.start()
        assert failure.value.code == "server_already_running"
        assert first.config.socket_path.exists()
    finally:
        await first.close()

    replacement, _ = await _start(_config(tmp_path, database_path=database))
    await replacement.close()


@pytest.mark.asyncio
async def test_requests_are_private_and_malformed_clients_are_isolated(
    running_server,
) -> None:
    server, _ = running_server
    first_reader, first_writer = await _raw_client(server, "requester")
    second_reader, second_writer = await _raw_client(server, "observer")
    try:
        await write_message(
            first_writer,
            {
                "type": "request",
                "id": 7,
                "operation": "get",
                "arguments": {"endpoint": "pid.kp"},
            },
        )
        response = await asyncio.wait_for(read_message(first_reader), 1)
        assert response["id"] == 7
        assert response["result"] == {"value": 1.0}
        with pytest.raises(TimeoutError):
            await asyncio.wait_for(read_message(second_reader), 0.03)

        bad_reader, bad_writer = await _raw_client(server, "malformed")
        bad_writer.write((server.config.maximum_ipc_message + 1).to_bytes(4, "little"))
        await bad_writer.drain()
        error = await asyncio.wait_for(read_message(bad_reader), 1)
        assert error["type"] == "error"
        bad_writer.close()
        await bad_writer.wait_closed()

        malformed_reader, malformed_writer = await _raw_client(server, "bad-cbor")
        malformed_writer.write((1).to_bytes(4, "little") + b"\xff")
        await malformed_writer.drain()
        malformed_error = await asyncio.wait_for(read_message(malformed_reader), 1)
        assert malformed_error["type"] == "error"
        malformed_writer.close()
        await malformed_writer.wait_closed()

        async with StationClient(server.config.socket_path) as healthy:
            status = await healthy.request("status")
            assert status["robot"]["state"] == "online"
    finally:
        first_writer.close()
        second_writer.close()
        await first_writer.wait_closed()
        await second_writer.wait_closed()


@pytest.mark.asyncio
async def test_global_source_fanout_unsubscribe_and_slow_client_isolation(
    tmp_path: Path,
) -> None:
    server, factory = await _start(_config(tmp_path, queue_depth=1))
    slow = StationClient(server.config.socket_path, name="slow")
    fast = StationClient(server.config.socket_path, name="fast")
    try:
        await slow.connect()
        await fast.connect()
        await slow.request(
            "stream", endpoint="imu.euler", frequency=50, batch=1, stop=False
        )
        slow_subscription = await slow.subscribe("imu.euler")
        fast_subscription = await fast.subscribe("imu.euler")

        await (
            factory.sessions[-1]
            .subscriptions["imu.euler"]
            .emit({"roll": 0.0, "pitch": 0.0, "yaw": -1.0})
        )
        assert (await asyncio.wait_for(slow_subscription.__anext__(), 1))["value"][
            "yaw"
        ] == -1.0
        assert (await asyncio.wait_for(fast_subscription.__anext__(), 1))["value"][
            "yaw"
        ] == -1.0

        slow_server_connection = next(
            client for client in server.ipc.clients if client.name == "slow"
        )
        assert slow_server_connection._event_task is not None
        slow_server_connection._event_task.cancel()
        with pytest.raises(asyncio.CancelledError):
            await slow_server_connection._event_task

        for yaw in range(3):
            await server.events.publish(
                "imu.euler",
                SourceKind.DATA_STREAM,
                {"roll": 0.0, "pitch": 0.0, "yaw": float(yaw)},
            )
        received = await asyncio.wait_for(fast_subscription.__anext__(), 1)
        assert received["source"] == "imu.euler"
        await wait_until(lambda: slow_server_connection not in server.ipc.clients)
        assert server.sources.sources["imu.euler"].state.value == "active"
        assert len(factory.sessions[-1].subscriptions) == 1

        await fast_subscription.close()
        assert server.sources.sources["imu.euler"].state.value == "active"
        assert not factory.sessions[-1].subscriptions["imu.euler"].closed
        await slow_subscription.close()
    finally:
        await slow.close()
        await fast.close()
        await server.close()


@pytest.mark.asyncio
async def test_source_and_active_recording_survive_station_restart(
    tmp_path: Path,
) -> None:
    database = tmp_path / "station.sqlite3"
    first, _ = await _start(_config(tmp_path, database_path=database))
    async with StationClient(first.config.socket_path) as client:
        await client.request(
            "stream", endpoint="imu.euler", frequency=25, batch=2, stop=False
        )
        await client.request("record_start", name="across_restart")
    await first.close()

    second, factory = await _start(_config(tmp_path, database_path=database))
    try:
        await wait_until(
            lambda: second.sources.sources["imu.euler"].state.value == "active"
        )
        assert second.recorder.active_count == 1
        await (
            factory.sessions[-1]
            .subscriptions["imu.euler"]
            .emit({"roll": 1.0, "pitch": 2.0, "yaw": 3.0})
        )
        await wait_until(
            lambda: second.sources.sources["imu.euler"].last_live_sequence is not None
        )
        async with StationClient(second.config.socket_path) as client:
            stopped = await client.request("record_stop", name=None)
            assert stopped["name"] == "across_restart"
            shown = await client.request("record_show", name="across_restart")
            assert shown["events"] >= 1
    finally:
        await second.close()


@pytest.mark.asyncio
async def test_subscription_survives_remote_reconnect(running_server) -> None:
    server, factory = running_server
    async with StationClient(server.config.socket_path) as client:
        await client.request(
            "stream", endpoint="imu.euler", frequency=10, batch=1, stop=False
        )
        subscription = await client.subscribe("imu.euler")
        original_count = len(factory.sessions)
        await client.request("reconnect")
        await wait_until(lambda: len(factory.sessions) > original_count)
        await wait_until(
            lambda: "imu.euler" in factory.sessions[-1].subscriptions
            and server.connection.state.value == "online"
        )
        await (
            factory.sessions[-1]
            .subscriptions["imu.euler"]
            .emit({"roll": 4.0, "pitch": 5.0, "yaw": 6.0})
        )
        async with asyncio.timeout(1):
            async for event in subscription:
                if event.get("value", {}).get("yaw") == 6.0:
                    break
        await subscription.close()


class ExpandedManifest(FakeManifest):
    def __init__(self):
        super().__init__()
        self.image = b"expanded-station-manifest"
        self.digest = b"\x43" * 32
        self.data.append(
            {
                "id": 12,
                "name": "battery.voltage",
                "schema": 100,
                "capabilities": 0,
            }
        )
        self.capabilities.append({"domain": "data", "endpoint": 12, "kind": "query"})


class MissingSourceManifest(FakeManifest):
    def __init__(self):
        super().__init__()
        self.image = b"incompatible-station-manifest"
        self.digest = b"\x44" * 32
        self.data = [item for item in self.data if item["name"] != "imu.euler"]
        self.capabilities = [
            item for item in self.capabilities if item["endpoint"] != 11
        ]


def _manifest_data_names(client: StationClient) -> set[str]:
    return {str(item["name"]) for item in (client.manifest or {}).get("data", [])}


@pytest.mark.asyncio
async def test_manifest_expansion_refreshes_client_catalog_without_host_restart(
    running_server,
) -> None:
    server, factory = running_server
    original_server_id = server.events.server_id
    async with StationClient(server.config.socket_path) as client:
        await client.track_manifest()
        assert "battery.voltage" not in _manifest_data_names(client)
        factory.use_next(ExpandedManifest())
        await client.reconnect()
        await wait_until(lambda: "battery.voltage" in _manifest_data_names(client))
        assert server.events.server_id == original_server_id


@pytest.mark.asyncio
async def test_long_lived_client_recovers_manifest_after_host_restart(
    tmp_path: Path,
) -> None:
    config = _config(tmp_path)
    first, _ = await _start(config)
    client = StationClient(config.socket_path, name="long-lived")
    try:
        await client.track_manifest()
        assert "battery.voltage" not in _manifest_data_names(client)
        await first.close()

        replacement_factory = FakeSessionFactory()
        replacement_factory.use_next(ExpandedManifest())
        replacement, _ = await _start(config, replacement_factory)
        try:
            await wait_until(
                lambda: "battery.voltage" in _manifest_data_names(client),
                deadline_seconds=3,
            )
            assert (await client.request("status"))["robot"]["state"] == "online"
        finally:
            await replacement.close()
    finally:
        await client.close()


@pytest.mark.asyncio
async def test_missing_source_is_reported_without_stopping_server(
    running_server,
) -> None:
    server, factory = running_server
    async with StationClient(server.config.socket_path) as client:
        await client.request(
            "stream", endpoint="imu.euler", frequency=10, batch=1, stop=False
        )
        subscription = await client.subscribe("imu.euler")
        factory.use_next(MissingSourceManifest())
        await client.request("reconnect")
        await wait_until(
            lambda: server.connection.state.value == "online"
            and server.sources.sources["imu.euler"].state.value == "unavailable"
        )
        async with asyncio.timeout(1):
            async for event in subscription:
                if event.get("value", {}).get("state") == "unavailable":
                    break
        status = await client.request("status")
        assert status["robot"]["state"] == "online"
        assert status["sources"][0]["state"] == "unavailable"
        await subscription.close()


@pytest.mark.asyncio
async def test_console_tcp_is_persisted_while_remote_is_offline(
    tmp_path: Path,
) -> None:
    sent = asyncio.Event()

    async def console_peer(
        _reader: asyncio.StreamReader, writer: asyncio.StreamWriter
    ) -> None:
        writer.write(b"first line\r\nsecond")
        await writer.drain()
        writer.write(b" line\n")
        await writer.drain()
        sent.set()
        await asyncio.sleep(0.1)
        writer.close()
        await writer.wait_closed()

    console_server = await asyncio.start_server(console_peer, "127.0.0.1", 0)
    port = console_server.sockets[0].getsockname()[1]
    station, _ = await _start(
        _config(tmp_path, console_target=f"tcp://127.0.0.1:{port}")
    )
    try:
        await asyncio.wait_for(sent.wait(), 1)
        await station.connection.close()
        assert station.connection.state.value == "stopping"
        async with StationClient(station.config.socket_path) as client:
            await wait_until(
                lambda: station.console.connected
                or station.console.last_error is not None
            )
            logs = await client.request("logs", head=2, tail=None)
            assert [event["value"] for event in logs] == [
                "first line",
                "second line",
            ]
    finally:
        await station.close()
        console_server.close()
        await console_server.wait_closed()
        assert station.console._task is None
        assert not station.database.is_open


@pytest.mark.asyncio
async def test_console_tcp_probe_is_not_reported_as_connected(
    tmp_path: Path,
    caplog: pytest.LogCaptureFixture,
) -> None:
    attempts = 0

    async def closing_peer(
        _reader: asyncio.StreamReader, writer: asyncio.StreamWriter
    ) -> None:
        nonlocal attempts
        attempts += 1
        writer.close()
        await writer.wait_closed()

    console_server = await asyncio.start_server(closing_peer, "127.0.0.1", 0)
    port = console_server.sockets[0].getsockname()[1]
    caplog.set_level(logging.DEBUG, logger="solar_station.console")
    station, _ = await _start(
        _config(tmp_path, console_target=f"tcp://127.0.0.1:{port}")
    )
    try:
        await wait_until(lambda: attempts >= 3)
        messages = [
            record.getMessage()
            for record in caplog.records
            if record.name == "solar_station.console"
        ]
        assert not any(message.startswith("Console connected") for message in messages)
        assert messages.count("Console unavailable: console connection closed") == 1
        assert "Console still unavailable: console connection closed" in messages
        assert not station.console.connected
    finally:
        await station.close()
        console_server.close()
        await console_server.wait_closed()


@pytest.mark.asyncio
async def test_idle_console_tcp_is_a_live_connection(tmp_path: Path) -> None:
    release = asyncio.Event()

    async def idle_peer(
        _reader: asyncio.StreamReader, writer: asyncio.StreamWriter
    ) -> None:
        await release.wait()
        writer.close()
        await writer.wait_closed()

    console_server = await asyncio.start_server(idle_peer, "127.0.0.1", 0)
    port = console_server.sockets[0].getsockname()[1]
    station, _ = await _start(
        _config(tmp_path, console_target=f"tcp://127.0.0.1:{port}")
    )
    try:
        await wait_until(lambda: station.console.connected)
        await asyncio.sleep(0.3)
        assert station.console.connected
        assert station.console.last_error is None
    finally:
        release.set()
        await station.close()
        console_server.close()
        await console_server.wait_closed()


@pytest.mark.asyncio
async def test_export_preserves_event_manifest_identity_and_shutdown_cleanup(
    tmp_path: Path,
) -> None:
    server, factory = await _start(_config(tmp_path))
    socket_path = server.config.socket_path
    output = tmp_path / "identity.json"
    async with StationClient(socket_path) as client:
        await client.request(
            "stream", endpoint="imu.euler", frequency=10, batch=1, stop=False
        )
        await client.request("record_start", name="identity")
        await (
            factory.sessions[-1]
            .subscriptions["imu.euler"]
            .emit({"roll": 0.1, "pitch": 0.2, "yaw": 0.3})
        )
        await wait_until(
            lambda: server.sources.sources["imu.euler"].last_live_sequence is not None
        )
        await client.request("record_stop", name="identity")
        await client.request(
            "record_export",
            name="identity",
            format="json",
            output=str(output),
        )
    payload = json.loads(output.read_text())
    telemetry = next(
        event for event in payload["events"] if event["source"] == "imu.euler"
    )
    assert telemetry["manifest_digest"] == {"$bytes": (b"\x42" * 32).hex()}

    session = factory.sessions[-1]
    await server.close()
    assert session._closed
    assert not server.database.is_open
    assert not socket_path.exists()
