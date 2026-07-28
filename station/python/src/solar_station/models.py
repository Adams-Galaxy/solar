"""Solar Station transport-neutral runtime models."""

from __future__ import annotations

from collections.abc import Mapping
from dataclasses import asdict, dataclass, is_dataclass
from enum import Enum
from pathlib import Path
from typing import Any


class RobotState(str, Enum):
    DISCONNECTED = "disconnected"
    CONNECTING = "connecting"
    RESOLVING_MANIFEST = "resolving_manifest"
    RESTORING_SOURCES = "restoring_sources"
    ONLINE = "online"
    STOPPING = "stopping"


class SourceState(str, Enum):
    INACTIVE = "inactive"
    STARTING = "starting"
    ACTIVE = "active"
    UNAVAILABLE = "unavailable"
    FAILED = "failed"


class SourceKind(str, Enum):
    DATA_WATCH = "data_watch"
    DATA_STREAM = "data_stream"
    STREAM = "stream"
    TOPIC = "topic"
    LOG = "log"
    SERVER = "server"


@dataclass(frozen=True, slots=True)
class ConnectionStatus:
    state: RobotState
    target: str | None
    build_id: int | None
    manifest_digest: bytes | None
    last_error: str | None

    @property
    def transport(self) -> str | None:
        if self.target is None:
            return None
        return self.target.partition("://")[0] or None

    @classmethod
    def from_wire(cls, value: Mapping[str, Any]) -> ConnectionStatus:
        return cls(
            state=RobotState(str(value.get("state", "disconnected"))),
            target=_optional_str(value.get("target")),
            build_id=_optional_int(value.get("build_id")),
            manifest_digest=(
                bytes(value["manifest_digest"])
                if value.get("manifest_digest") is not None
                else None
            ),
            last_error=_optional_str(value.get("last_error")),
        )


@dataclass(frozen=True, slots=True)
class SourceInformation:
    endpoint_name: str
    endpoint_id: int | None
    kind: SourceKind
    state: SourceState
    desired: bool
    frequency: float | None
    batch: int
    loss_count: int
    last_error: str | None

    @classmethod
    def from_wire(cls, value: Mapping[str, Any]) -> SourceInformation:
        return cls(
            endpoint_name=str(value["endpoint_name"]),
            endpoint_id=_optional_int(value.get("endpoint_id")),
            kind=SourceKind(str(value["kind"])),
            state=SourceState(str(value["state"])),
            desired=bool(value.get("desired", False)),
            frequency=(
                float(value["frequency"])
                if value.get("frequency") is not None
                else None
            ),
            batch=int(value.get("batch", 1)),
            loss_count=int(value.get("loss_count", 0)),
            last_error=_optional_str(value.get("last_error")),
        )


@dataclass(slots=True)
class SourceConfig:
    key: str
    endpoint_id: int | None
    endpoint_name: str
    kind: SourceKind
    desired: bool = False
    frequency: float | None = None
    batch: int = 1
    manifest_digest: bytes | None = None

    def to_wire(self) -> dict[str, Any]:
        value = asdict(self)
        value["kind"] = self.kind.value
        return value


@dataclass(slots=True)
class SourceStatus:
    config: SourceConfig
    state: SourceState = SourceState.INACTIVE
    negotiated_frequency: float | None = None
    negotiated_batch: int | None = None
    last_error: str | None = None
    last_live_sequence: int | None = None
    last_wall_ns: int | None = None
    loss_count: int = 0

    def to_wire(self) -> dict[str, Any]:
        return {
            **self.config.to_wire(),
            "state": self.state.value,
            "negotiated_frequency": self.negotiated_frequency,
            "negotiated_batch": self.negotiated_batch,
            "last_error": self.last_error,
            "last_live_sequence": self.last_live_sequence,
            "last_wall_ns": self.last_wall_ns,
            "loss_count": self.loss_count,
        }


@dataclass(slots=True)
class StationEvent:
    server_id: str
    live_sequence: int
    wall_ns: int
    monotonic_ns: int
    source_key: str
    source_kind: str
    value: Any
    endpoint_id: int | None = None
    endpoint_name: str | None = None
    schema_id: int | None = None
    manifest_digest: bytes | None = None
    source_loss_count: int = 0
    stored_event_id: int | None = None

    def to_wire(self) -> dict[str, Any]:
        return normalize(
            {
                "server_id": self.server_id,
                "live_sequence": self.live_sequence,
                "stored_event_id": self.stored_event_id,
                "wall_ns": self.wall_ns,
                "monotonic_ns": self.monotonic_ns,
                "source": self.source_key,
                "source_kind": self.source_kind,
                "endpoint_id": self.endpoint_id,
                "endpoint_name": self.endpoint_name,
                "schema_id": self.schema_id,
                "manifest_digest": self.manifest_digest,
                "value": self.value,
                "source_loss_count": self.source_loss_count,
            }
        )


@dataclass(frozen=True, slots=True)
class PingResult:
    round_trip_ns: int
    remote_processing_ns: int
    station_round_trip_ns: int | None = None

    @property
    def round_trip_ms(self) -> float:
        return self.round_trip_ns / 1_000_000

    @property
    def remote_processing_ms(self) -> float:
        return self.remote_processing_ns / 1_000_000


@dataclass(frozen=True, slots=True)
class RecordingInformation:
    name: str
    active: bool
    started_ns: int
    ended_ns: int | None

    @classmethod
    def from_wire(cls, value: Mapping[str, Any]) -> RecordingInformation:
        return cls(
            name=str(value["name"]),
            active=bool(value["active"]),
            started_ns=int(value["started_ns"]),
            ended_ns=_optional_int(value.get("ended_ns")),
        )


@dataclass(slots=True)
class Recording:
    id: int
    name: str
    start_after_event_id: int
    end_event_id: int | None
    started_ns: int
    ended_ns: int | None

    @property
    def active(self) -> bool:
        return self.end_event_id is None

    def to_wire(self) -> dict[str, Any]:
        return {**asdict(self), "active": self.active}


def normalize(value: Any) -> Any:
    """Convert SDK and application values into CBOR-safe primitives."""
    if value is None or isinstance(value, (bool, int, float, str, bytes)):
        return value
    if isinstance(value, Path):
        return str(value)
    if isinstance(value, Enum):
        return normalize(value.value)
    if is_dataclass(value):
        return normalize(asdict(value))
    if isinstance(value, Mapping):
        return {str(key): normalize(item) for key, item in value.items()}
    if isinstance(value, (list, tuple, set)):
        return [normalize(item) for item in value]
    if hasattr(value, "value") and isinstance(value.value, int):
        return int(value.value)
    return {"$unrepresentable": type(value).__name__, "text": repr(value)}


def _optional_int(value: Any) -> int | None:
    return int(value) if value is not None else None


def _optional_str(value: Any) -> str | None:
    return str(value) if value is not None else None
