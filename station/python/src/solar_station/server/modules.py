"""Discovery and lifecycle management for installed Station modules."""

from __future__ import annotations

import logging
from collections.abc import Callable
from importlib.metadata import entry_points
from typing import Any

from ..errors import StationError
from ..models import normalize
from ..modules import StationModule, StationModuleContext

LOGGER = logging.getLogger("solar_station.modules")
ModuleFactory = Callable[[], StationModule]


class ModuleManager:
    def __init__(
        self,
        host: Any,
        factories: dict[str, ModuleFactory] | None = None,
    ):
        self.host = host
        self.factories = self._discover() if factories is None else factories
        self.instances: dict[str, StationModule] = {}
        self.errors: dict[str, str] = {}

    @staticmethod
    def _discover() -> dict[str, ModuleFactory]:
        discovered: dict[str, ModuleFactory] = {}
        for entry in entry_points(group="solar_station.modules"):
            try:
                factory = entry.load()
                discovered[entry.name] = factory
            except Exception as error:
                LOGGER.warning("Could not load module %s: %s", entry.name, error)
        return discovered

    async def start(self) -> None:
        for name, factory in self.factories.items():
            if bool(getattr(factory, "default_enabled", False)):
                try:
                    await self.enable(name)
                except Exception as error:
                    self.errors[name] = str(error)
                    LOGGER.exception("Could not enable default module %s", name)

    async def enable(self, name: str) -> dict[str, Any]:
        if name in self.instances:
            return self.describe(name)
        factory = self.factories.get(name)
        if factory is None:
            raise StationError(
                "module_not_found", f"Station module {name!r} is not installed"
            )
        try:
            instance = factory()
            if instance.name != name:
                raise ValueError(
                    f"module entry point {name!r} identifies itself as "
                    f"{instance.name!r}"
                )
            context = StationModuleContext(
                socket_path=self.host.config.socket_path,
                publish_event=lambda event, value: self.publish(
                    name, event, value
                ),
            )
            await instance.start(context)
        except BaseException as error:
            self.errors[name] = str(error)
            raise StationError(
                "module_start_failed",
                f"could not start Station module {name!r}: {error}",
            ) from error
        self.instances[name] = instance
        self.errors.pop(name, None)
        LOGGER.info("Enabled module %s", name)
        await self.publish(name, "enabled", instance.status())
        return self.describe(name)

    async def disable(self, name: str) -> dict[str, Any]:
        instance = self.instances.pop(name, None)
        if instance is None:
            if name not in self.factories:
                raise StationError(
                    "module_not_found", f"Station module {name!r} is not installed"
                )
            return self.describe(name)
        try:
            await instance.close()
        finally:
            LOGGER.info("Disabled module %s", name)
            await self.publish(name, "disabled", {})
        return self.describe(name)

    async def request(
        self, name: str, method: str, arguments: dict[str, Any]
    ) -> Any:
        instance = self.instances.get(name)
        if instance is None:
            if name in self.factories:
                raise StationError(
                    "module_disabled", f"Station module {name!r} is disabled"
                )
            raise StationError(
                "module_not_found", f"Station module {name!r} is not installed"
            )
        try:
            return normalize(await instance.request(method, arguments))
        except StationError:
            raise
        except BaseException as error:
            raise StationError(
                "module_error",
                f"{name}.{method} failed: {error}",
                {"type": type(error).__name__},
            ) from error

    def list(self) -> list[dict[str, Any]]:
        return [self.describe(name) for name in sorted(self.factories)]

    def describe(self, name: str) -> dict[str, Any]:
        factory = self.factories.get(name)
        if factory is None:
            raise StationError(
                "module_not_found", f"Station module {name!r} is not installed"
            )
        instance = self.instances.get(name)
        target = instance or factory
        return {
            "name": name,
            "version": str(getattr(target, "version", "0.0.0")),
            "description": str(getattr(target, "description", "")),
            "enabled": instance is not None,
            "source": self.source(name),
            "error": self.errors.get(name),
            "status": normalize(instance.status()) if instance is not None else {},
        }

    def validate_subscription(self, source: str) -> str | None:
        prefix = "module."
        if not source.startswith(prefix):
            return None
        name = source[len(prefix) :]
        if name not in self.factories:
            raise StationError(
                "module_not_found", f"Station module {name!r} is not installed"
            )
        return source

    @staticmethod
    def source(name: str) -> str:
        return f"module.{name}"

    async def publish(self, name: str, event: str, value: Any) -> None:
        await self.host.events.publish(
            self.source(name),
            "module",
            {"event": event, "module": name, "state": normalize(value)},
            persist=False,
        )

    async def close(self) -> None:
        for name in tuple(reversed(self.instances)):
            try:
                await self.disable(name)
            except Exception:
                LOGGER.exception("Could not stop module %s", name)
