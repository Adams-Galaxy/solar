"""Internal Solar Station event hub."""

from __future__ import annotations

import asyncio
import itertools
import time
import uuid
from collections.abc import Awaitable, Callable
from typing import Any

from ..models import SourceKind, StationEvent
from .recorder import Recorder

EventSink = Callable[[StationEvent], Awaitable[None]]


class EventHub:
    def __init__(self, recorder: Recorder):
        self.server_id = uuid.uuid4().hex
        self.recorder = recorder
        self._sequence = itertools.count(1)
        self._sinks: list[EventSink] = []
        self.robot_session_id: int | None = None

    @property
    def live_sequence(self) -> int:
        # itertools has no observation API. Server status tracks published count.
        return getattr(self, "_last_sequence", 0)

    def add_sink(self, sink: EventSink) -> None:
        self._sinks.append(sink)

    def remove_sink(self, sink: EventSink) -> None:
        if sink in self._sinks:
            self._sinks.remove(sink)

    async def publish(
        self,
        source_key: str,
        source_kind: SourceKind | str,
        value: Any,
        *,
        endpoint_id: int | None = None,
        endpoint_name: str | None = None,
        schema_id: int | None = None,
        manifest_digest: bytes | None = None,
        source_loss_count: int = 0,
        persist: bool = True,
    ) -> StationEvent:
        sequence = next(self._sequence)
        self._last_sequence = sequence
        event = StationEvent(
            server_id=self.server_id,
            live_sequence=sequence,
            wall_ns=time.time_ns(),
            monotonic_ns=time.monotonic_ns(),
            source_key=source_key,
            source_kind=(
                source_kind.value
                if isinstance(source_kind, SourceKind)
                else source_kind
            ),
            value=value,
            endpoint_id=endpoint_id,
            endpoint_name=endpoint_name,
            schema_id=schema_id,
            manifest_digest=manifest_digest,
            source_loss_count=source_loss_count,
        )
        if persist:
            event.stored_event_id = await self.recorder.capture(
                event, self.robot_session_id
            )
        if self._sinks:
            await asyncio.gather(
                *(sink(event) for sink in tuple(self._sinks)),
                return_exceptions=True,
            )
        return event

    async def publish_server_state(
        self, state: str, *, persist: bool = True, **details: Any
    ) -> StationEvent:
        return await self.publish(
            "station.server",
            SourceKind.SERVER,
            {"state": state, **details},
            persist=persist,
        )

    async def publish_source_state(
        self, source: str, state: str, **details: Any
    ) -> StationEvent:
        return await self.publish(
            source,
            SourceKind.SERVER,
            {"state": state, **details},
        )
