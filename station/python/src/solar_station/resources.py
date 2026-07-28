"""Typed, ergonomic resources exposed by :class:`StationClient`."""

from __future__ import annotations

from collections.abc import AsyncIterator, Iterator, Mapping
from contextlib import AbstractAsyncContextManager
from typing import Any, Generic, TypeVar

from solar_remote import (
    ActionEndpoint,
    DataEndpoint,
    EndpointDescriptor,
    Frame,
    ManifestCatalog,
    ModelRegistry,
    StreamEndpoint,
    TopicEndpoint,
    UnsupportedOperation,
)

from .models import ConnectionStatus, RecordingInformation, SourceInformation

T = TypeVar("T")


StationFrame = Frame


class TypedSubscription(
    AbstractAsyncContextManager["TypedSubscription[T]"],
    AsyncIterator[StationFrame[T]],
    Generic[T],
):
    def __init__(self, client: Any, descriptor: EndpointDescriptor, raw: Any):
        self.client = client
        self.descriptor = descriptor
        self.raw = raw

    @property
    def closed(self) -> bool:
        return bool(self.raw._closed)

    @property
    def loss_count(self) -> int:
        return int(self.raw.loss_count)

    @property
    def queue_capacity(self) -> int:
        return int(self.raw.queue_capacity)

    @property
    def queue_policy(self) -> str:
        return str(self.raw.queue_policy)

    async def __aenter__(self) -> TypedSubscription[T]:
        return self

    async def __aexit__(self, *_: object) -> None:
        await self.close()

    def __aiter__(self) -> TypedSubscription[T]:
        return self

    async def __anext__(self) -> StationFrame[T]:
        event = await self.raw.__anext__()
        value = self.client._model_value(self.descriptor.schema_id, event["value"])
        return Frame(
            value=value,
            endpoint=self.descriptor,
            sequence=int(event["live_sequence"]),
            received_monotonic_ns=int(event["monotonic_ns"]),
            received_wall_ns=int(event["wall_ns"]),
            session_id=str(event["server_id"]),
            loss_count=int(event.get("source_loss_count", 0)) + self.loss_count,
        )

    async def close(self) -> None:
        await self.raw.close()


class RobotData:
    def __init__(self, client: Any, descriptor: DataEndpoint):
        self.client = client
        self.descriptor = descriptor

    async def get(self) -> Any:
        if not self.descriptor.supports("query"):
            raise UnsupportedOperation(self.descriptor, "get")
        value = await self.client.get(self.descriptor.id)
        return self.client._model_value(self.descriptor.schema_id, value)

    async def set(self, value: Any) -> None:
        if not self.descriptor.supports("update"):
            raise UnsupportedOperation(self.descriptor, "set")
        await self.client.set(self.descriptor.id, value)


class RobotAction:
    def __init__(self, client: Any, descriptor: ActionEndpoint):
        self.client = client
        self.descriptor = descriptor

    async def call(self, value: Any = None) -> Any:
        result = await self.client.call(self.descriptor.id, value)
        return self.client._model_value(self.descriptor.response_schema_id, result)

    async def __call__(self, value: Any = None) -> Any:
        return await self.call(value)


class ResourceCatalog(Mapping[str, T], Generic[T]):
    def __init__(self, client: Any, collection: str, binder: Any):
        self.client = client
        self.collection = collection
        self.binder = binder

    def _catalog(self) -> Any:
        return getattr(self.client._require_catalog(), self.collection)

    def __getitem__(self, identity: str | int) -> T:
        return self.binder(self._catalog().resolve(identity))

    def __iter__(self) -> Iterator[str]:
        return (descriptor.name for descriptor in self._catalog())

    def __len__(self) -> int:
        return len(self._catalog())


class RobotResource:
    def __init__(self, client: Any):
        self.data = ResourceCatalog(
            client, "data", lambda descriptor: RobotData(client, descriptor)
        )
        self.actions = ResourceCatalog(
            client, "actions", lambda descriptor: RobotAction(client, descriptor)
        )


class Source:
    def __init__(
        self,
        client: Any,
        descriptor: DataEndpoint | TopicEndpoint | StreamEndpoint,
    ):
        self.client = client
        self.descriptor = descriptor

    async def configure(
        self,
        *,
        frequency: float | None = None,
        batch: int = 1,
        enabled: bool = True,
    ) -> SourceInformation:
        return SourceInformation.from_wire(
            await self.client.configure_stream(
                self.descriptor.id,
                frequency=frequency,
                batch=batch,
                stop=not enabled,
            )
        )

    async def subscribe(self, *, queue_depth: int = 128) -> TypedSubscription[Any]:
        raw = await self.client.subscribe(
            self.descriptor.name,
            queue_depth=queue_depth,
        )
        return TypedSubscription(self.client, self.descriptor, raw)


class SourceCollection(Mapping[str, Source]):
    def __init__(self, client: Any):
        self.client = client
        self.configured: set[str] = set()

    def _descriptor(self, identity: str | int) -> EndpointDescriptor:
        catalog = self.client._require_catalog()
        for collection in (catalog.data, catalog.topics, catalog.streams):
            descriptor = collection.get(identity)
            if descriptor is not None:
                if isinstance(descriptor, DataEndpoint) and not (
                    descriptor.supports("watch")
                    or descriptor.supports("out_stream")
                ):
                    break
                return descriptor
        raise KeyError(identity)

    def __getitem__(self, identity: str | int) -> Source:
        return Source(self.client, self._descriptor(identity))

    def __iter__(self) -> Iterator[str]:
        return iter(self.configured)

    def __len__(self) -> int:
        return len(self.configured)

    def replace(self, values: set[str]) -> None:
        self.configured = values


class InputProducer(AbstractAsyncContextManager["InputProducer"]):
    def __init__(
        self,
        client: Any,
        descriptor: DataEndpoint,
        *,
        frequency: float | None,
        credit_timeout: float | None,
    ):
        self.client = client
        self.descriptor = descriptor
        self.frequency = frequency
        self.credit_timeout = credit_timeout
        self.handle: int | None = None
        self.policy: dict[str, Any] | None = None

    @property
    def closed(self) -> bool:
        return self.handle is None

    async def __aenter__(self) -> InputProducer:
        if self.handle is not None:
            return self
        result = await self.client.open_input(
            self.descriptor.id,
            frequency=self.frequency,
            credit_timeout=self.credit_timeout,
        )
        self.handle = int(result["handle"])
        self.policy = result
        return self

    async def __aexit__(self, *_: object) -> None:
        await self.close()

    async def send(
        self,
        value: Any,
        *,
        timeout: float | None = None,  # noqa: ASYNC109
    ) -> None:
        if self.handle is None:
            raise RuntimeError("input producer is not open")
        await self.client.send_input(self.handle, value, timeout=timeout)

    async def close(self) -> None:
        handle, self.handle = self.handle, None
        if handle is not None:
            await self.client.close_input(handle)


class InputEndpoint:
    def __init__(self, client: Any, descriptor: DataEndpoint):
        self.client = client
        self.descriptor = descriptor

    def open(
        self,
        *,
        frequency: float | None = None,
        credit_timeout: float | None = None,
    ) -> InputProducer:
        return InputProducer(
            self.client,
            self.descriptor,
            frequency=frequency,
            credit_timeout=credit_timeout,
        )


class InputCollection(Mapping[int | str, Any]):
    def __init__(self, client: Any):
        self.client = client
        self.active: dict[int, dict[str, Any]] = {}

    def __getitem__(self, identity: int | str) -> Any:
        if isinstance(identity, int):
            return self.active[identity]
        descriptor = self.client._require_catalog().data.resolve(identity)
        if not descriptor.supports("in_stream"):
            raise UnsupportedOperation(descriptor, "open_input")
        return InputEndpoint(self.client, descriptor)

    def __iter__(self) -> Iterator[int]:
        return iter(self.active)

    def __len__(self) -> int:
        return len(self.active)


class LogsResource:
    def __init__(self, client: Any):
        self.client = client

    async def history(
        self,
        *,
        head: int | None = None,
        tail: int | None = None,
    ) -> list[dict[str, Any]]:
        return await self.client.query_logs(head=head, tail=tail)

    async def subscribe(self, *, queue_depth: int = 128) -> Any:
        return await self.client.subscribe("logs.console", queue_depth=queue_depth)


class RecordingsResource:
    def __init__(self, client: Any):
        self.client = client
        self.known: set[str] = set()
        self.active: set[str] = set()

    def __iter__(self) -> Iterator[str]:
        return iter(self.known)

    def __len__(self) -> int:
        return len(self.known)

    async def start(self, name: str) -> RecordingInformation:
        return RecordingInformation.from_wire(await self.client.start_recording(name))

    async def stop(self, name: str | None = None) -> RecordingInformation:
        return RecordingInformation.from_wire(await self.client.stop_recording(name))

    async def list(self) -> list[RecordingInformation]:
        return [
            RecordingInformation.from_wire(value)
            for value in await self.client.list_recordings()
        ]

    async def show(self, name: str) -> dict[str, Any]:
        return await self.client.show_recording(name)


class ConnectionResource:
    def __init__(self, client: Any):
        self.client = client

    async def status(self) -> ConnectionStatus:
        value = await self.client.status()
        return ConnectionStatus.from_wire(value["robot"])

    async def reconnect(
        self, *, timeout: float = 10.0  # noqa: ASYNC109
    ) -> dict[str, Any]:
        return await self.client.reconnect(timeout=timeout)

    async def ping(self) -> Any:
        return await self.client.ping()


def manifest_runtime(
    raw: Mapping[str, Any],
) -> tuple[ManifestCatalog, ModelRegistry]:
    catalog = ManifestCatalog.from_raw(
        schemas=raw.get("schemas", ()),
        data=raw.get("data", ()),
        actions=raw.get("actions", ()),
        topics=raw.get("topics", ()),
        streams=raw.get("streams", ()),
        links=raw.get("links", ()),
        capabilities=raw.get("capabilities", ()),
        in_stream_groups=raw.get("in_stream_groups", ()),
    )
    return catalog, ModelRegistry(catalog)
