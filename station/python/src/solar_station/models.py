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
