"""Bounded length-prefixed CBOR protocol used by Solar Station."""

from __future__ import annotations

import asyncio
import struct
from collections.abc import Mapping
from typing import Any

import cbor2

from .errors import ProtocolError

IPC_VERSION = 1
DEFAULT_MAXIMUM_MESSAGE = 1 << 20
_LENGTH = struct.Struct("<I")


def encode_message(
    message: Mapping[str, Any], maximum: int = DEFAULT_MAXIMUM_MESSAGE
) -> bytes:
    body = encode_body(message, maximum)
    return _LENGTH.pack(len(body)) + body


def encode_body(
    message: Mapping[str, Any], maximum: int = DEFAULT_MAXIMUM_MESSAGE
) -> bytes:
    try:
        body = cbor2.dumps(dict(message), canonical=True)
    except (TypeError, ValueError, cbor2.CBOREncodeError) as error:
        raise ProtocolError(f"IPC message is not CBOR serializable: {error}") from error
    if not body or len(body) > maximum:
        raise ProtocolError(
            f"IPC message size {len(body)} is outside the limit {maximum}"
        )
    return body


def decode_message(
    body: bytes, maximum: int = DEFAULT_MAXIMUM_MESSAGE
) -> dict[str, Any]:
    if not body or len(body) > maximum:
        raise ProtocolError("IPC message is empty or oversized")
    try:
        value = cbor2.loads(body)
    except (ValueError, cbor2.CBORDecodeError) as error:
        raise ProtocolError(f"IPC message is malformed CBOR: {error}") from error
    if not isinstance(value, dict) or not all(isinstance(key, str) for key in value):
        raise ProtocolError("IPC message must be a CBOR map with string keys")
    return value


async def read_message(
    reader: asyncio.StreamReader, maximum: int = DEFAULT_MAXIMUM_MESSAGE
) -> dict[str, Any]:
    header = await reader.readexactly(_LENGTH.size)
    size = _LENGTH.unpack(header)[0]
    if not 0 < size <= maximum:
        raise ProtocolError(f"IPC message length {size} is invalid")
    return decode_message(await reader.readexactly(size), maximum)


async def write_message(
    writer: asyncio.StreamWriter,
    message: Mapping[str, Any],
    maximum: int = DEFAULT_MAXIMUM_MESSAGE,
) -> None:
    writer.write(encode_message(message, maximum))
    await writer.drain()


def require_type(message: Mapping[str, Any], expected: str) -> None:
    if message.get("type") != expected:
        raise ProtocolError(f"expected {expected!r} IPC message")
