"""Independent console/log connection supervision."""

from __future__ import annotations

import asyncio
import logging
from collections.abc import Callable
from contextlib import suppress
from typing import Any
from urllib.parse import urlparse

from ..config import StationConfig
from ..models import SourceKind
from .connection import SolarConnectionSupervisor
from .events import EventHub

LOGGER = logging.getLogger("solar_station.console")


class ConsoleConnectionSupervisor:
    def __init__(
        self,
        config: StationConfig,
        events: EventHub,
        connection: SolarConnectionSupervisor,
    ):
        self.config = config
        self.events = events
        self.connection = connection
        self.connected = False
        self.target: str | None = None
        self.last_error: str | None = None
        self._task: asyncio.Task[None] | None = None
        self._stop = asyncio.Event()
        self._device: Any = None
        self._logged_error: str | None = None

    async def start(self) -> None:
        if self.config.console_target is None:
            LOGGER.info("Console capture disabled")
            return
        LOGGER.info(
            "Console supervisor started (target=%s)", self.config.console_target
        )
        self._task = asyncio.create_task(self._run(), name="station-console")

    async def _resolve_target(self) -> str:
        if self.config.console_target not in (None, "auto"):
            return self.config.console_target
        from .discovery import discover_console_target

        target = await discover_console_target(
            self.connection.target,
            vid=self.config.usb_vid,
            pid=self.config.usb_pid,
        )
        if target is None:
            raise ConnectionError(
                "no matching simulator or console USB interface found"
            )
        return target

    async def _run(self) -> None:
        delay = self.config.reconnect_initial
        while not self._stop.is_set():
            try:
                target = await self._resolve_target()
                self.target = target
                parsed = urlparse(target)
                if parsed.scheme == "tcp":
                    await self._read_tcp(parsed.hostname, parsed.port)
                elif parsed.scheme in ("serial", "usb"):
                    port = parsed.path or parsed.netloc
                    await self._read_serial(port)
                else:
                    raise ValueError(f"unsupported console target {target!r}")
                if not self._stop.is_set():
                    self.last_error = "console connection closed"
            except asyncio.CancelledError:
                raise
            except BaseException as error:
                if self._stop.is_set():
                    break
                self.last_error = str(error) or type(error).__name__
                if self.last_error != self._logged_error:
                    LOGGER.warning("Console unavailable: %s", self.last_error)
                    self._logged_error = self.last_error
                else:
                    LOGGER.debug("Console still unavailable: %s", self.last_error)
            finally:
                self.connected = False
            if not self._stop.is_set():
                try:
                    await asyncio.wait_for(self._stop.wait(), delay)
                except TimeoutError:
                    pass
                delay = min(self.config.reconnect_maximum, delay * 2)

    async def _read_tcp(self, host: str | None, port: int | None) -> None:
        if not host or port is None:
            raise ValueError("console TCP target requires host and port")
        reader, writer = await asyncio.open_connection(host, port)

        def mark_connected() -> None:
            self.connected = True
            self.last_error = None
            self._logged_error = None
            LOGGER.info("Console connected to tcp://%s:%s", host, port)

        try:
            await self._read_lines(
                reader.read,
                idle_timeout=10.0,
                empty_is_disconnect=True,
                on_first_data=mark_connected,
            )
        finally:
            writer.close()
            await writer.wait_closed()

    async def _read_serial(self, port: str) -> None:
        if not port:
            raise ValueError("console serial target requires a device path")
        import serial

        device = await asyncio.to_thread(serial.Serial, port, 115200, timeout=0.1)
        self._device = device
        self.connected = True
        self.last_error = None
        self._logged_error = None
        LOGGER.info("Console connected to %s", port)

        async def read(_: int) -> bytes:
            return await asyncio.to_thread(device.read, 4096)

        try:
            await self._read_lines(read)
        finally:
            self._device = None
            await asyncio.to_thread(device.close)

    async def _read_lines(
        self,
        read: Any,
        *,
        idle_timeout: float | None = None,
        empty_is_disconnect: bool = False,
        on_first_data: Callable[[], None] | None = None,
    ) -> None:
        buffer = bytearray()
        received_data = False
        while not self._stop.is_set():
            try:
                if idle_timeout is None:
                    chunk = await read(4096)
                else:
                    chunk = await asyncio.wait_for(read(4096), idle_timeout)
            except TimeoutError as error:
                raise ConnectionError(
                    f"console stream was idle for {idle_timeout:g} seconds"
                ) from error
            if not chunk:
                if empty_is_disconnect:
                    raise ConnectionError("console connection closed")
                await asyncio.sleep(0)
                continue
            if not received_data:
                received_data = True
                if on_first_data is not None:
                    on_first_data()
            buffer.extend(chunk)
            while (newline := buffer.find(b"\n")) >= 0:
                raw = bytes(buffer[:newline])
                del buffer[: newline + 1]
                if raw.endswith(b"\r"):
                    raw = raw[:-1]
                await self.events.publish(
                    "logs.console",
                    SourceKind.LOG,
                    raw.decode("utf-8", errors="replace"),
                )

    async def close(self) -> None:
        self._stop.set()
        if self._device is not None:
            with suppress(BaseException):
                await asyncio.to_thread(self._device.close)
        task, self._task = self._task, None
        if task is not None:
            task.cancel()
            with suppress(asyncio.CancelledError):
                await task
        LOGGER.info("Console supervisor stopped")
