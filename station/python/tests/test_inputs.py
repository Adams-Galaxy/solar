from __future__ import annotations

from types import SimpleNamespace

import pytest

from solar_station.errors import StationError
from solar_station.server.inputs import InputRegistry


class _Stream:
    def __init__(self) -> None:
        self.target = 10
        self.token = 20
        self.effective = SimpleNamespace(minimum_interval_us=10_000)
        self.closure_reason = None
        self._closed = False
        self._closed_event = __import__("asyncio").Event()
        self.sent: list[object] = []
        self.session = SimpleNamespace(
            core=SimpleNamespace(credits={(10, 20): SimpleNamespace(window=2)})
        )

    @property
    def closed(self) -> bool:
        return self._closed

    @property
    def credit_window(self) -> int | None:
        grant = self.session.core.credits.get((self.target, self.token))
        return grant.window if grant else None

    async def open(self) -> None:
        pass

    async def send(self, value: object, *, timeout: float | None = None) -> None:
        self.sent.append(value)

    async def aclose(self) -> None:
        self._closed = True
        self._closed_event.set()

    async def wait_closed(self) -> None:
        await self._closed_event.wait()


class _Session:
    def __init__(self) -> None:
        self.streams: list[_Stream] = []

    def in_stream(self, *_: object, **__: object) -> _Stream:
        stream = _Stream()
        self.streams.append(stream)
        return stream


@pytest.mark.asyncio
async def test_handles_are_owned_and_disconnect_closes_them() -> None:
    registry = InputRegistry()
    session = _Session()
    alice, bob = object(), object()
    opened = await registry.open(
        alice, "alice", session, "drive", frequency=50, credit_timeout=None
    )
    await registry.send(alice, opened["handle"], {"throttle": 0.2}, timeout=None)
    assert session.streams[0].sent == [{"throttle": 0.2}]
    with pytest.raises(StationError) as error:
        await registry.send(bob, opened["handle"], {}, timeout=None)
    assert error.value.code == "input_not_owned"
    await registry.close_owner(alice)
    assert session.streams[0]._closed
    assert await registry.list(alice) == []
