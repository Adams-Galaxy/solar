"""Public extension contract for long-lived Station modules."""

from __future__ import annotations

from abc import ABC, abstractmethod
from collections.abc import Awaitable, Callable
from dataclasses import dataclass
from pathlib import Path
from typing import Any

PublishModuleEvent = Callable[[str, Any], Awaitable[None]]


@dataclass(frozen=True, slots=True)
class StationModuleContext:
    """Capabilities exposed to an installed Station module."""

    socket_path: Path
    publish_event: PublishModuleEvent


class StationModule(ABC):
    """A shared service hosted inside the Station daemon."""

    name: str
    version = "0.1.0"
    description = ""
    default_enabled = False

    @abstractmethod
    async def start(self, context: StationModuleContext) -> None: ...

    @abstractmethod
    async def close(self) -> None: ...

    @abstractmethod
    async def request(self, method: str, arguments: dict[str, Any]) -> Any: ...

    def status(self) -> dict[str, Any]:
        return {}
