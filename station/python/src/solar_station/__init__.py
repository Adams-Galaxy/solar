"""Persistent host and client APIs for Solar Remote."""

from .client import EventSubscription, StationClient
from .config import StationConfig
from .errors import ProtocolError, StationError
from .server import StationHost

__all__ = [
    "EventSubscription",
    "ProtocolError",
    "StationClient",
    "StationConfig",
    "StationError",
    "StationHost",
]
__version__ = "0.1.0"
