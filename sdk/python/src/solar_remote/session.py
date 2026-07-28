"""Async connection ownership, correlation, discovery, and dynamic operations."""

from __future__ import annotations

import asyncio
from contextlib import AbstractAsyncContextManager
from dataclasses import dataclass
import hashlib
from pathlib import Path
import secrets
import time
from typing import Any

from .client import Client, Message
from .codec import DynamicCodec
from .manifest import Manifest, parse_manifest
from .protocol import (
    FLAG_ERROR_PAYLOAD,
    KIND_DATA,
    KIND_ERROR,
    KIND_INTROSPECTION,
    KIND_CREDIT,
    KIND_IN_STREAM_CLOSED,
    KIND_PONG,
    KIND_RESPONSE,
    OPERATION_ACTION,
    OPERATION_QUERY,
    OPERATION_UPDATE,
    PingRequest,
    PingResponse,
    SUBSCRIPTION_DATA_IN_STREAM,
    SUBSCRIPTION_DATA_STREAM,
    SUBSCRIPTION_DATA_WATCH,
    SUBSCRIPTION_STREAM,
    SUBSCRIPTION_TOPIC,
    ManifestChunk,
    InStreamClosed,
    InStreamOpenResponse,
    ServerInformation,
    SubscriptionRequest,
    decode_batch,
)
from .channel import AsyncByteChannel


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


@dataclass(frozen=True, slots=True)
class PingResult:
    nonce: int
    round_trip_ns: int
    remote_processing_ns: int
    remote_receive_us: int
    remote_send_us: int

    @property
    def round_trip_ms(self) -> float:
        return self.round_trip_ns / 1_000_000

    @property
    def remote_processing_ms(self) -> float:
        return self.remote_processing_ns / 1_000_000


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


class InboundStream(AbstractAsyncContextManager["InboundStream"]):
    """A typed, token-scoped producer for one firmware inbound stream."""

    def __init__(
        self,
        session: "AsyncSession",
        endpoint: int | str,
        *,
        frequency: float | None = None,
        credit_timeout: float | None = None,
    ):
        self.session = session
        self.endpoint = endpoint
        self.frequency = frequency
        self.credit_timeout = credit_timeout
        self.target = 0
        self.token = 0
        self.schema = 0
        self.effective = None
        self.closure_reason: int | None = None
        self._credit = asyncio.Event()
        self._closed_event = asyncio.Event()
        self._send_lock = asyncio.Lock()
        self._opened = False
        self._closed = False

    @property
    def closed(self) -> bool:
        """Whether this producer can no longer accept samples."""
        return self._closed

    @property
    def credit_window(self) -> int | None:
        """The currently advertised firmware credit window, if active."""
        grant = self.session.core.credits.get((self.target, self.token))
        return grant.window if grant is not None else None

    async def __aenter__(self) -> "InboundStream":
        await self.open()
        return self

    async def __aexit__(self, *_: object) -> None:
        await self.aclose()

    async def open(self) -> None:
        if self._opened:
            return
        if self.frequency is not None and self.frequency <= 0:
            raise ValueError("frequency must be positive")
        hello = self.session.core.server_hello
        if hello is None or hello.minor < 1:
            raise SessionClosed(
                "firmware does not support explicit inbound-stream activation"
            )
        item = self.session._endpoint("data", self.endpoint)
        capability = next(
            (
                value
                for value in self.session.manifest.capabilities  # type: ignore[union-attr]
                if value["domain"] == "data"
                and value["endpoint"] == item["id"]
                and value["kind"] == "in_stream"
            ),
            None,
        )
        if capability is None or not capability.get("explicit_open"):
            raise ValueError(f"{self.endpoint!r} is not an explicit inbound stream")
        interval = (
            0
            if self.frequency is None
            else max(1, round(1_000_000 / self.frequency))
        )
        policy = SubscriptionRequest(
            minimum_interval_us=interval,
            batch_size=1,
            codec={"cbor": 1, "packed": 2}[capability["codec"]],
            flags=0,
        )
        message = await self.session._exchange(
            self.session.core.subscribe(
                item["id"], SUBSCRIPTION_DATA_IN_STREAM, policy
            )
        )
        if message.envelope.kind != KIND_RESPONSE:
            raise SessionClosed("expected inbound-stream open response")
        opened = InStreamOpenResponse.decode(message.payload)
        self.target = item["id"]
        self.schema = item["schema"]
        self.token = opened.token
        self.effective = opened.policy
        self._opened = True
        self.session._in_streams[(self.target, self.token)] = self
        if self.session.core.credits.get((self.target, self.token)):
            self._credit.set()

    def _notify_credit(self) -> None:
        self._credit.set()

    def _notify_closed(self, reason: int) -> None:
        self.closure_reason = reason
        self._closed = True
        self._credit.set()
        self._closed_event.set()

    async def wait_closed(self) -> int | None:
        """Wait until firmware, disconnect, or the owner closes this token."""
        await self._closed_event.wait()
        return self.closure_reason

    async def send(self, value: Any, *, timeout: float | None = None) -> None:
        if not self._opened:
            await self.open()
        async with self._send_lock:
            while True:
                if self._closed or self.session._closed:
                    raise SessionClosed(
                        f"inbound stream closed (reason={self.closure_reason})"
                    )
                grant = self.session.core.credits.get((self.target, self.token))
                if grant is not None and grant.credits:
                    payload = self.session.codec.encode(self.schema, value)  # type: ignore[union-attr]
                    self.session.core.send_stream(
                        self.target, payload, self.token
                    )
                    await self.session._flush()
                    return
                self._credit.clear()
                wait = self.credit_timeout if timeout is None else timeout
                try:
                    if wait is None:
                        await self._credit.wait()
                    else:
                        await asyncio.wait_for(self._credit.wait(), wait)
                except TimeoutError as error:
                    raise TimeoutError("timed out waiting for inbound-stream credit") from error

    async def aclose(self) -> None:
        if not self._opened or self._closed:
            return
        try:
            await self.session._exchange(
                self.session.core.close_in_stream(self.target, self.token)
            )
        finally:
            self._closed = True
            self._credit.set()
            self._closed_event.set()
            self.session._in_streams.pop((self.target, self.token), None)
            self.session.core.credits.pop((self.target, self.token), None)


class AsyncSession(AbstractAsyncContextManager["AsyncSession"]):
    def __init__(
        self,
        channel: AsyncByteChannel,
        *,
        manifest: Manifest | None = None,
        cache: Path | None = None,
        timeout: float = 5.0,
    ):
        self.channel = channel
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
        self._in_streams: dict[tuple[int, int], InboundStream] = {}
        self._write_lock = asyncio.Lock()
        self._closed = False

    async def __aenter__(self) -> "AsyncSession":
        await self.start()
        return self

    async def __aexit__(self, *_: object) -> None:
        await self.stop()

    async def start(self) -> None:
        """Start protocol negotiation on an already-open byte channel."""
        try:
            self._reader = asyncio.create_task(
                self._read_loop(), name="solar-remote-reader"
            )
            await asyncio.wait_for(self._active.wait(), self.timeout)
            await self.discover()
        except BaseException:
            await self.stop()
            raise

    async def _flush(self) -> None:
        async with self._write_lock:
            while (frame := self.core.take_outgoing()) is not None:
                await self.channel.send(frame)

    async def _read_loop(self) -> None:
        failure: BaseException = SessionClosed("Remote transport closed")
        try:
            while True:
                incoming = await self.channel.receive(4096)
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
                    elif message.envelope.kind == KIND_CREDIT:
                        token = int.from_bytes(message.payload[:4], "little")
                        stream = self._in_streams.get(
                            (message.envelope.target, token)
                        )
                        if stream is not None:
                            stream._notify_credit()
                    elif message.envelope.kind == KIND_IN_STREAM_CLOSED:
                        closed = InStreamClosed.decode(message.payload)
                        stream = self._in_streams.pop(
                            (message.envelope.target, closed.token), None
                        )
                        if stream is not None:
                            stream._notify_closed(closed.reason)
                    elif request and request in self._pending:
                        self._pending.pop(request).set_result(message)
                await self._flush()
        except BaseException as error:
            failure = error
        finally:
            self._closed = True
            for stream in tuple(self._in_streams.values()):
                stream._notify_closed(2)
            self._in_streams.clear()
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

    async def ping(self) -> "PingResult":
        """Measure one correlated protocol-control round trip."""
        hello = self.core.server_hello
        if hello is None or hello.minor < 2:
            raise SessionClosed("firmware does not support protocol ping")
        started = time.monotonic_ns()
        nonce = secrets.randbits(64)
        message = await self._exchange(
            self.core.ping(PingRequest(nonce, started).encode())
        )
        ended = time.monotonic_ns()
        if message.envelope.kind != KIND_PONG:
            raise SessionClosed("expected ping response")
        response = PingResponse.decode(message.payload)
        if response.nonce != nonce or response.host_monotonic_ns != started:
            raise SessionClosed("ping response does not match its request")
        return PingResult(
            nonce=nonce,
            round_trip_ns=ended - started,
            remote_processing_ns=max(
                0,
                (response.remote_send_us - response.remote_receive_us) * 1_000,
            ),
            remote_receive_us=response.remote_receive_us,
            remote_send_us=response.remote_send_us,
        )

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

    async def get(self, endpoint: int | str) -> Any:
        """Read an endpoint into its manifest-scoped Python model."""
        item = self._endpoint("data", endpoint)
        message = await self._exchange(
            self.core.request(item["id"], b"", OPERATION_QUERY)
        )
        return self.codec.decode_model(item["schema"], message.payload)  # type: ignore[union-attr]

    async def set(self, endpoint: int | str, value: Any) -> None:
        """Update an endpoint using a generated model, dynamic model, or mapping."""
        item = self._endpoint("data", endpoint)
        payload = self.codec.encode(item["schema"], value)  # type: ignore[union-attr]
        await self._exchange(
            self.core.request(item["id"], payload, OPERATION_UPDATE)
        )

    async def call(self, endpoint: int | str, value: Any = None) -> Any:
        """Invoke an action and decode its typed success model."""
        item = self._endpoint("actions", endpoint)
        payload = self.codec.encode(
            item["request_schema"], {} if value is None else value
        )  # type: ignore[union-attr]
        message = await self._exchange(
            self.core.request(item["id"], payload, OPERATION_ACTION)
        )
        if message.envelope.flags & FLAG_ERROR_PAYLOAD:
            raise ActionError(
                self.codec.decode_model(item["error_schema"], message.payload)  # type: ignore[union-attr]
            )
        return self.codec.decode_model(item["response_schema"], message.payload)  # type: ignore[union-attr]

    def robot(self):
        """Return the typed endpoint facade for this active session."""
        from .api import Robot

        return Robot(self)

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

    def in_stream(
        self,
        endpoint: int | str,
        *,
        frequency: float | None = None,
        credit_timeout: float | None = None,
    ) -> InboundStream:
        return InboundStream(
            self,
            endpoint,
            frequency=frequency,
            credit_timeout=credit_timeout,
        )

    async def stop(self) -> None:
        """Stop protocol work without closing the Station-owned channel."""
        for subscription in self._subscriptions.values():
            subscription._closed = True
        self._subscriptions.clear()
        for stream in self._in_streams.values():
            stream._notify_closed(2)
        self._in_streams.clear()
        if self._reader is not None:
            self._reader.cancel()
            try:
                await self._reader
            except asyncio.CancelledError:
                pass
            self._reader = None
