from __future__ import annotations

import asyncio
from typing import Any

from solar_station.client import StationClient
from solar_station.errors import StationError


async def test_reader_eof_is_normalized_for_subscriptions() -> None:
    client = StationClient()
    reader = asyncio.StreamReader()
    queue: asyncio.Queue[dict[str, Any] | BaseException | None] = asyncio.Queue()
    client._reader = reader
    client._subscriptions["logs.console"] = {queue}
    reader.feed_eof()

    await client._read_loop()

    failure = await queue.get()
    assert isinstance(failure, StationError)
    assert failure.code == "server_unavailable"
    assert str(failure) == "Station server disconnected"
