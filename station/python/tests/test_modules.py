from __future__ import annotations

from pathlib import Path
from typing import Any
from uuid import uuid4

from conftest import FakeSessionFactory, wait_until

from solar_station import (
    StationClient,
    StationConfig,
    StationModule,
    StationModuleContext,
)
from solar_station.server.runtime import StationHost


class FixtureModule(StationModule):
    name = "fixture.controls"
    version = "1.2.3"
    description = "fixture shared state"
    default_enabled = True

    def __init__(self) -> None:
        self.context: StationModuleContext | None = None
        self.value = 0

    async def start(self, context: StationModuleContext) -> None:
        self.context = context

    async def close(self) -> None:
        self.context = None

    async def request(self, method: str, arguments: dict[str, Any]) -> Any:
        assert method == "set"
        self.value = int(arguments["value"])
        assert self.context is not None
        await self.context.publish_event("changed", {"value": self.value})
        return {"value": self.value}

    def status(self) -> dict[str, Any]:
        return {"value": self.value}


async def test_station_modules_are_shared_and_publish_events(
    tmp_path: Path,
) -> None:
    config = StationConfig(
        socket_path=Path("/tmp") / f"station-module-{uuid4().hex}.sock",
        database_path=tmp_path / "station.sqlite3",
        websocket_host=None,
        remote_target="fake://robot",
        console_target=None,
        reconnect_initial=0.01,
        reconnect_maximum=0.05,
    )
    host = StationHost(
        config,
        session_factory=FakeSessionFactory(),
        module_factories={"fixture.controls": FixtureModule},
    )
    await host.start()
    await wait_until(lambda: host.connection.state.value == "online")
    try:
        first = StationClient(config.socket_path, name="first")
        second = StationClient(config.socket_path, name="second")
        await first.connect()
        await second.connect()
        try:
            modules = await first.list_modules()
            assert modules[0]["enabled"] is True
            assert modules[0]["status"] == {"value": 0}

            subscription = await second.subscribe("module.fixture.controls")
            async with subscription:
                result = await first.module_request("fixture.controls", "set", value=42)
                event = await subscription.__anext__()
            assert result == {"value": 42}
            assert event["value"]["event"] == "changed"
            assert event["value"]["state"] == {"value": 42}
        finally:
            await first.close()
            await second.close()
    finally:
        await host.close()
