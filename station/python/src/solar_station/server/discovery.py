"""Automatic discovery and pairing for Solar Remote and console transports."""

from __future__ import annotations

import asyncio
from typing import Any
from urllib.parse import urlparse, urlunparse

SIMULATOR_REMOTE_TARGET = "tcp://127.0.0.1:47000"
SIMULATOR_CONSOLE_PORT = 47001
DISCOVERY_PROBE_TIMEOUT = 0.15


async def discover_remote_target(
    *, vid: int | None, pid: int | None
) -> str | None:
    """Prefer attached USB hardware, then a reachable local simulator."""
    from solar_remote.discovery import discover_usb

    usb = _physical_usb_targets(
        await asyncio.to_thread(discover_usb, vid=vid, pid=pid)
    )
    if usb:
        preferred = _matching_interface(usb, "remote")
        # Linux commonly exposes the interface descriptor. macOS currently
        # does not, but numbers the second CDC function after the first one.
        # The firmware contract places console first and Remote second.
        return preferred[0].target if preferred else usb[-1].target
    if await tcp_target_reachable(SIMULATOR_REMOTE_TARGET):
        return SIMULATOR_REMOTE_TARGET
    return None


async def discover_console_target(
    remote_target: str | None, *, vid: int | None, pid: int | None
) -> str | None:
    """Pair console capture with the transport selected for Solar Remote."""
    if remote_target is not None:
        parsed = urlparse(remote_target)
        if parsed.scheme == "tcp" and parsed.hostname:
            netloc = f"{parsed.hostname}:{SIMULATOR_CONSOLE_PORT}"
            return urlunparse(("tcp", netloc, "", "", "", ""))

    from solar_remote.discovery import discover_usb

    usb = _physical_usb_targets(
        await asyncio.to_thread(discover_usb, vid=vid, pid=pid)
    )
    if remote_target is None:
        preferred = _matching_interface(usb, "console")
        if preferred:
            return preferred[0].target
        if len(usb) >= 2:
            return usb[0].target
        if not usb and await tcp_target_reachable(SIMULATOR_REMOTE_TARGET):
            return f"tcp://127.0.0.1:{SIMULATOR_CONSOLE_PORT}"
        return None
    candidates = [item.target for item in usb if item.target != remote_target]
    preferred = [
        item.target
        for item in _matching_interface(usb, "console")
        if item.target != remote_target
    ]
    return (preferred or candidates)[0] if preferred or candidates else None


async def tcp_target_reachable(
    target: str, *, wait_seconds: float = DISCOVERY_PROBE_TIMEOUT
) -> bool:
    parsed = urlparse(target)
    if parsed.scheme != "tcp" or not parsed.hostname or parsed.port is None:
        return False
    try:
        reader, writer = await asyncio.wait_for(
            asyncio.open_connection(parsed.hostname, parsed.port), wait_seconds
        )
    except (OSError, TimeoutError):
        return False
    del reader
    writer.close()
    await writer.wait_closed()
    return True


def _physical_usb_targets(targets: list[Any]) -> list[Any]:
    """Exclude platform pseudo-serial ports such as macOS Bluetooth."""
    return [
        target
        for target in targets
        if getattr(target, "vid", None) is not None
        and getattr(target, "pid", None) is not None
    ]


def _matching_interface(targets: list[Any], role: str) -> list[Any]:
    """Prefer composite USB interfaces by their descriptor rather than port order."""
    return [
        target
        for target in targets
        if role in str(getattr(target, "interface", "") or "").casefold()
    ]
