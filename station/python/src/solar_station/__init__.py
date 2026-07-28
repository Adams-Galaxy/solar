"""Persistent host and client APIs for Solar Remote."""

from .client import EventSubscription, StationClient
from .config import StationConfig
from .errors import ProtocolError, StationError
from .models import (
    ConnectionStatus,
    PingResult,
    RecordingInformation,
    SourceInformation,
)
from .modules import StationModule, StationModuleContext
from .resources import (
    ConnectionResource,
    InputEndpoint,
    InputProducer,
    LogsResource,
    RecordingsResource,
    RobotResource,
    Source,
    SourceCollection,
    StationFrame,
    TypedSubscription,
)
from .server import StationHost
from .server.discovery import (
    BridgeStatus,
    DiscoverySnapshot,
    TransportDiscovery,
    probe_bridge,
)

__all__ = [
    "EventSubscription",
    "BridgeStatus",
    "DiscoverySnapshot",
    "ProtocolError",
    "PingResult",
    "ConnectionStatus",
    "SourceInformation",
    "RecordingInformation",
    "StationClient",
    "StationConfig",
    "StationError",
    "StationHost",
    "StationModule",
    "StationModuleContext",
    "StationFrame",
    "TypedSubscription",
    "RobotResource",
    "Source",
    "SourceCollection",
    "InputEndpoint",
    "InputProducer",
    "LogsResource",
    "RecordingsResource",
    "ConnectionResource",
    "TransportDiscovery",
    "probe_bridge",
]
__version__ = "0.2.0"
