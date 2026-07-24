#!/usr/bin/env python3
from __future__ import annotations

import asyncio
import hashlib
import queue
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
from solar_remote.transports import SerialTransport


class FakeTransport:
    def __init__(self):
        self.incoming: asyncio.Queue[bytes | None] = asyncio.Queue()
        self.closed = False
        self.epoch = 7
        self.image = b"SLRM" + struct.pack("<HBBHHI", 2, 1, 0, 0, 0, 16)
        self.requests: list[Envelope] = []
        self.acknowledged: list[int] = []
        self.cancelled: list[int] = []

    async def open(self) -> None:
        await self._hello()

    async def _hello(self) -> None:
        payload = Hello(1, 0, 1024, 4096, 0x50).encode()
        await self.incoming.put(
            encode_frame(
                Envelope(kind=KIND_SERVER_HELLO, session_epoch=self.epoch), payload
            )
        )

    async def read(self, maximum: int) -> bytes:
        value = await self.incoming.get()
        return b"" if value is None else value

    async def write(self, data: bytes) -> None:
        envelope, payload = decode_frame(data)
        if envelope.kind == KIND_CLIENT_HELLO:
            await self._hello()
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

    async def close(self) -> None:
        self.closed = True
        await self.incoming.put(None)


class FakeSerial:
    def __init__(self):
        self.incoming: queue.Queue[bytes | None] = queue.Queue()
        self.written = bytearray()

    def read(self, _: int) -> bytes:
        value = self.incoming.get(timeout=1)
        return b"" if value is None else value

    def write(self, data: bytes) -> int:
        count = min(2, len(data))
        self.written.extend(data[:count])
        return count

    def close(self) -> None:
        self.incoming.put(None)


class SilentTransport:
    def __init__(self):
        self.closed = False
        self.incoming: asyncio.Queue[bytes | None] = asyncio.Queue()

    async def open(self) -> None:
        return None

    async def read(self, maximum: int) -> bytes:
        value = await self.incoming.get()
        return b"" if value is None else value

    async def write(self, data: bytes) -> None:
        return None

    async def close(self) -> None:
        self.closed = True
        await self.incoming.put(None)


async def exercise() -> None:
    transport = FakeTransport()
    async with AsyncSession(transport) as session:
        assert session.server_information is not None
        assert session.server_information.build_id == 0x301
        assert (
            session.manifest is not None and session.manifest.image == transport.image
        )

        async def raw_request(target: int) -> bytes:
            request = session.core.request(target)
            return (await session._exchange(request)).payload

        first, second = await asyncio.gather(raw_request(10), raw_request(20))
        assert first == b"10" and second == b"20"
        assert len(transport.acknowledged) == 2
        session.timeout = 0.01
        try:
            await raw_request(99)
        except TimeoutError:
            pass
        else:
            raise AssertionError("unanswered request did not time out")
        assert transport.cancelled
    assert transport.closed

    device = FakeSerial()
    serial_transport = SerialTransport(
        "/dev/fake", serial_factory=lambda *_args, **_kwargs: device
    )
    await serial_transport.open()
    device.incoming.put(b"abcdef")
    assert await serial_transport.read(3) == b"abc"
    assert await serial_transport.read(3) == b"def"
    await serial_transport.write(b"partial")
    assert device.written == b"partial"
    await serial_transport.close()
    try:
        SerialTransport("/dev/fake", baudrate=134)
    except ValueError:
        pass
    else:
        raise AssertionError("reserved Teensy reboot baud was accepted")

    silent = SilentTransport()
    try:
        await AsyncSession(silent, timeout=0.01).open()
    except TimeoutError:
        pass
    else:
        raise AssertionError("silent transport did not time out")
    assert silent.closed


def main() -> int:
    asyncio.run(exercise())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
