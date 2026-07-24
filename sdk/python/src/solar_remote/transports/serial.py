"""pySerial transport bridged through one owned reader thread."""

from __future__ import annotations

import asyncio
import threading
from typing import Any, Callable

import serial


class SerialTransport:
    def __init__(
        self,
        port: str,
        baudrate: int = 115200,
        *,
        queue_depth: int = 32,
        serial_factory: Callable[..., Any] = serial.Serial,
    ):
        if baudrate == 134:
            raise ValueError(
                "134 baud is reserved for the explicit Teensy reboot trigger"
            )
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

    async def open(self) -> None:
        if self._serial is not None:
            raise RuntimeError("transport is already open")
        self._loop = asyncio.get_running_loop()
        self._queue = asyncio.Queue(self.queue_depth)
        self._closing.clear()
        self._serial = await asyncio.to_thread(
            self._factory, self.port, self.baudrate, timeout=0.1, write_timeout=1.0
        )
        self._thread = threading.Thread(
            target=self._reader, name=f"solar-remote:{self.port}", daemon=True
        )
        self._thread.start()

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
        try:
            while not self._closing.is_set():
                value = self._serial.read(4096)
                if value and self._loop is not None and not self._loop.is_closed():
                    self._loop.call_soon_threadsafe(self._deliver, bytes(value))
        except BaseException as error:
            if self._loop is not None and not self._loop.is_closed():
                self._loop.call_soon_threadsafe(self._deliver, error)
        finally:
            if self._loop is not None and not self._loop.is_closed():
                self._loop.call_soon_threadsafe(self._deliver, None)

    async def read(self, maximum: int) -> bytes:
        if self._queue is None:
            raise RuntimeError("transport is not open")
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
        # The protocol decoder accepts arbitrary chunks; retain the tail in order.
        self._remainder = value[maximum:]
        return value[:maximum]

    async def write(self, data: bytes) -> None:
        if self._serial is None:
            raise RuntimeError("transport is not open")

        def write_all() -> None:
            offset = 0
            while offset < len(data):
                written = self._serial.write(data[offset:])
                if not written:
                    raise serial.SerialTimeoutException("serial write made no progress")
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
