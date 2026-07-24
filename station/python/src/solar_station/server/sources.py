"""One global Remote source configuration with selective local consumption."""

from __future__ import annotations

import asyncio
import logging
from typing import Any

from ..database import StationDatabase
from ..errors import StationError
from ..models import SourceConfig, SourceKind, SourceState, SourceStatus
from .events import EventHub

LOGGER = logging.getLogger("solar_station.sources")


class SourceRegistry:
    def __init__(self, database: StationDatabase, events: EventHub):
        self.database = database
        self.events = events
        self.sources: dict[str, SourceStatus] = {}
        self.session: Any = None
        self.manifest: Any = None
        self._tasks: dict[str, asyncio.Task[None]] = {}
        self._subscriptions: dict[str, Any] = {}
        self._lock = asyncio.Lock()

    async def initialize(self) -> None:
        for source in await self.database.load_sources():
            self.sources[source.key] = SourceStatus(source)

    def list(self) -> list[dict[str, Any]]:
        return [
            value.to_wire()
            for value in sorted(
                self.sources.values(), key=lambda item: item.config.endpoint_name
            )
        ]

    def _collections(self) -> tuple[tuple[str, SourceKind, list[dict[str, Any]]], ...]:
        if self.manifest is None:
            return ()
        return (
            ("stream", SourceKind.STREAM, self.manifest.streams),
            ("topic", SourceKind.TOPIC, self.manifest.topics),
            ("data", SourceKind.DATA_STREAM, self.manifest.data),
        )

    def _continuous_endpoint(
        self, endpoint: int | str
    ) -> tuple[dict[str, Any], SourceKind]:
        if self.manifest is None:
            raise StationError("robot_offline", "no active robot manifest")
        for domain, _default_kind, collection in self._collections():
            for item in collection:
                if endpoint not in (item["id"], item["name"]):
                    continue
                if domain == "stream":
                    return item, SourceKind.STREAM
                if domain == "topic":
                    return item, SourceKind.TOPIC
                capabilities = [
                    capability
                    for capability in self.manifest.capabilities
                    if capability["domain"] == "data"
                    and capability["endpoint"] == item["id"]
                ]
                kinds = {capability["kind"] for capability in capabilities}
                if "out_stream" in kinds:
                    return item, SourceKind.DATA_STREAM
                if "watch" in kinds:
                    return item, SourceKind.DATA_WATCH
                raise StationError(
                    "unsupported_capability",
                    f"{item['name']} has no continuous output capability",
                )
        raise StationError(
            "unknown_endpoint", f"continuous endpoint {endpoint!r} not found"
        )

    def validate_subscription(self, endpoint: str) -> str:
        if endpoint in ("logs.console", "station.server"):
            return endpoint
        # Existing local selections remain valid while the robot is absent. The
        # source supervisor will restore or mark them unavailable on reconnect.
        if endpoint in self.sources:
            return endpoint
        item, _ = self._continuous_endpoint(endpoint)
        return str(item["name"])

    async def configure(
        self,
        endpoint: int | str,
        *,
        frequency: float | None,
        batch: int,
        stop: bool,
    ) -> dict[str, Any]:
        if frequency is not None and frequency <= 0:
            raise StationError("invalid_value", "frequency must be positive")
        if not 1 <= batch <= 0xFFFF:
            raise StationError("invalid_value", "batch must be 1..65535")
        item, kind = self._continuous_endpoint(endpoint)
        key = str(item["name"])
        schema_id = item.get("schema")
        digest = self.manifest.digest
        config = SourceConfig(
            key=key,
            endpoint_id=int(item["id"]),
            endpoint_name=key,
            kind=kind,
            desired=not stop,
            frequency=frequency,
            batch=batch,
            manifest_digest=digest,
        )
        status = self.sources.get(key)
        if status is None:
            status = SourceStatus(config)
            self.sources[key] = status
        else:
            status.config = config
            status.last_error = None
        await self.database.save_source(config)
        if stop:
            LOGGER.info("Disabling source %s", key)
            await self._stop_source(key, SourceState.INACTIVE)
            await self.events.publish_source_state(key, "inactive")
        elif self.session is not None:
            LOGGER.info(
                "Configuring source %s (frequency=%s, batch=%s)",
                key,
                frequency,
                batch,
            )
            await self._start_source(status, schema_id)
        return status.to_wire()

    async def online(self, session: Any, manifest: Any) -> None:
        async with self._lock:
            self.session = session
            self.manifest = manifest
        for status in self.sources.values():
            if not status.config.desired:
                status.state = SourceState.INACTIVE
                continue
            try:
                item, kind = self._continuous_endpoint(
                    status.config.endpoint_id
                    if status.config.endpoint_id is not None
                    else status.config.endpoint_name
                )
                if item["name"] != status.config.endpoint_name:
                    raise StationError(
                        "manifest_mismatch",
                        f"source ID now names {item['name']!r}, expected "
                        f"{status.config.endpoint_name!r}",
                    )
                status.config.kind = kind
                status.config.manifest_digest = manifest.digest
                await self.database.save_source(status.config)
                await self._start_source(status, item.get("schema"))
            except BaseException as error:
                status.state = SourceState.UNAVAILABLE
                status.last_error = str(error)
                LOGGER.warning(
                    "Could not restore source %s: %s", status.config.key, error
                )
                await self.events.publish_source_state(
                    status.config.key,
                    "unavailable",
                    error=str(error),
                )

    async def offline(self, reason: str) -> None:
        async with self._lock:
            self.session = None
            self.manifest = None
        for key in tuple(self._tasks):
            await self._stop_source(key, SourceState.UNAVAILABLE, reason)
        for status in self.sources.values():
            if status.config.desired:
                status.state = SourceState.UNAVAILABLE
                status.last_error = reason
                await self.events.publish_source_state(
                    status.config.key, "unavailable", error=reason
                )

    async def _start_source(
        self, status: SourceStatus, schema_id: int | None = None
    ) -> None:
        key = status.config.key
        await self._stop_source(key, SourceState.STARTING)
        status.state = SourceState.STARTING
        status.last_error = None
        assert self.session is not None
        try:
            if status.config.kind == SourceKind.TOPIC:
                subscription = await self.session.topic(key)
            elif status.config.kind == SourceKind.DATA_WATCH:
                subscription = await self.session.watch(
                    key, frequency=status.config.frequency
                )
            else:
                subscription = await self.session.stream(
                    key,
                    frequency=status.config.frequency,
                    batch=status.config.batch,
                )
        except BaseException as error:
            status.state = SourceState.FAILED
            status.last_error = str(error)
            raise StationError(
                "source_unavailable", f"could not start source {key}: {error}"
            ) from error
        self._subscriptions[key] = subscription
        status.state = SourceState.ACTIVE
        status.negotiated_frequency = status.config.frequency
        status.negotiated_batch = status.config.batch
        self._tasks[key] = asyncio.create_task(
            self._consume(status, subscription, schema_id),
            name=f"station-source:{key}",
        )
        LOGGER.info(
            "Source %s active (frequency=%s, batch=%s)",
            key,
            status.negotiated_frequency,
            status.negotiated_batch,
        )
        await self.events.publish_source_state(key, "active")

    async def _consume(
        self, status: SourceStatus, subscription: Any, schema_id: int | None
    ) -> None:
        try:
            async for value in subscription:
                event = await self.events.publish(
                    status.config.key,
                    status.config.kind,
                    value,
                    endpoint_id=status.config.endpoint_id,
                    endpoint_name=status.config.endpoint_name,
                    schema_id=schema_id,
                    manifest_digest=status.config.manifest_digest,
                    source_loss_count=getattr(subscription, "loss_count", 0),
                )
                status.last_live_sequence = event.live_sequence
                status.last_wall_ns = event.wall_ns
                status.loss_count = getattr(subscription, "loss_count", 0)
        except asyncio.CancelledError:
            raise
        except BaseException as error:
            status.state = SourceState.FAILED
            status.last_error = str(error)
            LOGGER.warning("Source %s failed: %s", status.config.key, error)
            await self.events.publish_source_state(
                status.config.key, "failed", error=str(error)
            )

    async def _stop_source(
        self,
        key: str,
        state: SourceState,
        error: str | None = None,
    ) -> None:
        task = self._tasks.pop(key, None)
        if task is not None:
            task.cancel()
            try:
                await task
            except asyncio.CancelledError:
                pass
        subscription = self._subscriptions.pop(key, None)
        if subscription is not None:
            try:
                await subscription.aclose()
            except BaseException:
                pass
        status = self.sources.get(key)
        if status is not None:
            status.state = state
            status.last_error = error

    async def close(self) -> None:
        for key in tuple(self._tasks):
            await self._stop_source(key, SourceState.INACTIVE)
        self.session = None
        self.manifest = None
