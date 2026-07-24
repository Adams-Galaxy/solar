from __future__ import annotations

import asyncio

import pytest

from solar_station.errors import ProtocolError
from solar_station.protocol import decode_message, encode_message, read_message


def test_cbor_message_round_trip_and_bounds() -> None:
    framed = encode_message({"type": "request", "id": 4, "payload": b"\x01"})
    size = int.from_bytes(framed[:4], "little")
    assert decode_message(framed[4:]) == {
        "type": "request",
        "id": 4,
        "payload": b"\x01",
    }
    assert size == len(framed) - 4
    with pytest.raises(ProtocolError):
        encode_message({"value": "x" * 100}, maximum=8)
    with pytest.raises(ProtocolError):
        decode_message(b"\x01", maximum=8)


@pytest.mark.asyncio
async def test_reader_accepts_arbitrary_stream_chunking() -> None:
    reader = asyncio.StreamReader()
    framed = encode_message({"type": "hello", "version": 1})
    task = asyncio.create_task(read_message(reader))
    for byte in framed:
        reader.feed_data(bytes((byte,)))
        await asyncio.sleep(0)
    assert await task == {"type": "hello", "version": 1}


@pytest.mark.asyncio
async def test_reader_rejects_oversized_header_before_body() -> None:
    reader = asyncio.StreamReader()
    reader.feed_data((1025).to_bytes(4, "little"))
    with pytest.raises(ProtocolError):
        await read_message(reader, maximum=1024)
