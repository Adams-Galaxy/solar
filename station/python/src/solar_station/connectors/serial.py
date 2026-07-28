"""Station-owned serial channel bridged through one reader thread."""

from __future__ import annotations

import asyncio
import threading
from collections.abc import Callable
from typing import Any

import serial


class SerialChannel:
    def __init__(
        self,
        port: str,
        baudrate: int,
        *,
        queue_depth: int,
        serial_factory: Callable[..., Any],
    ):
        self.port = port
        self.baudrate = baudrate
        self.queue_depth = queue_depth
        self._factory = serial_factory
        self._serial: Any = None
        self._loop: asyncio.AbstractEventLoop | None = None
        self._queue: asyncio.Queue[bytes | BaseException | None] | None = None
        self._thread: threading.Thread | None = None
        self._closing = threading.Event()
        self._remainder = b""

    @classmethod
    async def open(
        cls,
        port: str,
        baudrate: int = 115200,
        *,
        queue_depth: int = 32,
        serial_factory: Callable[..., Any] = serial.Serial,
    ) -> SerialChannel:
        if baudrate == 134:
            raise ValueError(
                "134 baud is reserved for the explicit Teensy reboot trigger"
            )
        channel = cls(
            port,
            baudrate,
            queue_depth=queue_depth,
            serial_factory=serial_factory,
        )
        channel._loop = asyncio.get_running_loop()
        channel._queue = asyncio.Queue(queue_depth)
        channel._serial = await asyncio.to_thread(
            serial_factory,
            port,
            baudrate,
            timeout=0.1,
            write_timeout=1.0,
        )
        channel._thread = threading.Thread(
            target=channel._reader,
            name=f"solar-station:{port}",
            daemon=True,
        )
        channel._thread.start()
        return channel

    def _deliver(self, value: bytes | BaseException | None) -> None:
        queue = self._queue
        if queue is None:
            return
        if queue.full():
            while not queue.empty():
                queue.get_nowait()
            value = BufferError("serial receive queue overflowed")
        queue.put_nowait(value)

    def _reader(self) -> None:
        device = self._serial
        if device is None:
            return
        try:
            while not self._closing.is_set():
                value = _read_available(device, 4096)
                if value and self._loop is not None and not self._loop.is_closed():
                    self._loop.call_soon_threadsafe(self._deliver, bytes(value))
        except BaseException as error:
            if (
                not self._closing.is_set()
                and self._loop is not None
                and not self._loop.is_closed()
            ):
                self._loop.call_soon_threadsafe(self._deliver, error)
        finally:
            if self._loop is not None and not self._loop.is_closed():
                self._loop.call_soon_threadsafe(self._deliver, None)

    async def receive(self, maximum: int) -> bytes:
        if self._queue is None:
            raise RuntimeError("serial channel is not open")
        if self._remainder:
            value, self._remainder = (
                self._remainder[:maximum],
                self._remainder[maximum:],
            )
            return value
        value = await self._queue.get()
        if isinstance(value, BaseException):
            raise value
        if value is None:
            return b""
        if len(value) <= maximum:
            return value
        self._remainder = value[maximum:]
        return value[:maximum]

    async def send(self, data: bytes) -> None:
        if self._serial is None:
            raise RuntimeError("serial channel is not open")

        def write_all() -> None:
            offset = 0
            while offset < len(data):
                written = self._serial.write(data[offset:])
                if not written:
                    raise serial.SerialTimeoutException(
                        "serial write made no progress"
                    )
                offset += written

        await asyncio.to_thread(write_all)

    async def close(self) -> None:
        device, thread = self._serial, self._thread
        self._serial = None
        self._thread = None
        self._closing.set()
        self._remainder = b""
        if device is not None:
            await asyncio.to_thread(device.close)
        if thread is not None:
            await asyncio.to_thread(thread.join, 2.0)
            if thread.is_alive():
                raise RuntimeError("serial reader thread did not stop")
        self._deliver(None)


def _read_available(device: Any, maximum: int) -> bytes:
    """Block for the first byte, then drain an already-buffered burst."""
    waiting = max(0, int(device.in_waiting))
    return device.read(min(maximum, waiting) if waiting else 1)
