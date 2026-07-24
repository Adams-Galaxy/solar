"""Async connection ownership, correlation, discovery, and dynamic operations."""

from __future__ import annotations

import asyncio
from contextlib import AbstractAsyncContextManager
import hashlib
from pathlib import Path
from typing import Any

from .client import Client, Message
from .codec import DynamicCodec
from .manifest import Manifest, parse_manifest
from .protocol import (
    FLAG_ERROR_PAYLOAD,
    KIND_DATA,
    KIND_ERROR,
    KIND_INTROSPECTION,
    KIND_RESPONSE,
    OPERATION_ACTION,
    OPERATION_QUERY,
    OPERATION_UPDATE,
    SUBSCRIPTION_DATA_STREAM,
    SUBSCRIPTION_DATA_WATCH,
    SUBSCRIPTION_STREAM,
    SUBSCRIPTION_TOPIC,
    ManifestChunk,
    ServerInformation,
    SubscriptionRequest,
    decode_batch,
)
from .transports.base import AsyncTransport


class SessionClosed(ConnectionError):
    pass


class RemoteError(RuntimeError):
    def __init__(self, code: int):
        super().__init__(f"Remote request failed with protocol error {code}")
        self.code = code


class ActionError(RuntimeError):
    def __init__(self, value: Any):
        super().__init__(f"Remote action returned a domain error: {value!r}")
        self.value = value


class Subscription:
    def __init__(
        self,
        session: "AsyncSession",
        target: int,
        kind: int,
        schema: int,
        *,
        queue_depth: int = 32,
        batch: int = 1,
        batched: bool = False,
    ):
        self.session = session
        self.target = target
        self.kind = kind
        self.schema = schema
        self.loss_count = 0
        self.batch = batch
        self.batched = batched
        self._queue: asyncio.Queue[Any] = asyncio.Queue(queue_depth)
        self._closed = False

    def _deliver(self, payload: bytes) -> None:
        payloads = decode_batch(payload)[1] if self.batched else [payload]
        for item in payloads:
            if self._queue.full():
                self._queue.get_nowait()
                self.loss_count += 1
            value = self.session.codec.decode(self.schema, item)  # type: ignore[union-attr]
            self._queue.put_nowait(value)

    def __aiter__(self) -> "Subscription":
        return self

    async def __anext__(self) -> Any:
        if self._closed:
            raise StopAsyncIteration
        return await self._queue.get()

    async def aclose(self) -> None:
        if self._closed:
            return
        self._closed = True
        self.session._subscriptions.pop((self.target, self.kind), None)
        await self.session._exchange(
            self.session.core.unsubscribe(self.target, self.kind)
        )


class AsyncSession(AbstractAsyncContextManager["AsyncSession"]):
    def __init__(
        self,
        transport: AsyncTransport,
        *,
        manifest: Manifest | None = None,
        cache: Path | None = None,
        timeout: float = 5.0,
    ):
        self.transport = transport
        self.core = Client()
        self.manifest = manifest
        self.cache = cache
        self.timeout = timeout
        self.codec = DynamicCodec(manifest) if manifest else None
        self.server_information: ServerInformation | None = None
        self._reader: asyncio.Task[None] | None = None
        self._active = asyncio.Event()
        self._pending: dict[int, asyncio.Future[Message]] = {}
        self._subscriptions: dict[tuple[int, int], Subscription] = {}
        self._write_lock = asyncio.Lock()
        self._closed = False

    async def __aenter__(self) -> "AsyncSession":
        await self.open()
        return self

    async def __aexit__(self, *_: object) -> None:
        await self.close()

    async def open(self) -> None:
        await self.transport.open()
        try:
            self._reader = asyncio.create_task(
                self._read_loop(), name="solar-remote-reader"
            )
            await asyncio.wait_for(self._active.wait(), self.timeout)
            await self.discover()
        except BaseException:
            await self.close()
            raise

    async def _flush(self) -> None:
        async with self._write_lock:
            while (frame := self.core.take_outgoing()) is not None:
                await self.transport.write(frame)

    async def _read_loop(self) -> None:
        failure: BaseException = SessionClosed("Remote transport closed")
        try:
            while True:
                incoming = await self.transport.read(4096)
                if not incoming:
                    break
                for message in self.core.feed(incoming):
                    if self.core.active:
                        self._active.set()
                    request = message.envelope.request_id
                    if message.envelope.kind == KIND_DATA:
                        key = (message.envelope.target, message.envelope.subscription)
                        subscription = self._subscriptions.get(key)
                        if subscription is not None:
                            subscription._deliver(message.payload)
                    elif request and request in self._pending:
                        self._pending.pop(request).set_result(message)
                await self._flush()
        except BaseException as error:
            failure = error
        finally:
            self._closed = True
            for future in self._pending.values():
                if not future.done():
                    future.set_exception(failure)
            self._pending.clear()
            self._active.set()

    async def _exchange(self, request: int) -> Message:
        loop = asyncio.get_running_loop()
        future = loop.create_future()
        self._pending[request] = future
        await self._flush()
        try:
            message = await asyncio.wait_for(future, self.timeout)
        except BaseException:
            self._pending.pop(request, None)
            self.core.cancel(request)
            await self._flush()
            raise
        if message.envelope.kind == KIND_ERROR:
            code = (
                int.from_bytes(message.payload[:2], "little")
                if len(message.payload) >= 2
                else 0
            )
            raise RemoteError(code)
        if message.envelope.kind == KIND_RESPONSE:
            self.core.acknowledge(request)
            await self._flush()
        return message

    async def discover(self) -> ServerInformation:
        message = await self._exchange(self.core.request_server_information())
        if message.envelope.kind != KIND_INTROSPECTION:
            raise SessionClosed("expected server-information response")
        information = ServerInformation.decode(message.payload)
        self.server_information = information
        if self.manifest and self.manifest.digest != information.manifest_digest:
            raise ValueError("explicit manifest does not match connected firmware")
        if self.manifest is None and self.cache is not None:
            path = self.cache / f"{information.manifest_digest.hex()}.slrm"
            if path.exists():
                candidate = parse_manifest(path.read_bytes())
                if candidate.digest == information.manifest_digest:
                    self.manifest = candidate
        if self.manifest is None and information.feature_flags & 1:
            self.manifest = await self.fetch_manifest()
            if self.cache is not None:
                self.cache.mkdir(parents=True, exist_ok=True)
                (self.cache / f"{self.manifest.digest.hex()}.slrm").write_bytes(
                    self.manifest.image
                )
        if self.manifest is not None:
            self.codec = DynamicCodec(self.manifest)
        return information

    async def fetch_manifest(self, chunk_size: int = 1024) -> Manifest:
        if self.server_information is None:
            raise RuntimeError("server information is unavailable")
        output = bytearray()
        while len(output) < self.server_information.manifest_size:
            limit = min(chunk_size, self.server_information.manifest_size - len(output))
            message = await self._exchange(
                self.core.request_manifest_chunk(len(output), limit)
            )
            chunk = ManifestChunk.decode(message.payload)
            if (
                chunk.offset != len(output)
                or chunk.total != self.server_information.manifest_size
            ):
                raise ValueError("non-contiguous manifest response")
            output.extend(chunk.data)
        if hashlib.sha256(output).digest() != self.server_information.manifest_digest:
            raise ValueError("retrieved manifest digest mismatch")
        return parse_manifest(bytes(output))

    def _endpoint(self, collection: str, endpoint: int | str) -> dict[str, Any]:
        if self.manifest is None:
            raise RuntimeError("no matching Remote manifest is available")
        values = getattr(self.manifest, collection)
        for value in values:
            if endpoint == value["id"] or endpoint == value["name"]:
                return value
        raise KeyError(endpoint)

    async def action(self, endpoint: int | str, value: Any = None) -> Any:
        item = self._endpoint("actions", endpoint)
        payload = self.codec.encode(item["request_schema"], value or {})  # type: ignore[union-attr]
        message = await self._exchange(
            self.core.request(item["id"], payload, OPERATION_ACTION)
        )
        if message.envelope.flags & FLAG_ERROR_PAYLOAD:
            raise ActionError(
                self.codec.decode(item["error_schema"], message.payload)  # type: ignore[union-attr]
            )
        return self.codec.decode(item["response_schema"], message.payload)  # type: ignore[union-attr]

    async def query(self, endpoint: int | str) -> Any:
        item = self._endpoint("data", endpoint)
        message = await self._exchange(
            self.core.request(item["id"], b"", OPERATION_QUERY)
        )
        return self.codec.decode(item["schema"], message.payload)  # type: ignore[union-attr]

    async def update(self, endpoint: int | str, value: Any) -> Any:
        item = self._endpoint("data", endpoint)
        payload = self.codec.encode(item["schema"], value)  # type: ignore[union-attr]
        message = await self._exchange(
            self.core.request(item["id"], payload, OPERATION_UPDATE)
        )
        return message.payload

    async def _subscribe(
        self,
        item: dict[str, Any],
        kind: int,
        *,
        frequency: float | None,
        batch: int,
        queue_depth: int,
    ) -> Subscription:
        if frequency is not None and frequency <= 0:
            raise ValueError("frequency must be positive")
        interval = 0 if frequency is None else max(1, round(1_000_000 / frequency))
        policy = SubscriptionRequest(
            minimum_interval_us=interval, batch_size=batch, codec=0, flags=0
        )
        message = await self._exchange(self.core.subscribe(item["id"], kind, policy))
        if message.envelope.kind != KIND_RESPONSE:
            raise SessionClosed("expected subscription response")
        capability = next(
            (
                value
                for value in self.manifest.capabilities  # type: ignore[union-attr]
                if value["endpoint"] == item["id"]
                and value["domain"]
                == (
                    "data"
                    if kind in (SUBSCRIPTION_DATA_STREAM, SUBSCRIPTION_DATA_WATCH)
                    else "topic"
                    if kind == SUBSCRIPTION_TOPIC
                    else "stream"
                )
                and (
                    value["kind"] == "out_stream"
                    if kind == SUBSCRIPTION_DATA_STREAM
                    else True
                )
            ),
            {},
        )
        subscription = Subscription(
            self,
            item["id"],
            kind,
            item["schema"],
            queue_depth=queue_depth,
            batch=batch,
            batched=bool(capability.get("batched")),
        )
        self._subscriptions[(item["id"], kind)] = subscription
        return subscription

    async def watch(
        self,
        endpoint: int | str,
        *,
        frequency: float | None = None,
        queue_depth: int = 32,
    ) -> Subscription:
        return await self._subscribe(
            self._endpoint("data", endpoint),
            SUBSCRIPTION_DATA_WATCH,
            frequency=frequency,
            batch=1,
            queue_depth=queue_depth,
        )

    async def stream(
        self,
        endpoint: int | str,
        *,
        frequency: float | None = None,
        batch: int = 1,
        queue_depth: int = 32,
    ) -> Subscription:
        try:
            item = self._endpoint("streams", endpoint)
            kind = SUBSCRIPTION_STREAM
        except KeyError:
            item = self._endpoint("data", endpoint)
            kind = SUBSCRIPTION_DATA_STREAM
        return await self._subscribe(
            item, kind, frequency=frequency, batch=batch, queue_depth=queue_depth
        )

    async def topic(
        self,
        endpoint: int | str,
        *,
        queue_depth: int = 32,
    ) -> Subscription:
        return await self._subscribe(
            self._endpoint("topics", endpoint),
            SUBSCRIPTION_TOPIC,
            frequency=None,
            batch=1,
            queue_depth=queue_depth,
        )

    async def close(self) -> None:
        for subscription in self._subscriptions.values():
            subscription._closed = True
        self._subscriptions.clear()
        if self._reader is not None:
            self._reader.cancel()
            try:
                await self._reader
            except asyncio.CancelledError:
                pass
            self._reader = None
        await self.transport.close()
