"""Connection target selection."""

from __future__ import annotations

from pathlib import Path
from urllib.parse import urlparse

from .manifest import Manifest, parse_manifest
from .session import AsyncSession
from .transports import SerialTransport, TcpTransport


def connect(
    target: str = "tcp://127.0.0.1:47000",
    *,
    manifest: Manifest | Path | None = None,
    cache: Path | None = None,
    timeout: float = 5.0,
) -> AsyncSession:
    parsed = urlparse(target)
    resolved_manifest = (
        parse_manifest(manifest.read_bytes())
        if isinstance(manifest, Path)
        else manifest
    )
    if parsed.scheme == "tcp":
        if not parsed.hostname or parsed.port is None:
            raise ValueError("TCP target requires host and port")
        transport = TcpTransport(parsed.hostname, parsed.port)
    elif parsed.scheme in ("serial", "usb"):
        port = parsed.path or parsed.netloc
        if not port:
            raise ValueError("serial target requires a device path")
        transport = SerialTransport(port)
    else:
        raise ValueError(f"unsupported Remote target scheme {parsed.scheme!r}")
    return AsyncSession(
        transport, manifest=resolved_manifest, cache=cache, timeout=timeout
    )
