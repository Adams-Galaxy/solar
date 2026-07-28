"""Async client for a Solar Station host."""

from __future__ import annotations

import asyncio
import itertools
import time
from collections.abc import AsyncIterator
from contextlib import AbstractAsyncContextManager
from pathlib import Path
from typing import Any

from .config import default_socket_path
from .errors import ProtocolError, StationError
from .models import PingResult, normalize
from .protocol import IPC_VERSION, read_message, write_message
from .resources import (
    ConnectionResource,
    InputCollection,
    LogsResource,
    RecordingsResource,
    RobotResource,
    SourceCollection,
    manifest_runtime,
)


class _EventQueue(asyncio.Queue[dict[str, Any] | BaseException | None]):
    def __init__(self, maximum: int):
        super().__init__(maximum)
        self.loss_count = 0

    def deliver(self, value: dict[str, Any]) -> None:
        if self.full():
            self.get_nowait()
            self.loss_count += 1
        self.put_nowait(value)


class EventSubscription(AbstractAsyncContextManager["EventSubscription"]):
    def __init__(
        self,
        client: StationClient,
        source: str,
        queue: _EventQueue,
    ):
        self.client = client
        self.source = source
        self.queue = queue
        self._closed = False

    @property
    def loss_count(self) -> int:
        return self.queue.loss_count

    @property
    def queue_capacity(self) -> int:
        return self.queue.maxsize

    @property
    def queue_policy(self) -> str:
        return "drop_oldest"

    def __aiter__(self) -> AsyncIterator[dict[str, Any]]:
        return self

    async def __anext__(self) -> dict[str, Any]:
        if self._closed:
            raise StopAsyncIteration
        value = await self.queue.get()
        if value is None:
            self._closed = True
            raise StopAsyncIteration
        if isinstance(value, BaseException):
            self._closed = True
            raise value
        return value

    async def __aenter__(self) -> EventSubscription:
        return self

    async def __aexit__(self, *_: object) -> None:
        await self.close()

    async def close(self) -> None:
        self._closed = True
        await self.client.unsubscribe(self.source, self.queue)


class StationClient(AbstractAsyncContextManager["StationClient"]):
    def __init__(
        self,
        socket_path: Path | None = None,
        *,
        name: str = "station-client",
        timeout: float = 5.0,
        maximum_message: int = 1 << 20,
    ):
        self.socket_path = socket_path or default_socket_path()
        self.name = name
        self.timeout = timeout
        self.maximum_message = maximum_message
        self.server_information: dict[str, Any] | None = None
        self.manifest: dict[str, Any] | None = None
        self.robot_status: dict[str, Any] = {"state": "disconnected"}
        self.catalog: Any = None
        self.models: Any = None
        self.robot = RobotResource(self)
        self.sources = SourceCollection(self)
        self.inputs = InputCollection(self)
        self.logs = LogsResource(self)
        self.recordings = RecordingsResource(self)
        self.connection = ConnectionResource(self)
        self._reader: asyncio.StreamReader | None = None
        self._writer: asyncio.StreamWriter | None = None
        self._reader_task: asyncio.Task[None] | None = None
        self._write_lock = asyncio.Lock()
        self._ids = itertools.count(1)
        self._pending: dict[int, asyncio.Future[Any]] = {}
        self._subscriptions: dict[str, set[_EventQueue]] = {}
        self._manifest_subscription: EventSubscription | None = None
        self._manifest_task: asyncio.Task[None] | None = None
        self._closed = True

    @property
    def connected(self) -> bool:
        return self._writer is not None and not self._writer.is_closing()

    @property
    def active_recordings(self) -> set[str]:
        """Names of currently active recordings."""
        return self.recordings.active

    async def __aenter__(self) -> StationClient:
        await self.connect()
        return self

    async def __aexit__(self, *_: object) -> None:
        await self.close()

    async def connect(self) -> None:
        if self.connected:
            return
        try:
            reader, writer = await asyncio.open_unix_connection(self.socket_path)
        except OSError as error:
            raise StationError(
                "server_unavailable",
                f"Station server is unavailable at {self.socket_path}: {error}",
            ) from error
        self._reader = reader
        self._writer = writer
        self._closed = False
        try:
            await self._send(
                {"type": "hello", "version": IPC_VERSION, "client": self.name}
            )
            response = await asyncio.wait_for(
                read_message(reader, self.maximum_message), self.timeout
            )
            if response.get("type") == "error":
                raise StationError.from_wire(response.get("error"))
            if (
                response.get("type") != "hello_response"
                or response.get("version") != IPC_VERSION
            ):
                raise ProtocolError("Station returned an invalid handshake")
            self.server_information = response
            self.robot_status["state"] = str(
                response.get("robot_state", "disconnected")
            )
            if response.get("manifest_digest") is not None:
                self.robot_status["manifest_digest"] = response["manifest_digest"]
            self._reader_task = asyncio.create_task(
                self._read_loop(), name=f"station-client-reader:{self.name}"
            )
            if self.robot_status["state"] == "online":
                await self.get_manifest()
            for source in tuple(self._subscriptions):
                await self._exchange({"type": "subscribe", "source": source})
        except BaseException:
            await self.close()
            raise

    async def ensure_connected(self) -> None:
        if not self.connected:
            await self.connect()

    async def _send(self, message: dict[str, Any]) -> None:
        if self._writer is None:
            raise StationError("server_unavailable", "Station client is not connected")
        async with self._write_lock:
            await write_message(self._writer, message, self.maximum_message)

    async def _read_loop(self) -> None:
        failure: BaseException = StationError(
            "server_unavailable", "Station server disconnected"
        )
        try:
            assert self._reader is not None
            while True:
                message = await read_message(self._reader, self.maximum_message)
                message_type = message.get("type")
                if message_type in ("response", "error"):
                    request_id = message.get("id")
                    if not isinstance(request_id, int):
                        raise ProtocolError("response has no integer request ID")
                    future = self._pending.pop(request_id, None)
                    if future is not None and not future.done():
                        if message_type == "error":
                            future.set_exception(
                                StationError.from_wire(message.get("error"))
                            )
                        else:
                            future.set_result(message.get("result"))
                elif message_type == "event":
                    event = message.get("event")
                    if not isinstance(event, dict) or not isinstance(
                        event.get("source"), str
                    ):
                        raise ProtocolError("malformed Station event")
                    self._observe_event(event)
                    for queue in tuple(self._subscriptions.get(event["source"], ())):
                        queue.deliver(event)
                elif message_type in ("subscription_state", "server_state"):
                    source = message.get("source", "station.server")
                    event = {
                        "source": source,
                        "source_kind": "server",
                        "value": message,
                    }
                    for queue in tuple(self._subscriptions.get(source, ())):
                        queue.deliver(event)
                else:
                    raise ProtocolError(f"unsupported Station message {message_type!r}")
        except asyncio.CancelledError:
            failure = StationError("client_closed", "Station client closed")
            raise
        except (asyncio.IncompleteReadError, ConnectionError, OSError) as error:
            failure = StationError(
                "server_unavailable",
                "Station server disconnected",
                {"type": type(error).__name__},
            )
        except BaseException as error:
            failure = error
        finally:
            self._closed = True
            writer, self._writer = self._writer, None
            self._reader = None
            if writer is not None:
                writer.close()
            for future in self._pending.values():
                if not future.done():
                    future.set_exception(failure)
            self._pending.clear()
            for queues in self._subscriptions.values():
                for queue in queues:
                    if not queue.full():
                        queue.put_nowait(failure)

    async def request(self, operation: str, **arguments: Any) -> Any:
        return await self.request_with_timeout(
            operation, request_timeout=self.timeout, **arguments
        )

    async def request_with_timeout(
        self, operation: str, *, request_timeout: float, **arguments: Any
    ) -> Any:
        result = await self._exchange(
            {
                "type": "request",
                "operation": operation,
                "arguments": arguments,
            },
            wait_seconds=request_timeout,
        )
        if operation == "manifest" and isinstance(result, dict):
            self.manifest = result
            self.catalog, self.models = manifest_runtime(result)
        elif operation == "status" and isinstance(result, dict):
            robot = result.get("robot")
            if isinstance(robot, dict):
                self.robot_status.update(robot)
            sources = result.get("sources")
            if isinstance(sources, list):
                self.sources.replace(_source_names(sources))
        elif operation == "streams" and isinstance(result, list):
            self.sources.replace(_source_names(result))
        elif operation == "stream" and isinstance(result, dict):
            endpoint_name = result.get("endpoint_name")
            if isinstance(endpoint_name, str):
                self.sources.configured.add(endpoint_name)
        elif operation == "record_list" and isinstance(result, list):
            self.recordings.known = {
                str(item["name"])
                for item in result
                if isinstance(item, dict) and isinstance(item.get("name"), str)
            }
            self.recordings.active = {
                str(item["name"])
                for item in result
                if isinstance(item, dict)
                and isinstance(item.get("name"), str)
                and item.get("active") is True
            }
        elif operation == "record_start" and isinstance(result, dict):
            name = result.get("name")
            if isinstance(name, str):
                self.recordings.known.add(name)
                self.recordings.active.add(name)
        elif operation == "record_stop" and isinstance(result, dict):
            name = result.get("name")
            if isinstance(name, str):
                self.recordings.active.discard(name)
        elif operation == "input.open" and isinstance(result, dict):
            handle = result.get("handle")
            if isinstance(handle, int):
                self.inputs.active[handle] = result
        elif operation == "input.close" and isinstance(result, dict):
            handle = result.get("handle")
            if isinstance(handle, int):
                self.inputs.active.pop(handle, None)
        elif operation == "input.list" and isinstance(result, list):
            self.inputs.active = {
                item["handle"]: item
                for item in result
                if isinstance(item, dict) and isinstance(item.get("handle"), int)
            }
        return result

    async def status(self) -> dict[str, Any]:
        return await self.request("status")

    async def get_manifest(self) -> dict[str, Any]:
        return await self.request("manifest")

    async def get(self, endpoint: int | str) -> Any:
        return await self.request("get", endpoint=endpoint)

    async def set(self, endpoint: int | str, value: Any) -> dict[str, Any]:
        return await self.request("set", endpoint=endpoint, value=value)

    async def call(self, endpoint: int | str, value: Any = None) -> Any:
        return await self.request("call", endpoint=endpoint, value=value)

    async def configure_stream(
        self,
        endpoint: int | str,
        *,
        frequency: float | None = None,
        batch: int = 1,
        stop: bool = False,
    ) -> dict[str, Any]:
        return await self.request(
            "stream",
            endpoint=endpoint,
            frequency=frequency,
            batch=batch,
            stop=stop,
        )

    async def list_streams(self) -> list[dict[str, Any]]:
        return await self.request("streams")

    async def open_input(
        self,
        endpoint: int | str,
        *,
        frequency: float | None = None,
        credit_timeout: float | None = None,
    ) -> dict[str, Any]:
        return await self.request(
            "input.open",
            endpoint=endpoint,
            frequency=frequency,
            credit_timeout=credit_timeout,
        )

    async def send_input(
        self,
        handle: int,
        value: Any,
        *,
        timeout: float | None = None,  # noqa: ASYNC109
    ) -> dict[str, Any]:
        return await self.request(
            "input.send", handle=handle, value=value, timeout=timeout
        )

    async def close_input(self, handle: int) -> dict[str, Any]:
        return await self.request("input.close", handle=handle)

    async def list_inputs(self) -> list[dict[str, Any]]:
        return await self.request("input.list")

    async def list_modules(self) -> list[dict[str, Any]]:
        return await self.request("module.list")

    async def enable_module(self, name: str) -> dict[str, Any]:
        return await self.request("module.enable", name=name)

    async def disable_module(self, name: str) -> dict[str, Any]:
        return await self.request("module.disable", name=name)

    async def module_request(
        self, module: str, method: str, **arguments: Any
    ) -> Any:
        return await self.request(
            "module.request",
            name=module,
            method=method,
            arguments=arguments,
        )

    async def reconnect(
        self, *, timeout: float = 10.0  # noqa: ASYNC109
    ) -> dict[str, Any]:
        return await self.request_with_timeout(
            "reconnect",
            request_timeout=max(timeout + 1.0, self.timeout),
            timeout=timeout,
        )

    async def ping(self) -> PingResult:
        started = time.monotonic_ns()
        result = await self.request("ping")
        ended = time.monotonic_ns()
        return PingResult(
            round_trip_ns=int(result["round_trip_ns"]),
            remote_processing_ns=int(result["remote_processing_ns"]),
            station_round_trip_ns=ended - started,
        )

    async def query_logs(
        self, *, head: int | None = None, tail: int | None = None
    ) -> list[dict[str, Any]]:
        return await self.request("logs", head=head, tail=tail)

    async def start_recording(self, name: str) -> dict[str, Any]:
        return await self.request("record_start", name=name)

    async def stop_recording(self, name: str | None = None) -> dict[str, Any]:
        return await self.request("record_stop", name=name)

    async def list_recordings(self) -> list[dict[str, Any]]:
        return await self.request("record_list")

    async def show_recording(self, name: str) -> dict[str, Any]:
        return await self.request("record_show", name=name)

    async def export_recording(
        self,
        name: str,
        *,
        format: str = "jsonl",
        output: str | None = None,
    ) -> dict[str, Any]:
        return await self.request(
            "record_export", name=name, format=format, output=output
        )

    async def shutdown_host(self) -> dict[str, Any]:
        return await self.request("shutdown")

    async def watch(
        self, source: str, *, queue_depth: int = 128
    ) -> EventSubscription:
        return await self.subscribe(source, queue_depth=queue_depth)

    async def track_manifest(self) -> None:
        """Populate and refresh the synchronous completion catalog."""
        await self.ensure_connected()
        await self.request("status")
        await self.request("record_list")
        try:
            await self.request("manifest")
        except StationError as error:
            if error.code != "robot_offline":
                raise
        if self._manifest_task is not None:
            return
        self._manifest_subscription = await self.subscribe("station.server")
        self._manifest_task = asyncio.create_task(
            self._track_manifest(), name=f"station-manifest:{self.name}"
        )

    async def _track_manifest(self) -> None:
        while True:
            subscription = self._manifest_subscription
            try:
                if subscription is None or subscription._closed:
                    subscription = await self.subscribe("station.server")
                    self._manifest_subscription = subscription
                    try:
                        await self.request("manifest")
                    except StationError as error:
                        if error.code != "robot_offline":
                            raise
                async for event in subscription:
                    value = event.get("value", {})
                    if not isinstance(value, dict) or value.get("state") != "online":
                        continue
                    digest = value.get("manifest_digest")
                    current = (
                        self.manifest.get("manifest_sha256") if self.manifest else None
                    )
                    digest_text = digest.hex() if isinstance(digest, bytes) else digest
                    if current != digest_text:
                        await self.request("manifest")
            except asyncio.CancelledError:
                raise
            except Exception:
                if subscription is not None:
                    await self.unsubscribe(subscription.source, subscription.queue)
                self._manifest_subscription = None
                await asyncio.sleep(0.25)

    def _observe_event(self, event: dict[str, Any]) -> None:
        if event.get("source") != "station.server":
            return
        value = event.get("value")
        if not isinstance(value, dict):
            return
        state = value.get("state")
        robot_states = {
            "connecting",
            "resolving_manifest",
            "restoring_sources",
            "online",
            "disconnected",
        }
        if state in robot_states:
            self.robot_status["state"] = state
        elif state == "recording_started" and isinstance(value.get("name"), str):
            self.recordings.known.add(value["name"])
            self.recordings.active.add(value["name"])
        elif state == "recording_stopped" and isinstance(value.get("name"), str):
            self.recordings.known.add(value["name"])
            self.recordings.active.discard(value["name"])
        elif state == "input_closed" and isinstance(value.get("handle"), int):
            self.inputs.active.pop(value["handle"], None)
        for key in ("target", "build_id", "manifest_digest", "reason"):
            if key in value:
                self.robot_status[key] = value[key]

    async def _exchange(
        self, message: dict[str, Any], *, wait_seconds: float | None = None
    ) -> Any:
        await self.ensure_connected()
        request_id = next(self._ids)
        future = asyncio.get_running_loop().create_future()
        self._pending[request_id] = future
        try:
            await self._send(normalize({**message, "id": request_id}))
            return await asyncio.wait_for(
                future, self.timeout if wait_seconds is None else wait_seconds
            )
        except BaseException:
            self._pending.pop(request_id, None)
            raise

    async def subscribe(
        self, source: str, *, queue_depth: int = 128
    ) -> EventSubscription:
        await self.ensure_connected()
        queue = _EventQueue(queue_depth)
        queues = self._subscriptions.setdefault(source, set())
        first = not queues
        queues.add(queue)
        try:
            if first:
                result = await self._exchange({"type": "subscribe", "source": source})
                canonical = result["source"]
                if canonical != source:
                    self._subscriptions.pop(source, None)
                    self._subscriptions.setdefault(canonical, set()).add(queue)
                    source = canonical
        except BaseException:
            queues.discard(queue)
            if not queues:
                self._subscriptions.pop(source, None)
            raise
        return EventSubscription(self, source, queue)

    async def unsubscribe(
        self,
        source: str,
        queue: _EventQueue,
    ) -> None:
        queues = self._subscriptions.get(source)
        if queues is None:
            return
        queues.discard(queue)
        if not queues:
            self._subscriptions.pop(source, None)
            if self.connected:
                await self._exchange({"type": "unsubscribe", "source": source})
        if not queue.full():
            queue.put_nowait(None)

    async def close(self) -> None:
        manifest_task, self._manifest_task = self._manifest_task, None
        if manifest_task is not None:
            manifest_task.cancel()
            try:
                await manifest_task
            except asyncio.CancelledError:
                pass
        manifest_subscription, self._manifest_subscription = (
            self._manifest_subscription,
            None,
        )
        if manifest_subscription is not None:
            await manifest_subscription.close()
        task, self._reader_task = self._reader_task, None
        if task is not None:
            task.cancel()
            try:
                await task
            except asyncio.CancelledError:
                pass
        writer, self._writer = self._writer, None
        self._reader = None
        if writer is not None:
            writer.close()
            try:
                await writer.wait_closed()
            except OSError:
                pass
        self._closed = True
        self.server_information = None

    def _require_catalog(self) -> Any:
        if self.catalog is None:
            raise StationError(
                "manifest_unavailable",
                "load the active manifest before accessing typed robot resources",
            )
        return self.catalog

    def _model_value(self, schema_id: int, value: Any) -> Any:
        if self.models is None:
            raise StationError(
                "manifest_unavailable", "manifest models are unavailable"
            )
        if hasattr(value, "__solar_schema_id__"):
            return value
        return self.models.construct(schema_id, value)


def _source_names(values: list[Any]) -> set[str]:
    return {
        str(item["endpoint_name"])
        for item in values
        if isinstance(item, dict) and isinstance(item.get("endpoint_name"), str)
    }
