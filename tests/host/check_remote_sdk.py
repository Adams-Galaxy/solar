#!/usr/bin/env python3
from __future__ import annotations

import asyncio
import hashlib
import struct

from solar_remote import AsyncSession
from solar_remote.client import Hello
from solar_remote.protocol import (
    Envelope,
    INTROSPECTION_MANIFEST,
    INTROSPECTION_SERVER_INFORMATION,
    KIND_CANCEL,
    KIND_CLIENT_HELLO,
    KIND_INTROSPECTION,
    KIND_REQUEST,
    KIND_RESPONSE,
    KIND_RESPONSE_ACK,
    KIND_SERVER_HELLO,
    decode_frame,
    encode_frame,
)


class FakeChannel:
    """Already-open byte channel owned by the host test."""

    def __init__(self):
        self.incoming: asyncio.Queue[bytes | None] = asyncio.Queue()
        self.epoch = 7
        self.image = b"SLRM" + struct.pack("<HBBHHI", 2, 1, 0, 0, 0, 16)
        self.requests: list[Envelope] = []
        self.acknowledged: list[int] = []
        self.cancelled: list[int] = []
        self.incoming.put_nowait(self._hello_frame())

    def _hello_frame(self) -> bytes:
        payload = Hello(1, 0, 1024, 4096, 0x50).encode()
        return encode_frame(
            Envelope(kind=KIND_SERVER_HELLO, session_epoch=self.epoch), payload
        )

    async def receive(self, maximum: int) -> bytes:
        value = await self.incoming.get()
        return b"" if value is None else value

    async def send(self, data: bytes) -> None:
        envelope, payload = decode_frame(data)
        if envelope.kind == KIND_CLIENT_HELLO:
            await self.incoming.put(self._hello_frame())
        elif envelope.kind == KIND_INTROSPECTION:
            if envelope.target == INTROSPECTION_SERVER_INFORMATION:
                information = (
                    bytes((1, 1, 0, 1))
                    + struct.pack("<IIQ", 1024, 4096, 0x301)
                    + hashlib.sha256(self.image).digest()
                    + struct.pack("<I", len(self.image))
                )
                await self.incoming.put(
                    encode_frame(
                        Envelope(
                            kind=KIND_INTROSPECTION,
                            session_epoch=self.epoch,
                            target=envelope.target,
                            request_id=envelope.request_id,
                        ),
                        information,
                    )
                )
            elif envelope.target == INTROSPECTION_MANIFEST:
                offset, limit, reserved = struct.unpack("<IHH", payload)
                assert reserved == 0
                chunk = self.image[offset : offset + limit]
                await self.incoming.put(
                    encode_frame(
                        Envelope(
                            kind=KIND_INTROSPECTION,
                            session_epoch=self.epoch,
                            target=envelope.target,
                            request_id=envelope.request_id,
                        ),
                        struct.pack("<II", offset, len(self.image)) + chunk,
                    )
                )
        elif envelope.kind == KIND_REQUEST:
            if envelope.target == 99:
                return
            self.requests.append(envelope)
            if len(self.requests) == 2:
                for request in reversed(self.requests):
                    await self.incoming.put(
                        encode_frame(
                            Envelope(
                                kind=KIND_RESPONSE,
                                session_epoch=self.epoch,
                                target=request.target,
                                request_id=request.request_id,
                            ),
                            str(request.target).encode(),
                        )
                    )
        elif envelope.kind == KIND_RESPONSE_ACK:
            self.acknowledged.append(envelope.request_id)
        elif envelope.kind == KIND_CANCEL:
            self.cancelled.append(envelope.request_id)


class SilentChannel:
    def __init__(self):
        self.incoming: asyncio.Queue[bytes | None] = asyncio.Queue()

    async def receive(self, maximum: int) -> bytes:
        value = await self.incoming.get()
        return b"" if value is None else value

    async def send(self, data: bytes) -> None:
        return None


async def exercise() -> None:
    channel = FakeChannel()
    async with AsyncSession(channel) as session:
        assert session.server_information is not None
        assert session.server_information.build_id == 0x301
        assert session.manifest is not None and session.manifest.image == channel.image

        async def raw_request(target: int) -> bytes:
            request = session.core.request(target)
            return (await session._exchange(request)).payload

        first, second = await asyncio.gather(raw_request(10), raw_request(20))
        assert first == b"10" and second == b"20"
        assert len(channel.acknowledged) == 2
        session.timeout = 0.01
        try:
            await raw_request(99)
        except TimeoutError:
            pass
        else:
            raise AssertionError("unanswered request did not time out")
        assert channel.cancelled

    # Sessions consume an already-open channel and deliberately do not own it.
    silent = SilentChannel()
    try:
        await AsyncSession(silent, timeout=0.01).start()
    except TimeoutError:
        pass
    else:
        raise AssertionError("silent channel did not time out")


def main() -> int:
    asyncio.run(exercise())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
