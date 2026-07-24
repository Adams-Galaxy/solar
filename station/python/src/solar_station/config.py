"""Solar Station paths and runtime configuration."""

from __future__ import annotations

import os
import sqlite3
import tempfile
from dataclasses import dataclass
from pathlib import Path

from platformdirs import user_cache_path, user_data_path, user_runtime_path


def _environment(primary: str, legacy: str) -> str | None:
    return os.environ.get(primary) or os.environ.get(legacy)


def default_socket_path() -> Path:
    explicit = _environment("SOLAR_STATION_SOCKET", "STATION_SOCKET")
    if explicit:
        return Path(explicit).expanduser()
    runtime = user_runtime_path("solar-station", ensure_exists=False)
    candidate = runtime / "station.sock"
    # Unix-domain paths are short on macOS. Fall back before reaching its limit.
    if len(os.fsencode(candidate)) < 96:
        return candidate
    return Path(tempfile.gettempdir()) / f"solar-station-{os.getuid()}.sock"


def default_database_path() -> Path:
    explicit = _environment("SOLAR_STATION_DATABASE", "STATION_DATABASE")
    if explicit:
        return Path(explicit).expanduser()
    return user_data_path("solar-station", ensure_exists=False) / "station.sqlite3"


def legacy_database_path() -> Path:
    return user_data_path("robocup-station", ensure_exists=False) / "station.sqlite3"


def migrate_legacy_database(
    destination: Path, *, legacy: Path | None = None
) -> bool:
    """Copy the previous project database once using SQLite's backup API."""
    generic = user_data_path("solar-station", ensure_exists=False) / "station.sqlite3"
    if legacy is None and destination != generic:
        return False
    source_path = legacy or legacy_database_path()
    if destination.exists() or not source_path.exists():
        return False
    destination.parent.mkdir(parents=True, exist_ok=True)
    with (
        sqlite3.connect(source_path) as source,
        sqlite3.connect(destination) as target,
    ):
        source.backup(target)
    return True


@dataclass(slots=True)
class StationConfig:
    socket_path: Path
    database_path: Path
    remote_target: str = "auto"
    console_target: str | None = "auto"
    manifest_cache: Path | None = None
    usb_vid: int | None = None
    usb_pid: int | None = None
    reconnect_initial: float = 0.25
    reconnect_maximum: float = 5.0
    request_timeout: float = 5.0
    maximum_ipc_message: int = 1 << 20
    client_event_queue: int = 256

    def __post_init__(self) -> None:
        if self.manifest_cache is None:
            self.manifest_cache = (
                user_cache_path("solar-station", ensure_exists=False) / "manifests"
            )

    @classmethod
    def defaults(cls) -> StationConfig:
        return cls(default_socket_path(), default_database_path())
