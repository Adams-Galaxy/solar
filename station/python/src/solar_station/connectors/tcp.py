"""Station-owned TCP channel."""

from __future__ import annotations

import asyncio
import socket


class TcpChannel:
    def __init__(
        self,
        reader: asyncio.StreamReader,
        writer: asyncio.StreamWriter,
    ):
        self._reader = reader
        self._writer = writer

    @classmethod
    async def open(cls, host: str, port: int) -> TcpChannel:
        # Station targets are always IPv4 LAN hosts (a Solar Bridge or the
        # local simulator). Unrestricted (AF_UNSPEC) resolution of .local
        # names can stall for seconds on hosts that don't answer mDNS AAAA
        # queries before falling back to the working A record, so skip the
        # dual-stack wait entirely.
        reader, writer = await asyncio.open_connection(host, port, family=socket.AF_INET)
        return cls(reader, writer)

    async def receive(self, maximum: int) -> bytes:
        return await self._reader.read(maximum)

    async def send(self, data: bytes) -> None:
        self._writer.write(data)
        await self._writer.drain()

    async def close(self) -> None:
        self._writer.close()
        await self._writer.wait_closed()
