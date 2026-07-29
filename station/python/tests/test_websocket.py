from __future__ import annotations

from pathlib import Path
from uuid import uuid4

import cbor2
import pytest
from conftest import FakeSessionFactory, wait_until
from websockets.asyncio.client import connect
from websockets.exceptions import ConnectionClosedError

from solar_station.client import StationClient
from solar_station.config import StationConfig
from solar_station.models import SourceKind
from solar_station.server.runtime import StationHost


def _config(tmp_path: Path) -> StationConfig:
    return StationConfig(
        socket_path=Path("/tmp") / f"station-ws-{uuid4().hex}.sock",
        database_path=tmp_path / "station.sqlite3",
        websocket_host="127.0.0.1",
        websocket_port=0,
        remote_target="fake://robot",
        console_target=None,
        reconnect_initial=0.01,
        reconnect_maximum=0.05,
    )


async def _start(tmp_path: Path) -> tuple[StationHost, str]:
    host = StationHost(_config(tmp_path), session_factory=FakeSessionFactory())
    await host.start()
    await wait_until(lambda: host.connection.state.value == "online")
    assert host.websocket._server is not None
    port = host.websocket._server.sockets[0].getsockname()[1]
    return host, f"ws://127.0.0.1:{port}/station"


async def _send(socket: object, value: dict[str, object]) -> None:
    await socket.send(cbor2.dumps(value, canonical=True))


async def _receive(socket: object) -> dict[str, object]:
    frame = await socket.recv()
    assert isinstance(frame, bytes)
    value = cbor2.loads(frame)
    assert isinstance(value, dict)
    return value


@pytest.mark.asyncio
async def test_websocket_reuses_station_contract_and_fanout(tmp_path: Path) -> None:
    host, url = await _start(tmp_path)
    try:
        async with connect(url) as socket:
            await _send(
                socket,
                {"type": "hello", "version": 1, "client": "browser-test"},
            )
            hello = await _receive(socket)
            assert hello["type"] == "hello_response"
            assert hello["version"] == 1

            await _send(
                socket,
                {
                    "type": "request",
                    "id": 1,
                    "operation": "status",
                    "arguments": {},
                },
            )
            status = await _receive(socket)
            assert status["type"] == "response"
            assert status["result"]["server"]["websocket_clients"] == 1

            async with StationClient(host.config.socket_path, name="unix-test"):
                await _send(
                    socket,
                    {"type": "subscribe", "id": 2, "source": "logs.console"},
                )
                assert (await _receive(socket))["type"] == "response"
                await host.events.publish(
                    "logs.console", SourceKind.LOG, "hello from WebSocket"
                )
                event = await _receive(socket)
                assert event["type"] == "event"
                assert event["event"]["value"] == "hello from WebSocket"
    finally:
        await host.close()


@pytest.mark.asyncio
async def test_websocket_rejects_wrong_path_text_and_version(tmp_path: Path) -> None:
    host, url = await _start(tmp_path)
    try:
        with pytest.raises(ConnectionClosedError):
            async with connect(url.replace("/station", "/wrong")) as socket:
                await socket.recv()

        async with connect(url) as socket:
            await socket.send("not CBOR")
            error = await _receive(socket)
            assert error["type"] == "error"

        async with connect(url) as socket:
            await _send(
                socket,
                {"type": "hello", "version": 999, "client": "bad-version"},
            )
            error = await _receive(socket)
            assert error["type"] == "error"
            assert error["error"]["code"] == "invalid_request"
    finally:
        await host.close()
