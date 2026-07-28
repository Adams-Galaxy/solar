"""The byte-stream boundary consumed by Solar Remote."""

from __future__ import annotations

from typing import Protocol, runtime_checkable


@runtime_checkable
class AsyncByteChannel(Protocol):
    """An already-open, ordered, bidirectional byte stream.

    Channel creation, reconnection, and closure belong to the host application.
    Solar Remote only consumes the stream while a protocol session is active.
    """

    async def receive(self, maximum: int) -> bytes: ...

    async def send(self, data: bytes) -> None: ...
