"""Single-owner SQLite persistence for Solar Station."""

from __future__ import annotations

import asyncio
import fcntl
import json
import os
import sqlite3
import time
from pathlib import Path
from typing import Any

import cbor2

from .errors import StationError
from .models import Recording, SourceConfig, SourceKind, normalize

SCHEMA_VERSION = 1


class StationDatabase:
    def __init__(self, path: Path):
        self.path = path
        self._connection: sqlite3.Connection | None = None
        self._lock = asyncio.Lock()
        self._lock_descriptor: int | None = None

    async def open(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        lock_path = self.path.with_name(f"{self.path.name}.lock")
        descriptor = os.open(lock_path, os.O_RDWR | os.O_CREAT, 0o600)
        try:
            fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            os.close(descriptor)
            raise StationError(
                "server_already_running",
                f"another Station server owns database {self.path}",
            ) from error
        self._lock_descriptor = descriptor
        try:
            connection = sqlite3.connect(self.path)
            connection.row_factory = sqlite3.Row
            connection.execute("PRAGMA journal_mode=WAL")
            connection.execute("PRAGMA foreign_keys=ON")
            connection.execute("PRAGMA synchronous=NORMAL")
            version = connection.execute("PRAGMA user_version").fetchone()[0]
            if version not in (0, SCHEMA_VERSION):
                connection.close()
                raise StationError(
                    "database_error",
                    f"unsupported Station database schema {version}",
                )
            if version == 0:
                connection.executescript(
                    """
                CREATE TABLE metadata (
                    key TEXT PRIMARY KEY,
                    value BLOB NOT NULL
                );
                CREATE TABLE manifests (
                    digest BLOB PRIMARY KEY,
                    image BLOB NOT NULL,
                    received_ns INTEGER NOT NULL
                );
                CREATE TABLE robot_sessions (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    started_ns INTEGER NOT NULL,
                    ended_ns INTEGER,
                    build_id TEXT,
                    manifest_digest BLOB,
                    target TEXT NOT NULL,
                    disconnect_reason TEXT
                );
                CREATE TABLE sources (
                    source_key TEXT PRIMARY KEY,
                    endpoint_id INTEGER,
                    endpoint_name TEXT NOT NULL,
                    source_kind TEXT NOT NULL,
                    desired INTEGER NOT NULL,
                    frequency REAL,
                    batch INTEGER NOT NULL,
                    manifest_digest BLOB,
                    updated_ns INTEGER NOT NULL
                );
                CREATE TABLE events (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    wall_ns INTEGER NOT NULL,
                    monotonic_ns INTEGER NOT NULL,
                    robot_session_id INTEGER,
                    source_key TEXT NOT NULL,
                    source_kind TEXT NOT NULL,
                    endpoint_id INTEGER,
                    schema_id INTEGER,
                    manifest_digest BLOB,
                    payload BLOB NOT NULL,
                    FOREIGN KEY(robot_session_id) REFERENCES robot_sessions(id)
                );
                CREATE INDEX events_source_id ON events(source_key, id);
                CREATE INDEX events_wall_time ON events(wall_ns);
                CREATE TABLE recordings (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    name TEXT NOT NULL UNIQUE,
                    start_after_event_id INTEGER NOT NULL,
                    end_event_id INTEGER,
                    started_ns INTEGER NOT NULL,
                    ended_ns INTEGER
                );
                CREATE TABLE settings (
                    key TEXT PRIMARY KEY,
                    value BLOB NOT NULL
                );
                PRAGMA user_version=1;
                """
                )
                connection.commit()
            self._connection = connection
        except BaseException:
            self._release_process_lock()
            raise

    async def close(self) -> None:
        async with self._lock:
            if self._connection is not None:
                self._connection.commit()
                self._connection.close()
                self._connection = None
            self._release_process_lock()

    def _release_process_lock(self) -> None:
        descriptor, self._lock_descriptor = self._lock_descriptor, None
        if descriptor is not None:
            fcntl.flock(descriptor, fcntl.LOCK_UN)
            os.close(descriptor)

    @property
    def connection(self) -> sqlite3.Connection:
        if self._connection is None:
            raise StationError("database_error", "Station database is not open")
        return self._connection

    @property
    def is_open(self) -> bool:
        return self._connection is not None

    async def store_manifest(self, digest: bytes, image: bytes) -> None:
        async with self._lock:
            self.connection.execute(
                """
                INSERT INTO manifests(digest, image, received_ns)
                VALUES(?, ?, ?)
                ON CONFLICT(digest) DO UPDATE SET image=excluded.image
                """,
                (digest, image, time.time_ns()),
            )
            self.connection.commit()

    async def manifest_image(self, digest: bytes) -> bytes | None:
        async with self._lock:
            row = self.connection.execute(
                "SELECT image FROM manifests WHERE digest=?", (digest,)
            ).fetchone()
            return bytes(row["image"]) if row else None

    async def begin_robot_session(
        self, target: str, build_id: int | None, manifest_digest: bytes | None
    ) -> int:
        async with self._lock:
            cursor = self.connection.execute(
                """
                INSERT INTO robot_sessions(
                    started_ns, build_id, manifest_digest, target
                ) VALUES(?, ?, ?, ?)
                """,
                (
                    time.time_ns(),
                    None if build_id is None else str(build_id),
                    manifest_digest,
                    target,
                ),
            )
            self.connection.commit()
            return int(cursor.lastrowid)

    async def end_robot_session(self, session_id: int, reason: str) -> None:
        async with self._lock:
            self.connection.execute(
                """
                UPDATE robot_sessions
                SET ended_ns=?, disconnect_reason=?
                WHERE id=? AND ended_ns IS NULL
                """,
                (time.time_ns(), reason, session_id),
            )
            self.connection.commit()

    async def save_source(self, source: SourceConfig) -> None:
        async with self._lock:
            self.connection.execute(
                """
                INSERT INTO sources(
                    source_key, endpoint_id, endpoint_name, source_kind,
                    desired, frequency, batch, manifest_digest, updated_ns
                ) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)
                ON CONFLICT(source_key) DO UPDATE SET
                    endpoint_id=excluded.endpoint_id,
                    endpoint_name=excluded.endpoint_name,
                    source_kind=excluded.source_kind,
                    desired=excluded.desired,
                    frequency=excluded.frequency,
                    batch=excluded.batch,
                    manifest_digest=excluded.manifest_digest,
                    updated_ns=excluded.updated_ns
                """,
                (
                    source.key,
                    source.endpoint_id,
                    source.endpoint_name,
                    source.kind.value,
                    source.desired,
                    source.frequency,
                    source.batch,
                    source.manifest_digest,
                    time.time_ns(),
                ),
            )
            self.connection.commit()

    async def load_sources(self) -> list[SourceConfig]:
        async with self._lock:
            rows = self.connection.execute(
                "SELECT * FROM sources ORDER BY endpoint_name"
            ).fetchall()
        return [
            SourceConfig(
                key=row["source_key"],
                endpoint_id=row["endpoint_id"],
                endpoint_name=row["endpoint_name"],
                kind=SourceKind(row["source_kind"]),
                desired=bool(row["desired"]),
                frequency=row["frequency"],
                batch=row["batch"],
                manifest_digest=(
                    bytes(row["manifest_digest"])
                    if row["manifest_digest"] is not None
                    else None
                ),
            )
            for row in rows
        ]

    async def active_recording_count(self) -> int:
        async with self._lock:
            return int(
                self.connection.execute(
                    "SELECT COUNT(*) FROM recordings WHERE end_event_id IS NULL"
                ).fetchone()[0]
            )

    async def store_event(
        self,
        *,
        wall_ns: int,
        monotonic_ns: int,
        robot_session_id: int | None,
        source_key: str,
        source_kind: str,
        endpoint_id: int | None,
        schema_id: int | None,
        manifest_digest: bytes | None,
        value: Any,
    ) -> int:
        payload = cbor2.dumps(normalize(value), canonical=True)
        async with self._lock:
            cursor = self.connection.execute(
                """
                INSERT INTO events(
                    wall_ns, monotonic_ns, robot_session_id, source_key,
                    source_kind, endpoint_id, schema_id, manifest_digest, payload
                ) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    wall_ns,
                    monotonic_ns,
                    robot_session_id,
                    source_key,
                    source_kind,
                    endpoint_id,
                    schema_id,
                    manifest_digest,
                    payload,
                ),
            )
            self.connection.commit()
            return int(cursor.lastrowid)

    async def query_events(
        self,
        *,
        source_key: str | None = None,
        first: int | None = None,
        last: int | None = None,
        after_id: int | None = None,
        through_id: int | None = None,
    ) -> list[dict[str, Any]]:
        if first is not None and last is not None:
            raise StationError("invalid_request", "choose first or last, not both")
        limit = first if first is not None else last
        if limit is not None and not 1 <= limit <= 10_000:
            raise StationError("invalid_request", "event limit must be 1..10000")
        clauses: list[str] = []
        arguments: list[Any] = []
        if source_key is not None:
            clauses.append("source_key=?")
            arguments.append(source_key)
        if after_id is not None:
            clauses.append("id>?")
            arguments.append(after_id)
        if through_id is not None:
            clauses.append("id<=?")
            arguments.append(through_id)
        where = f"WHERE {' AND '.join(clauses)}" if clauses else ""
        descending = last is not None
        direction = "DESC" if descending else "ASC"
        sql = f"SELECT * FROM events {where} ORDER BY id {direction}"
        if limit is not None:
            sql += " LIMIT ?"
            arguments.append(limit)
        async with self._lock:
            rows = self.connection.execute(sql, arguments).fetchall()
        if descending:
            rows.reverse()
        return [self._event_row(row) for row in rows]

    def _event_row(self, row: sqlite3.Row) -> dict[str, Any]:
        return {
            "event_id": row["id"],
            "wall_ns": row["wall_ns"],
            "monotonic_ns": row["monotonic_ns"],
            "robot_session_id": row["robot_session_id"],
            "source": row["source_key"],
            "source_kind": row["source_kind"],
            "endpoint_id": row["endpoint_id"],
            "schema_id": row["schema_id"],
            "manifest_digest": (
                bytes(row["manifest_digest"])
                if row["manifest_digest"] is not None
                else None
            ),
            "value": cbor2.loads(row["payload"]),
        }

    async def start_recording(self, name: str) -> Recording:
        name = name.strip()
        if not name:
            raise StationError("invalid_request", "recording name must not be empty")
        async with self._lock:
            connection = self.connection
            connection.execute("BEGIN IMMEDIATE")
            boundary = int(
                connection.execute(
                    "SELECT COALESCE(MAX(id), 0) FROM events"
                ).fetchone()[0]
            )
            started = time.time_ns()
            try:
                cursor = connection.execute(
                    """
                    INSERT INTO recordings(
                        name, start_after_event_id, started_ns
                    ) VALUES(?, ?, ?)
                    """,
                    (name, boundary, started),
                )
            except sqlite3.IntegrityError as error:
                connection.rollback()
                raise StationError(
                    "recording_already_exists",
                    f"recording {name!r} already exists",
                ) from error
            connection.commit()
            return Recording(int(cursor.lastrowid), name, boundary, None, started, None)

    async def stop_recording(self, name: str | None = None) -> Recording:
        async with self._lock:
            connection = self.connection
            connection.execute("BEGIN IMMEDIATE")
            if name is None:
                row = connection.execute(
                    """
                    SELECT * FROM recordings
                    WHERE end_event_id IS NULL
                    ORDER BY started_ns DESC, id DESC LIMIT 1
                    """
                ).fetchone()
            else:
                row = connection.execute(
                    """
                    SELECT * FROM recordings
                    WHERE name=? AND end_event_id IS NULL
                    """,
                    (name,),
                ).fetchone()
            if row is None:
                connection.rollback()
                target = "an active recording" if name is None else repr(name)
                raise StationError(
                    "recording_not_found", f"could not find {target} to stop"
                )
            boundary = int(
                connection.execute(
                    "SELECT COALESCE(MAX(id), 0) FROM events"
                ).fetchone()[0]
            )
            ended = time.time_ns()
            connection.execute(
                """
                UPDATE recordings SET end_event_id=?, ended_ns=? WHERE id=?
                """,
                (boundary, ended, row["id"]),
            )
            connection.commit()
            return Recording(
                row["id"],
                row["name"],
                row["start_after_event_id"],
                boundary,
                row["started_ns"],
                ended,
            )

    async def recordings(self, name: str | None = None) -> list[Recording]:
        async with self._lock:
            if name is None:
                rows = self.connection.execute(
                    "SELECT * FROM recordings ORDER BY started_ns DESC, id DESC"
                ).fetchall()
            else:
                rows = self.connection.execute(
                    "SELECT * FROM recordings WHERE name=?", (name,)
                ).fetchall()
        return [
            Recording(
                row["id"],
                row["name"],
                row["start_after_event_id"],
                row["end_event_id"],
                row["started_ns"],
                row["ended_ns"],
            )
            for row in rows
        ]

    async def recording_events(
        self, name: str
    ) -> tuple[Recording, list[dict[str, Any]]]:
        matches = await self.recordings(name)
        if not matches:
            raise StationError("recording_not_found", f"recording {name!r} not found")
        recording = matches[0]
        events = await self.query_events(
            after_id=recording.start_after_event_id,
            through_id=recording.end_event_id,
        )
        return recording, events

    async def export_recording(self, name: str, format_name: str, output: Path) -> int:
        recording, events = await self.recording_events(name)
        output.parent.mkdir(parents=True, exist_ok=True)
        if format_name == "jsonl":
            with output.open("w", encoding="utf-8") as stream:
                for event in events:
                    stream.write(json.dumps(_json_safe(event), sort_keys=True) + "\n")
        elif format_name == "json":
            output.write_text(
                json.dumps(
                    {
                        "recording": recording.to_wire(),
                        "events": _json_safe(events),
                    },
                    indent=2,
                    sort_keys=True,
                )
                + "\n",
                encoding="utf-8",
            )
        elif format_name == "text":
            with output.open("w", encoding="utf-8") as stream:
                for event in events:
                    stream.write(
                        f"{event['wall_ns']} {event['source']} "
                        f"{_text_value(event['value'])}\n"
                    )
        elif format_name == "cbor":
            output.write_bytes(
                cbor2.dumps(
                    {
                        "recording": recording.to_wire(),
                        "events": events,
                    },
                    canonical=True,
                )
            )
        else:
            raise StationError(
                "invalid_request",
                f"unsupported recording format {format_name!r}",
            )
        return len(events)


def _json_safe(value: Any) -> Any:
    if isinstance(value, bytes):
        return {"$bytes": value.hex()}
    if isinstance(value, dict):
        return {str(key): _json_safe(item) for key, item in value.items()}
    if isinstance(value, list):
        return [_json_safe(item) for item in value]
    return value


def _text_value(value: Any) -> str:
    return (
        value
        if isinstance(value, str)
        else json.dumps(_json_safe(value), sort_keys=True)
    )
