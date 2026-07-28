"""Connection target parsing and ownership."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any
from urllib.parse import urlparse

from solar_remote import AsyncByteChannel


@dataclass(slots=True)
class ConnectedTarget:
    """One open physical target owned by Station."""

    target: str
    kind: str
    channel: AsyncByteChannel
    _owner: Any

    async def close(self) -> None:
        await self._owner.close()


async def open_target(target: str) -> ConnectedTarget:
    """Open a supported Station target and return its channel."""
    parsed = urlparse(target)
    if parsed.scheme == "tcp":
        if not parsed.hostname or parsed.port is None:
            raise ValueError("TCP target requires host and port")
        from .tcp import TcpChannel

        channel = await TcpChannel.open(parsed.hostname, parsed.port)
        return ConnectedTarget(target, "tcp", channel, channel)
    if parsed.scheme in ("serial", "usb"):
        port = parsed.path or parsed.netloc
        if not port:
            raise ValueError("serial target requires a device path")
        from .serial import SerialChannel

        channel = await SerialChannel.open(port)
        return ConnectedTarget(target, "usb", channel, channel)
    raise ValueError(f"unsupported Remote target scheme {parsed.scheme!r}")
