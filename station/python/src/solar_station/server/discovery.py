"""Automatic discovery and pairing for Solar Remote and console transports."""

from __future__ import annotations

import asyncio
import json
from dataclasses import asdict, dataclass
from typing import Any
from urllib.parse import urlparse, urlunparse

from ..config import StationConfig

SIMULATOR_REMOTE_TARGET = "tcp://127.0.0.1:47000"
SIMULATOR_CONSOLE_PORT = 47001
BRIDGE_STATUS_PROTOCOL = 1
DISCOVERY_PROBE_TIMEOUT = 0.5


@dataclass(slots=True)
class BridgeStatus:
    host: str
    reachable: bool
    mode: str | None = None
    device: str | None = None
    generation: int | None = None
    serial: str | None = None
    detail: str | None = None
    error: str | None = None

    @property
    def available(self) -> bool:
        return self.reachable and self.mode == "active" and self.device == "connected"

    def to_wire(self) -> dict[str, Any]:
        return asdict(self) | {"available": self.available}


@dataclass(slots=True)
class DiscoverySnapshot:
    source: str | None = None
    remote_target: str | None = None
    console_target: str | None = None
    bridge: BridgeStatus | None = None
    detail: str = "discovery has not run"

    def to_wire(self) -> dict[str, Any]:
        return {
            "source": self.source,
            "remote_target": self.remote_target,
            "console_target": self.console_target,
            "detail": self.detail,
            "bridge": self.bridge.to_wire() if self.bridge else None,
        }


class TransportDiscovery:
    """Resolve and retain one coherent transport-selection snapshot."""

    def __init__(self, config: StationConfig):
        self.config = config
        self.snapshot = DiscoverySnapshot()
        self._lock = asyncio.Lock()

    def select_explicit_remote(self, target: str) -> None:
        self.snapshot = DiscoverySnapshot(
            source="explicit",
            remote_target=target,
            console_target=self.snapshot.console_target,
            detail="using the explicitly configured Remote target",
        )

    def select_explicit_console(self, target: str) -> None:
        self.snapshot.console_target = target

    async def resolve_remote(self) -> str | None:
        async with self._lock:
            from ..discovery import discover_usb

            usb = _physical_usb_targets(
                await asyncio.to_thread(
                    discover_usb,
                    vid=self.config.usb_vid,
                    pid=self.config.usb_pid,
                )
            )
            if usb:
                preferred = _matching_interface(usb, "remote")
                # Linux commonly exposes the interface descriptor. macOS currently
                # does not, but numbers the second CDC function after the first one.
                target = preferred[0].target if preferred else usb[-1].target
                self.snapshot = DiscoverySnapshot(
                    source="usb",
                    remote_target=target,
                    detail="selected a directly attached USB Remote interface",
                )
                return target

            bridge = await probe_bridge(
                self.config.bridge_host,
                port=self.config.bridge_status_port,
                wait_seconds=self.config.discovery_probe_timeout,
            )
            if bridge is not None and bridge.available:
                remote = _tcp_target(bridge.host, self.config.bridge_remote_port)
                console = _tcp_target(bridge.host, self.config.bridge_console_port)
                self.snapshot = DiscoverySnapshot(
                    source="bridge",
                    remote_target=remote,
                    console_target=console,
                    bridge=bridge,
                    detail="selected the connected Solar Bridge",
                )
                return remote

            if await tcp_target_reachable(
                SIMULATOR_REMOTE_TARGET,
                wait_seconds=self.config.discovery_probe_timeout,
            ):
                self.snapshot = DiscoverySnapshot(
                    source="simulator",
                    remote_target=SIMULATOR_REMOTE_TARGET,
                    console_target=_tcp_target("127.0.0.1", SIMULATOR_CONSOLE_PORT),
                    bridge=bridge,
                    detail="selected the local simulator",
                )
                return SIMULATOR_REMOTE_TARGET

            self.snapshot = DiscoverySnapshot(
                bridge=bridge,
                detail=_unavailable_detail(bridge),
            )
            return None

    async def resolve_console(self, remote_target: str | None) -> str | None:
        if remote_target is None:
            if self.config.remote_target != "auto":
                remote_target = self.config.remote_target
            else:
                async with self._lock:
                    remote_target = self.snapshot.remote_target
                if remote_target is None:
                    remote_target = await self.resolve_remote()

        async with self._lock:
            if (
                remote_target is not None
                and remote_target == self.snapshot.remote_target
                and self.snapshot.console_target is not None
            ):
                return self.snapshot.console_target

            if remote_target is not None:
                parsed = urlparse(remote_target)
                if parsed.scheme == "tcp" and parsed.hostname:
                    port = (
                        self.config.bridge_console_port
                        if self.snapshot.source == "bridge"
                        else SIMULATOR_CONSOLE_PORT
                    )
                    target = _tcp_target(parsed.hostname, port)
                    self.snapshot.console_target = target
                    return target

            from ..discovery import discover_usb

            usb = _physical_usb_targets(
                await asyncio.to_thread(
                    discover_usb,
                    vid=self.config.usb_vid,
                    pid=self.config.usb_pid,
                )
            )
            preferred = _matching_interface(usb, "console")
            if preferred:
                target = preferred[0].target
                self.snapshot.console_target = target
                return target
            candidates = [item.target for item in usb if item.target != remote_target]
            if candidates:
                self.snapshot.console_target = candidates[0]
                return candidates[0]
            return None

    def unavailable_detail(self) -> str:
        return self.snapshot.detail

    def to_wire(self) -> dict[str, Any]:
        return self.snapshot.to_wire()


async def probe_bridge(
    host: str | None,
    *,
    port: int = 46999,
    wait_seconds: float = DISCOVERY_PROBE_TIMEOUT,
) -> BridgeStatus | None:
    """Read and validate one Solar Bridge status response."""
    if not host:
        return None
    writer: asyncio.StreamWriter | None = None
    try:
        reader, writer = await asyncio.wait_for(
            asyncio.open_connection(host, port), wait_seconds
        )
        line = await asyncio.wait_for(reader.readline(), wait_seconds)
        if not line:
            raise ValueError("status connection closed without a response")
        if len(line) > 4096:
            raise ValueError("status response exceeds 4096 bytes")
        payload = json.loads(line)
        if not isinstance(payload, dict):
            raise ValueError("status response is not an object")
        if payload.get("protocol") != BRIDGE_STATUS_PROTOCOL:
            raise ValueError("unsupported status protocol")
        mode = payload.get("mode")
        device = payload.get("device")
        if not isinstance(mode, str) or not isinstance(device, str):
            raise ValueError("status response omits mode or device")
        return BridgeStatus(
            host=host,
            reachable=True,
            mode=mode,
            device=device,
            generation=_optional_int(payload.get("generation")),
            serial=_optional_string(payload.get("serial")),
            detail=_optional_string(payload.get("detail")),
        )
    except (OSError, TimeoutError, ValueError, json.JSONDecodeError) as error:
        return BridgeStatus(
            host=host,
            reachable=False,
            error=str(error) or type(error).__name__,
        )
    finally:
        if writer is not None:
            writer.close()
            try:
                await writer.wait_closed()
            except OSError:
                pass


async def discover_remote_target(
    *,
    vid: int | None,
    pid: int | None,
    bridge_host: str | None = None,
) -> str | None:
    """Compatibility wrapper around first-class transport discovery."""
    config = StationConfig.defaults()
    config.usb_vid = vid
    config.usb_pid = pid
    config.bridge_host = bridge_host
    return await TransportDiscovery(config).resolve_remote()


async def discover_console_target(
    remote_target: str | None,
    *,
    vid: int | None,
    pid: int | None,
    bridge_host: str | None = None,
) -> str | None:
    """Compatibility wrapper around first-class transport pairing."""
    config = StationConfig.defaults()
    config.usb_vid = vid
    config.usb_pid = pid
    config.bridge_host = bridge_host
    resolver = TransportDiscovery(config)
    return await resolver.resolve_console(remote_target)


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


def _tcp_target(host: str, port: int) -> str:
    return urlunparse(("tcp", f"{host}:{port}", "", "", "", ""))


def _unavailable_detail(bridge: BridgeStatus | None) -> str:
    if bridge is None:
        return "no directly attached USB device or local simulator found"
    if not bridge.reachable:
        return f"Solar Bridge {bridge.host} is unreachable" + (
            f": {bridge.error}" if bridge.error else ""
        )
    if bridge.mode != "active":
        return f"Solar Bridge {bridge.host} is in {bridge.mode} mode"
    if bridge.device != "connected":
        return (
            f"Solar Bridge {bridge.host} is reachable but its device is {bridge.device}"
        )
    return f"Solar Bridge {bridge.host} is unavailable"


def _optional_int(value: object) -> int | None:
    return value if isinstance(value, int) and not isinstance(value, bool) else None


def _optional_string(value: object) -> str | None:
    return value if isinstance(value, str) else None


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
