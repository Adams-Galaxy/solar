"""Persistence policy and recording operations."""

from __future__ import annotations

import asyncio
import logging
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from ..database import StationDatabase
from ..models import Recording, SourceKind, StationEvent

LOGGER = logging.getLogger("solar_station.recorder")


@dataclass(slots=True)
class _Capture:
    event: StationEvent
    robot_session_id: int | None


@dataclass(slots=True)
class _Flush:
    completed: asyncio.Future[None]


class Recorder:
    def __init__(
        self,
        database: StationDatabase,
        *,
        queue_depth: int = 4096,
        batch_size: int = 128,
        flush_interval: float = 0.05,
    ):
        self.database = database
        self._active = 0
        self._queue: asyncio.Queue[_Capture | _Flush | None] = asyncio.Queue(
            queue_depth
        )
        self._batch_size = batch_size
        self._flush_interval = flush_interval
        self._worker: asyncio.Task[None] | None = None
        self._dropped = 0

    async def initialize(self) -> None:
        self._active = await self.database.active_recording_count()
        self._worker = asyncio.create_task(
            self._run_writer(), name="station-persistence"
        )

    async def capture(
        self, event: StationEvent, robot_session_id: int | None
    ) -> None:
        always = event.source_kind in (SourceKind.LOG.value, SourceKind.SERVER.value)
        if not always and self._active == 0:
            return None
        try:
            self._queue.put_nowait(_Capture(event, robot_session_id))
        except asyncio.QueueFull:
            self._dropped += 1
        return None

    async def flush(self) -> None:
        if self._worker is None:
            return
        completed = asyncio.get_running_loop().create_future()
        await self._queue.put(_Flush(completed))
        await completed

    async def start(self, name: str) -> Recording:
        await self.flush()
        recording = await self.database.start_recording(name)
        self._active += 1
        LOGGER.info("Recording started: %s", recording.name)
        return recording

    async def stop(self, name: str | None = None) -> Recording:
        await self.flush()
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

    @property
    def dropped_count(self) -> int:
        return self._dropped

    async def close(self) -> None:
        worker, self._worker = self._worker, None
        if worker is None:
            return
        await self.flush_worker(worker)

    async def flush_worker(self, worker: asyncio.Task[None]) -> None:
        completed = asyncio.get_running_loop().create_future()
        await self._queue.put(_Flush(completed))
        await completed
        await self._queue.put(None)
        await worker

    async def _run_writer(self) -> None:
        batch: list[tuple[StationEvent, int | None]] = []
        while True:
            item = await self._queue.get()
            if item is None:
                return
            if isinstance(item, _Flush):
                if batch:
                    await self.database.store_events(batch)
                    batch.clear()
                item.completed.set_result(None)
                continue
            batch.append((item.event, item.robot_session_id))
            deadline = asyncio.get_running_loop().time() + self._flush_interval
            while len(batch) < self._batch_size:
                remaining = deadline - asyncio.get_running_loop().time()
                if remaining <= 0:
                    break
                try:
                    item = await asyncio.wait_for(self._queue.get(), remaining)
                except TimeoutError:
                    break
                if item is None:
                    await self.database.store_events(batch)
                    return
                if isinstance(item, _Flush):
                    await self.database.store_events(batch)
                    batch.clear()
                    item.completed.set_result(None)
                    break
                batch.append((item.event, item.robot_session_id))
            if batch:
                await self.database.store_events(batch)
                batch.clear()
