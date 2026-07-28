"""Typed endpoint facade over a Remote session."""

from __future__ import annotations

import itertools
import time
from collections.abc import AsyncIterator, Iterator, Mapping
from contextlib import AbstractAsyncContextManager
from typing import Any, Generic, TypeVar

from .descriptors import (
    ActionEndpoint,
    Catalog,
    DataEndpoint,
    EndpointDescriptor,
    ManifestCatalog,
    StreamEndpoint,
    TopicEndpoint,
)
from .frames import Frame


T = TypeVar("T")


class UnsupportedOperation(RuntimeError):
    def __init__(self, endpoint: EndpointDescriptor | ActionEndpoint, operation: str):
        super().__init__(f"{endpoint.name} does not support {operation}")
        self.endpoint = endpoint
        self.operation = operation


class FrameSubscription(
    AbstractAsyncContextManager["FrameSubscription[T]"],
    AsyncIterator[Frame[T]],
    Generic[T],
):
    def __init__(
        self,
        session: Any,
        descriptor: EndpointDescriptor,
        subscription: Any,
    ):
        self.session = session
        self.descriptor = descriptor
        self.subscription = subscription
        self._sequence = itertools.count(1)

    @property
    def closed(self) -> bool:
        return bool(getattr(self.subscription, "_closed", False))

    @property
    def loss_count(self) -> int:
        return int(getattr(self.subscription, "loss_count", 0))

    async def __aenter__(self) -> "FrameSubscription[T]":
        return self

    async def __aexit__(self, *_: object) -> None:
        await self.aclose()

    def __aiter__(self) -> "FrameSubscription[T]":
        return self

    async def __anext__(self) -> Frame[T]:
        value = await self.subscription.__anext__()
        if not hasattr(value, "__solar_schema_id__"):
            value = self.session.codec.models.construct(
                self.descriptor.schema_id, value
            )
        return Frame(
            value=value,
            endpoint=self.descriptor,
            sequence=next(self._sequence),
            received_monotonic_ns=time.monotonic_ns(),
            received_wall_ns=time.time_ns(),
            session_id=str(self.session.core.session_epoch),
            loss_count=self.loss_count,
        )

    async def aclose(self) -> None:
        await self.subscription.aclose()


class BoundDataEndpoint:
    def __init__(self, session: Any, descriptor: DataEndpoint):
        self.session = session
        self.descriptor = descriptor

    async def get(self) -> Any:
        if not self.descriptor.supports("query"):
            raise UnsupportedOperation(self.descriptor, "get")
        return await self.session.get(self.descriptor.id)

    async def set(self, value: Any) -> None:
        if not self.descriptor.supports("update"):
            raise UnsupportedOperation(self.descriptor, "set")
        await self.session.set(self.descriptor.id, value)

    async def subscribe(
        self,
        *,
        frequency: float | None = None,
        batch: int = 1,
        queue_depth: int = 32,
    ) -> FrameSubscription[Any]:
        if self.descriptor.supports("out_stream"):
            subscription = await self.session.stream(
                self.descriptor.id,
                frequency=frequency,
                batch=batch,
                queue_depth=queue_depth,
            )
        elif self.descriptor.supports("watch"):
            if batch != 1:
                raise ValueError("watch subscriptions do not support batching")
            subscription = await self.session.watch(
                self.descriptor.id,
                frequency=frequency,
                queue_depth=queue_depth,
            )
        else:
            raise UnsupportedOperation(self.descriptor, "subscribe")
        return FrameSubscription(self.session, self.descriptor, subscription)

    def open_input(
        self,
        *,
        frequency: float | None = None,
        credit_timeout: float | None = None,
    ) -> Any:
        if not self.descriptor.supports("in_stream"):
            raise UnsupportedOperation(self.descriptor, "open_input")
        return self.session.in_stream(
            self.descriptor.id,
            frequency=frequency,
            credit_timeout=credit_timeout,
        )


class BoundActionEndpoint:
    def __init__(self, session: Any, descriptor: ActionEndpoint):
        self.session = session
        self.descriptor = descriptor

    async def call(self, value: Any = None) -> Any:
        return await self.session.call(self.descriptor.id, value)

    async def __call__(self, value: Any = None) -> Any:
        return await self.call(value)


class BoundOutputEndpoint:
    def __init__(
        self,
        session: Any,
        descriptor: TopicEndpoint | StreamEndpoint,
    ):
        self.session = session
        self.descriptor = descriptor

    async def subscribe(
        self,
        *,
        frequency: float | None = None,
        batch: int = 1,
        queue_depth: int = 32,
    ) -> FrameSubscription[Any]:
        if isinstance(self.descriptor, TopicEndpoint):
            if frequency is not None or batch != 1:
                raise ValueError("topics do not support frequency or batching")
            subscription = await self.session.topic(
                self.descriptor.id, queue_depth=queue_depth
            )
        else:
            subscription = await self.session.stream(
                self.descriptor.id,
                frequency=frequency,
                batch=batch,
                queue_depth=queue_depth,
            )
        return FrameSubscription(self.session, self.descriptor, subscription)


E = TypeVar("E")
Bound = TypeVar("Bound")


class BoundCatalog(Mapping[str, Bound], Generic[E, Bound]):
    def __init__(self, catalog: Catalog[E], binder: Any):
        self.catalog = catalog
        self._values = {getattr(value, "name"): binder(value) for value in catalog}

    def __getitem__(self, key: str | int) -> Bound:
        if isinstance(key, int):
            key = getattr(self.catalog.by_id(key), "name")
        return self._values[key]

    def __iter__(self) -> Iterator[str]:
        return iter(self._values)

    def __len__(self) -> int:
        return len(self._values)


class Robot:
    """Manifest-scoped typed facade bound to one active Remote session."""

    def __init__(self, session: Any):
        if session.manifest is None:
            raise RuntimeError("Remote session has no manifest")
        self.session = session
        self.manifest = session.manifest
        self.catalog: ManifestCatalog = self.manifest.require_catalog()
        self.data = BoundCatalog(self.catalog.data, lambda value: BoundDataEndpoint(session, value))
        self.actions = BoundCatalog(
            self.catalog.actions, lambda value: BoundActionEndpoint(session, value)
        )
        self.topics = BoundCatalog(
            self.catalog.topics, lambda value: BoundOutputEndpoint(session, value)
        )
        self.streams = BoundCatalog(
            self.catalog.streams, lambda value: BoundOutputEndpoint(session, value)
        )
