from __future__ import annotations

import asyncio
from types import SimpleNamespace

import pytest

from solar_remote.client import Message
from solar_remote.protocol import (
    CreditGrant,
    Envelope,
    InStreamOpenResponse,
    KIND_RESPONSE,
    SubscriptionPolicy,
)
from solar_remote.session import InboundStream, SessionClosed


class _Codec:
    def encode(self, schema: int, value: object) -> bytes:
        assert schema == 7
        return bytes([int(value)])


class _Core:
    def __init__(self) -> None:
        self.server_hello = SimpleNamespace(minor=1)
        self.credits: dict[tuple[int, int], CreditGrant] = {}
        self.sent: list[tuple[int, bytes, int]] = []

    def subscribe(self, *_: object) -> int:
        return 1

    def close_in_stream(self, *_: object) -> int:
        return 2

    def send_stream(self, target: int, payload: bytes, token: int) -> None:
        grant = self.credits[(target, token)]
        self.credits[(target, token)] = CreditGrant(
            token, grant.credits - 1, grant.window
        )
        self.sent.append((target, payload, token))


class _Session:
    def __init__(self) -> None:
        self.core = _Core()
        self.codec = _Codec()
        self.manifest = SimpleNamespace(
            data=[{"id": 10, "name": "drive", "schema": 7}],
            capabilities=[
                {
                    "domain": "data",
                    "endpoint": 10,
                    "kind": "in_stream",
                    "explicit_open": True,
                    "codec": "cbor",
                }
            ],
        )
        self._in_streams: dict[tuple[int, int], InboundStream] = {}
        self._closed = False

    def _endpoint(self, collection: str, endpoint: int | str) -> dict:
        assert collection == "data"
        return self.manifest.data[0]

    async def _exchange(self, request: int) -> Message:
        if request == 1:
            payload = InStreamOpenResponse(
                SubscriptionPolicy(20_000, 1, 1), 44
            ).encode()
        else:
            payload = b""
        return Message(Envelope(kind=KIND_RESPONSE, request_id=request), payload)

    async def _flush(self) -> None:
        pass


@pytest.mark.asyncio
async def test_typed_producer_waits_for_token_scoped_credit() -> None:
    session = _Session()
    stream = InboundStream(session, "drive", frequency=50)  # type: ignore[arg-type]
    await stream.open()
    assert stream.token == 44

    send = asyncio.create_task(stream.send(9))
    await asyncio.sleep(0)
    assert not send.done()
    session.core.credits[(10, 44)] = CreditGrant(44, 1, 2)
    stream._notify_credit()
    await send
    assert session.core.sent == [(10, b"\x09", 44)]


@pytest.mark.asyncio
async def test_replacement_wakes_blocked_sender_and_discards_credit() -> None:
    session = _Session()
    stream = InboundStream(session, "drive")  # type: ignore[arg-type]
    await stream.open()
    send = asyncio.create_task(stream.send(1))
    await asyncio.sleep(0)
    stream._notify_closed(1)
    with pytest.raises(SessionClosed):
        await send
    assert stream.closure_reason == 1


@pytest.mark.asyncio
async def test_context_exit_correlates_explicit_close() -> None:
    session = _Session()
    async with InboundStream(session, "drive") as stream:  # type: ignore[arg-type]
        session.core.credits[(10, 44)] = CreditGrant(44, 1, 1)
        await stream.send(3)
    assert stream._closed
    assert (10, 44) not in session._in_streams
