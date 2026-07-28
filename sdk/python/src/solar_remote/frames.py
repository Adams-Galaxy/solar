"""Public continuous-delivery types."""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
from typing import Generic, TypeVar

from .descriptors import EndpointDescriptor


T = TypeVar("T")


class QueuePolicy(str, Enum):
    DROP_OLDEST = "drop_oldest"
    DROP_NEWEST = "drop_newest"
    DISCONNECT = "disconnect"
    BLOCK = "block"


@dataclass(frozen=True, slots=True)
class Frame(Generic[T]):
    value: T
    endpoint: EndpointDescriptor
    sequence: int
    received_monotonic_ns: int
    received_wall_ns: int
    session_id: str
    loss_count: int = 0
