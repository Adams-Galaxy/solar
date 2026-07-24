"""Persistence policy and recording operations."""

from __future__ import annotations

import logging
from pathlib import Path
from typing import Any

from ..database import StationDatabase
from ..models import Recording, SourceKind, StationEvent

LOGGER = logging.getLogger("solar_station.recorder")


class Recorder:
    def __init__(self, database: StationDatabase):
        self.database = database
        self._active = 0

    async def initialize(self) -> None:
        self._active = await self.database.active_recording_count()

    async def capture(
        self, event: StationEvent, robot_session_id: int | None
    ) -> int | None:
        always = event.source_kind in (SourceKind.LOG.value, SourceKind.SERVER.value)
        if not always and self._active == 0:
            return None
        return await self.database.store_event(
            wall_ns=event.wall_ns,
            monotonic_ns=event.monotonic_ns,
            robot_session_id=robot_session_id,
            source_key=event.source_key,
            source_kind=event.source_kind,
            endpoint_id=event.endpoint_id,
            schema_id=event.schema_id,
            manifest_digest=event.manifest_digest,
            value=event.value,
        )

    async def start(self, name: str) -> Recording:
        recording = await self.database.start_recording(name)
        self._active += 1
        LOGGER.info("Recording started: %s", recording.name)
        return recording

    async def stop(self, name: str | None = None) -> Recording:
        recording = await self.database.stop_recording(name)
        self._active = max(0, self._active - 1)
        LOGGER.info("Recording stopped: %s", recording.name)
        return recording

    async def list(self, name: str | None = None) -> list[Recording]:
        return await self.database.recordings(name)

    async def export(self, name: str, format_name: str, output: Path) -> dict[str, Any]:
        count = await self.database.export_recording(name, format_name, output)
        LOGGER.info(
            "Recording exported: %s (%s events, %s)",
            name,
            count,
            output,
        )
        return {
            "name": name,
            "format": format_name,
            "output": str(output),
            "events": count,
        }

    @property
    def active_count(self) -> int:
        return self._active
