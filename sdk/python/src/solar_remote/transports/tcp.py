"""asyncio TCP transport used by the containerized simulator."""

from __future__ import annotations

import asyncio


class TcpTransport:
    def __init__(self, host: str = "127.0.0.1", port: int = 47000):
        self.host = host
        self.port = port
        self._reader: asyncio.StreamReader | None = None
        self._writer: asyncio.StreamWriter | None = None

    async def open(self) -> None:
        if self._writer is not None:
            raise RuntimeError("transport is already open")
        self._reader, self._writer = await asyncio.open_connection(self.host, self.port)

    async def read(self, maximum: int) -> bytes:
        if self._reader is None:
            raise RuntimeError("transport is not open")
        return await self._reader.read(maximum)

    async def write(self, data: bytes) -> None:
        if self._writer is None:
            raise RuntimeError("transport is not open")
        self._writer.write(data)
        await self._writer.drain()

    async def close(self) -> None:
        writer, self._reader, self._writer = self._writer, None, None
        if writer is not None:
            writer.close()
            await writer.wait_closed()
