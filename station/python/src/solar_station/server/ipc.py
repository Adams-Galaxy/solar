"""Unix-socket listener and selective local event fan-out."""

from __future__ import annotations

import asyncio
import logging
import os
import stat
from contextlib import suppress
from typing import TYPE_CHECKING, Any

from ..errors import ProtocolError, StationError, wrap_error
from ..models import StationEvent
from ..protocol import IPC_VERSION, read_message, write_message

if TYPE_CHECKING:
    from .runtime import StationHost

LOGGER = logging.getLogger("solar_station.ipc")


class ClientConnection:
    def __init__(
        self,
        server: UnixSocketServer,
        reader: asyncio.StreamReader,
        writer: asyncio.StreamWriter,
    ):
        self.server = server
        self.reader = reader
        self.writer = writer
        self.name = "unknown"
        self.subscriptions: set[str] = set()
        self._write_lock = asyncio.Lock()
        self._events: asyncio.Queue[StationEvent | None] = asyncio.Queue(
            server.runtime.config.client_event_queue
        )
        self._event_task: asyncio.Task[None] | None = None
        self._closed = False
        self._handshaken = False

    async def run(self) -> None:
        try:
            hello = await read_message(
                self.reader, self.server.runtime.config.maximum_ipc_message
            )
            if hello.get("type") != "hello":
                raise ProtocolError("first Station IPC message must be hello")
            if hello.get("version") != IPC_VERSION:
                raise ProtocolError(
                    f"unsupported Station IPC version {hello.get('version')!r}"
                )
            self.name = str(hello.get("client", "unknown"))
            self._handshaken = True
            LOGGER.info("Client connected: %s", self.name)
            await self.send(
                {
                    "type": "hello_response",
                    "version": IPC_VERSION,
                    **self.server.runtime.hello_information(),
                }
            )
            self._event_task = asyncio.create_task(
                self._event_writer(), name=f"station-events:{self.name}"
            )
            while True:
                message = await read_message(
                    self.reader, self.server.runtime.config.maximum_ipc_message
                )
                await self._handle(message)
        except (asyncio.IncompleteReadError, ConnectionResetError, BrokenPipeError):
            pass
        except BaseException as error:
            if not isinstance(error, asyncio.CancelledError):
                with suppress(BaseException):
                    await self.send(
                        {"type": "error", "error": wrap_error(error).to_wire()}
                    )
        finally:
            await self.close()

    async def _handle(self, message: dict[str, Any]) -> None:
        message_type = message.get("type")
        if message_type == "request":
            request_id = message.get("id")
            if not isinstance(request_id, int):
                raise ProtocolError("request ID must be an integer")
            operation = message.get("operation")
            arguments = message.get("arguments", {})
            if not isinstance(operation, str) or not isinstance(arguments, dict):
                await self._send_error(
                    request_id, ProtocolError("malformed request operation")
                )
                return
            try:
                result = await self.server.runtime.handle_request(
                    operation,
                    arguments,
                    owner=self,
                    owner_name=self.name,
                )
                await self.send(
                    {"type": "response", "id": request_id, "result": result}
                )
            except BaseException as error:
                await self._send_error(request_id, error)
        elif message_type in ("subscribe", "unsubscribe"):
            request_id = message.get("id")
            source = message.get("source")
            if not isinstance(request_id, int) or not isinstance(source, str):
                raise ProtocolError("malformed subscription request")
            try:
                canonical = self.server.runtime.sources.validate_subscription(source)
                if message_type == "subscribe":
                    self.subscriptions.add(canonical)
                else:
                    self.subscriptions.discard(canonical)
                await self.send(
                    {
                        "type": "response",
                        "id": request_id,
                        "result": {
                            "source": canonical,
                            "subscribed": message_type == "subscribe",
                        },
                    }
                )
            except BaseException as error:
                await self._send_error(request_id, error)
        else:
            raise ProtocolError(f"unsupported IPC message {message_type!r}")

    async def _send_error(self, request_id: int, error: BaseException) -> None:
        await self.send(
            {
                "type": "error",
                "id": request_id,
                "error": wrap_error(error).to_wire(),
            }
        )

    async def send(self, message: dict[str, Any]) -> None:
        if self._closed:
            return
        async with self._write_lock:
            await write_message(
                self.writer,
                message,
                self.server.runtime.config.maximum_ipc_message,
            )

    def enqueue(self, event: StationEvent) -> None:
        if event.source_key not in self.subscriptions or self._closed:
            return
        try:
            self._events.put_nowait(event)
        except asyncio.QueueFull:
            asyncio.create_task(self.close(slow=True))

    async def _event_writer(self) -> None:
        while True:
            event = await self._events.get()
            if event is None:
                return
            if event.source_kind == "server" and event.source_key != "station.server":
                await self.send(
                    {
                        "type": "subscription_state",
                        "source": event.source_key,
                        **event.value,
                    }
                )
            else:
                await self.send({"type": "event", "event": event.to_wire()})

    async def close(self, *, slow: bool = False) -> None:
        if self._closed:
            return
        self._closed = True
        await self.server.runtime.inputs.close_owner(self)
        self.server.clients.discard(self)
        if self._handshaken:
            LOGGER.info(
                "Client disconnected: %s%s",
                self.name,
                " (slow consumer)" if slow else "",
            )
        if slow:
            with suppress(BaseException):
                async with self._write_lock:
                    await write_message(
                        self.writer,
                        {
                            "type": "error",
                            "error": StationError(
                                "slow_consumer",
                                "client could not consume Station events in time",
                            ).to_wire(),
                        },
                        self.server.runtime.config.maximum_ipc_message,
                    )
        task, self._event_task = self._event_task, None
        if task is not None and task is not asyncio.current_task():
            task.cancel()
            with suppress(asyncio.CancelledError):
                await task
        self.writer.close()
        with suppress(OSError):
            await self.writer.wait_closed()


class UnixSocketServer:
    def __init__(self, runtime: StationHost):
        self.runtime = runtime
        self.path = runtime.config.socket_path
        self.clients: set[ClientConnection] = set()
        self._server: asyncio.AbstractServer | None = None
        self._connection_tasks: set[asyncio.Task[None]] = set()
        self._owns_path = False

    async def start(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        await self._prepare_path()
        self._server = await asyncio.start_unix_server(self._accept, path=self.path)
        self._owns_path = True
        os.chmod(self.path, stat.S_IRUSR | stat.S_IWUSR)
        LOGGER.info("Listening on %s", self.path)

    async def _prepare_path(self) -> None:
        if not self.path.exists():
            return
        try:
            reader, writer = await asyncio.open_unix_connection(self.path)
        except OSError:
            info = self.path.lstat()
            if info.st_uid != os.getuid() or not stat.S_ISSOCK(info.st_mode):
                raise StationError(
                    "server_unavailable",
                    f"refusing to remove unowned or non-socket path {self.path}",
                ) from None
            self.path.unlink()
            return
        writer.close()
        await writer.wait_closed()
        del reader
        raise StationError(
            "server_already_running",
            f"a Station server is already listening at {self.path}",
        )

    def _accept(
        self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter
    ) -> None:
        client = ClientConnection(self, reader, writer)
        self.clients.add(client)
        task = asyncio.create_task(client.run(), name="station-ipc-client")
        self._connection_tasks.add(task)
        task.add_done_callback(self._connection_tasks.discard)

    async def broadcast(self, event: StationEvent) -> None:
        for client in tuple(self.clients):
            client.enqueue(event)

    async def close(self) -> None:
        server, self._server = self._server, None
        if server is not None:
            server.close()
        await asyncio.gather(
            *(client.close() for client in tuple(self.clients)),
            return_exceptions=True,
        )
        if server is not None:
            await server.wait_closed()
        for task in tuple(self._connection_tasks):
            task.cancel()
        if self._connection_tasks:
            await asyncio.gather(*self._connection_tasks, return_exceptions=True)
        self._connection_tasks.clear()
        if self._owns_path and self.path.exists():
            info = self.path.lstat()
            if info.st_uid == os.getuid() and stat.S_ISSOCK(info.st_mode):
                self.path.unlink()
        self._owns_path = False
