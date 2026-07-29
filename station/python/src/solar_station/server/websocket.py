"""Binary-CBOR WebSocket listener for browser and network Station clients."""

from __future__ import annotations

import asyncio
import logging
from contextlib import suppress
from typing import TYPE_CHECKING, Any

from websockets.asyncio.server import Server, ServerConnection, serve
from websockets.exceptions import ConnectionClosed

from ..errors import ProtocolError, StationError, wrap_error
from ..models import StationEvent
from ..protocol import IPC_VERSION, decode_message, encode_body

if TYPE_CHECKING:
    from .runtime import StationHost

LOGGER = logging.getLogger("solar_station.websocket")


class WebSocketClient:
    def __init__(self, server: WebSocketServer, socket: ServerConnection):
        self.server = server
        self.socket = socket
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
            hello = await self._receive()
            if hello.get("type") != "hello":
                raise ProtocolError("first Station WebSocket message must be hello")
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
                self._event_writer(), name=f"station-ws-events:{self.name}"
            )
            async for frame in self.socket:
                await self._handle(self._decode_frame(frame))
        except ConnectionClosed:
            pass
        except BaseException as error:
            if not isinstance(error, asyncio.CancelledError):
                with suppress(BaseException):
                    await self.send(
                        {"type": "error", "error": wrap_error(error).to_wire()}
                    )
        finally:
            await self.close()

    async def _receive(self) -> dict[str, Any]:
        return self._decode_frame(await self.socket.recv())

    def _decode_frame(self, frame: str | bytes) -> dict[str, Any]:
        if isinstance(frame, str):
            raise ProtocolError("Station WebSocket messages must be binary CBOR")
        return decode_message(frame, self.server.runtime.config.maximum_ipc_message)

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
            return

        if message_type in ("subscribe", "unsubscribe"):
            request_id = message.get("id")
            source = message.get("source")
            if not isinstance(request_id, int) or not isinstance(source, str):
                raise ProtocolError("malformed subscription request")
            try:
                canonical = self.server.runtime.modules.validate_subscription(source)
                if canonical is None:
                    canonical = self.server.runtime.sources.validate_subscription(
                        source
                    )
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
            return

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
            await self.socket.send(
                encode_body(message, self.server.runtime.config.maximum_ipc_message)
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
                await self.socket.send(
                    encode_body(
                        {
                            "type": "error",
                            "error": StationError(
                                "slow_consumer",
                                "client could not consume Station events in time",
                            ).to_wire(),
                        },
                        self.server.runtime.config.maximum_ipc_message,
                    )
                )
        task, self._event_task = self._event_task, None
        if task is not None and task is not asyncio.current_task():
            task.cancel()
            with suppress(asyncio.CancelledError):
                await task
        with suppress(BaseException):
            await self.socket.close()


class WebSocketServer:
    def __init__(self, runtime: StationHost):
        self.runtime = runtime
        self.clients: set[WebSocketClient] = set()
        self._server: Server | None = None
        self._connection_tasks: set[asyncio.Task[None]] = set()

    async def start(self) -> None:
        if self.runtime.config.websocket_host is None:
            LOGGER.info("Listener disabled")
            return
        self._server = await serve(
            self._accept,
            self.runtime.config.websocket_host,
            self.runtime.config.websocket_port,
            max_size=self.runtime.config.maximum_ipc_message,
            origins=None,
        )
        LOGGER.info(
            "Listening on ws://%s:%d/station",
            self.runtime.config.websocket_host,
            self.runtime.config.websocket_port,
        )

    async def _accept(self, socket: ServerConnection) -> None:
        if socket.request.path != "/station":
            await socket.close(1008, "Station WebSocket endpoint is /station")
            return
        client = WebSocketClient(self, socket)
        self.clients.add(client)
        task = asyncio.current_task()
        if task is not None:
            self._connection_tasks.add(task)
        try:
            await client.run()
        finally:
            if task is not None:
                self._connection_tasks.discard(task)

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
            if task is not asyncio.current_task():
                task.cancel()
        self._connection_tasks.clear()
