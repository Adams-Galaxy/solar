from __future__ import annotations

import sqlite3
import time
from pathlib import Path

import pytest

from solar_station.config import migrate_legacy_database
from solar_station.database import StationDatabase
from solar_station.models import SourceConfig, SourceKind


def test_legacy_database_is_migrated_once(tmp_path: Path) -> None:
    legacy = tmp_path / "legacy.sqlite3"
    destination = tmp_path / "new" / "station.sqlite3"
    with sqlite3.connect(legacy) as database:
        database.execute("CREATE TABLE marker (value TEXT NOT NULL)")
        database.execute("INSERT INTO marker VALUES ('preserved')")

    assert migrate_legacy_database(destination, legacy=legacy)
    assert not migrate_legacy_database(destination, legacy=legacy)
    with sqlite3.connect(destination) as database:
        assert database.execute("SELECT value FROM marker").fetchone() == (
            "preserved",
        )


@pytest.mark.asyncio
async def test_sources_recording_ranges_and_overlap(tmp_path: Path) -> None:
    database = StationDatabase(tmp_path / "station.sqlite3")
    await database.open()
    try:
        source = SourceConfig(
            "imu.euler",
            11,
            "imu.euler",
            SourceKind.DATA_STREAM,
            True,
            100,
            1,
            b"\x42" * 32,
        )
        await database.save_source(source)
        assert (await database.load_sources())[0] == source

        first = await database.start_recording("first")
        event1 = await _store(database, "imu.euler", {"roll": 1.0})
        await database.start_recording("second")
        event2 = await _store(database, "imu.euler", {"roll": 2.0})
        stopped_first = await database.stop_recording("first")
        event3 = await _store(database, "logs.console", "after first")
        stopped_second = await database.stop_recording()

        assert first.start_after_event_id == 0
        assert stopped_first.end_event_id == event2
        assert stopped_second.end_event_id == event3
        assert [
            event["event_id"] for event in (await database.recording_events("first"))[1]
        ] == [
            event1,
            event2,
        ]
        assert [
            event["event_id"]
            for event in (await database.recording_events("second"))[1]
        ] == [event2, event3]
    finally:
        await database.close()


async def _store(database: StationDatabase, source: str, value: object) -> int:
    return await database.store_event(
        wall_ns=time.time_ns(),
        monotonic_ns=time.monotonic_ns(),
        robot_session_id=None,
        source_key=source,
        source_kind="log" if source == "logs.console" else "data_stream",
        endpoint_id=None,
        schema_id=None,
        manifest_digest=None,
        value=value,
    )
