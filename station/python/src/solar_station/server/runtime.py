"""Composition root for a persistent Solar Station host."""

from __future__ import annotations

import asyncio
import logging
import time
from pathlib import Path
from typing import Any

from ..config import StationConfig, migrate_legacy_database
from ..database import StationDatabase
from ..errors import StationError
from ..models import normalize
from .connection import SolarConnectionSupervisor
from .console import ConsoleConnectionSupervisor
from .events import EventHub
from .ipc import UnixSocketServer
from .recorder import Recorder
from .sources import SourceRegistry

LOGGER = logging.getLogger("solar_station.server")


class StationHost:
    def __init__(
        self,
        config: StationConfig,
        *,
        session_factory: Any = None,
    ):
        self.config = config
        self.database = StationDatabase(config.database_path)
        self.recorder = Recorder(self.database)
        self.events = EventHub(self.recorder)
        self.sources = SourceRegistry(self.database, self.events)
        self.connection = SolarConnectionSupervisor(
            config,
            self.database,
            self.events,
            self.sources,
            session_factory=session_factory,
        )
        self.console = ConsoleConnectionSupervisor(config, self.events, self.connection)
        self.ipc = UnixSocketServer(self)
        self._stop = asyncio.Event()
        self._started_ns = time.time_ns()
        self._closing = False

    async def __aenter__(self) -> StationHost:
        await self.start()
        return self

    async def __aexit__(self, *_: object) -> None:
        await self.close()

    async def start(self) -> None:
        if migrate_legacy_database(self.config.database_path):
            LOGGER.info("Migrated legacy database to %s", self.config.database_path)
        LOGGER.info(
            "Starting (socket=%s, database=%s)",
            self.config.socket_path,
            self.config.database_path,
        )
        try:
            await self.database.open()
            await self.recorder.initialize()
            await self.sources.initialize()
            await self.ipc.start()
            self.events.add_sink(self.ipc.broadcast)
            await self.connection.start()
            await self.console.start()
            await self.events.publish_server_state("started")
            LOGGER.info("Ready; accepting local clients")
        except BaseException:
            LOGGER.exception("Startup failed")
            await self._close(publish=False)
            raise

    async def run(self) -> None:
        async with self:
            await self.serve_forever()

    async def serve_forever(self) -> None:
        await self._stop.wait()

    def request_shutdown(self) -> None:
        self._stop.set()

    def hello_information(self) -> dict[str, Any]:
        manifest = self.connection.manifest
        return {
            "server_id": self.events.server_id,
            "robot_state": self.connection.state.value,
            "manifest_digest": manifest.digest if manifest else None,
            "live_sequence": self.events.live_sequence,
        }

    async def handle_request(self, operation: str, arguments: dict[str, Any]) -> Any:
        LOGGER.debug("Local request: %s", operation)
        handlers = {
            "status": self._status,
            "manifest": self._manifest,
            "get": self._get,
            "set": self._set,
            "call": self._call,
            "stream": self._stream,
            "streams": self._streams,
            "reconnect": self._reconnect,
            "logs": self._logs,
            "record_start": self._record_start,
            "record_stop": self._record_stop,
            "record_list": self._record_list,
            "record_show": self._record_show,
            "record_export": self._record_export,
            "shutdown": self._shutdown,
        }
        handler = handlers.get(operation)
        if handler is None:
            raise StationError(
                "invalid_request", f"unknown Station operation {operation!r}"
            )
        return normalize(await handler(arguments))

    async def _status(self, _: dict[str, Any]) -> dict[str, Any]:
        manifest = self.connection.manifest
        return {
            "server": {
                "id": self.events.server_id,
                "started_ns": self._started_ns,
                "socket": str(self.config.socket_path),
                "database": str(self.config.database_path),
                "clients": len(self.ipc.clients),
                "live_sequence": self.events.live_sequence,
            },
            "robot": {
                "state": self.connection.state.value,
                "target": self.connection.target,
                "build_id": self.connection.build_id,
                "manifest_digest": manifest.digest if manifest else None,
                "last_error": self.connection.last_error,
            },
            "console": {
                "connected": self.console.connected,
                "target": self.console.target,
                "last_error": self.console.last_error,
            },
            "sources": self.sources.list(),
            "active_recordings": [
                recording.to_wire()
                for recording in await self.database.recordings()
                if recording.active
            ],
        }

    async def _manifest(self, _: dict[str, Any]) -> dict[str, Any]:
        manifest = self.connection.manifest
        if manifest is None:
            raise StationError("robot_offline", "no active robot manifest")
        information = self.connection.session.server_information
        return {
            **manifest.to_dict(),
            "build_id": information.build_id if information else None,
            "manifest_size": len(manifest.image),
        }

    async def _get(self, arguments: dict[str, Any]) -> Any:
        endpoint = _endpoint_argument(arguments)
        session = self.connection.require_session()
        try:
            return await session.query(endpoint)
        except KeyError as error:
            raise StationError(
                "unknown_endpoint", f"queryable endpoint {endpoint!r} not found"
            ) from error
        except BaseException as error:
            raise _remote_error("get", endpoint, error) from error

    async def _set(self, arguments: dict[str, Any]) -> Any:
        endpoint = _endpoint_argument(arguments)
        if "value" not in arguments:
            raise StationError("invalid_value", "set requires a value")
        session = self.connection.require_session()
        try:
            await session.update(endpoint, arguments["value"])
            return {"endpoint": endpoint, "updated": True}
        except KeyError as error:
            raise StationError(
                "unknown_endpoint", f"writable endpoint {endpoint!r} not found"
            ) from error
        except BaseException as error:
            raise _remote_error("set", endpoint, error) from error

    async def _call(self, arguments: dict[str, Any]) -> Any:
        endpoint = _endpoint_argument(arguments)
        session = self.connection.require_session()
        try:
            return await session.action(endpoint, arguments.get("value"))
        except KeyError as error:
            raise StationError(
                "unknown_endpoint", f"action endpoint {endpoint!r} not found"
            ) from error
        except BaseException as error:
            raise _remote_error("call", endpoint, error) from error

    async def _stream(self, arguments: dict[str, Any]) -> Any:
        endpoint = _endpoint_argument(arguments)
        return await self.sources.configure(
            endpoint,
            frequency=_optional_float(arguments.get("frequency"), "frequency"),
            batch=_integer(arguments.get("batch", 1), "batch"),
            stop=bool(arguments.get("stop", False)),
        )

    async def _streams(self, _: dict[str, Any]) -> list[dict[str, Any]]:
        return self.sources.list()

    async def _reconnect(self, arguments: dict[str, Any]) -> dict[str, Any]:
        LOGGER.info("Robot reconnect requested by a client")
        timeout = _optional_float(arguments.get("timeout"), "timeout")
        return await self.connection.reconnect(10.0 if timeout is None else timeout)

    async def _logs(self, arguments: dict[str, Any]) -> list[dict[str, Any]]:
        head = arguments.get("head")
        tail = arguments.get("tail")
        return await self.database.query_events(
            source_key="logs.console",
            first=_optional_integer(head, "head"),
            last=_optional_integer(tail, "tail"),
        )

    async def _record_start(self, arguments: dict[str, Any]) -> dict[str, Any]:
        name = str(arguments.get("name", ""))
        recording = await self.recorder.start(name)
        await self.events.publish_server_state(
            "recording_started", name=recording.name, persist=False
        )
        return recording.to_wire()

    async def _record_stop(self, arguments: dict[str, Any]) -> dict[str, Any]:
        raw_name = arguments.get("name")
        name = None if raw_name is None else str(raw_name)
        recording = await self.recorder.stop(name)
        await self.events.publish_server_state(
            "recording_stopped", name=recording.name, persist=False
        )
        return recording.to_wire()

    async def _record_list(self, _: dict[str, Any]) -> list[dict[str, Any]]:
        return [item.to_wire() for item in await self.recorder.list()]

    async def _record_show(self, arguments: dict[str, Any]) -> dict[str, Any]:
        name = str(arguments.get("name", ""))
        matches = await self.recorder.list(name)
        if not matches:
            raise StationError("recording_not_found", f"recording {name!r} not found")
        recording, events = await self.database.recording_events(name)
        return {**recording.to_wire(), "events": len(events)}

    async def _record_export(self, arguments: dict[str, Any]) -> dict[str, Any]:
        name = str(arguments.get("name", ""))
        format_name = str(arguments.get("format", "jsonl"))
        raw_output = arguments.get("output")
        output = (
            Path(str(raw_output)).expanduser()
            if raw_output
            else Path.cwd() / f"{name}.{_extension(format_name)}"
        )
        return await self.recorder.export(name, format_name, output)

    async def _shutdown(self, _: dict[str, Any]) -> dict[str, Any]:
        LOGGER.info("Shutdown requested by a client")
        asyncio.get_running_loop().call_later(0.05, self.request_shutdown)
        return {"stopping": True}

    async def close(self) -> None:
        await self._close(publish=True)

    async def _close(self, *, publish: bool) -> None:
        if self._closing:
            return
        self._closing = True
        LOGGER.info("Stopping")
        if publish and self.database.is_open:
            await self.events.publish_server_state("stopping")
        await self.ipc.close()
        self.events.remove_sink(self.ipc.broadcast)
        await self.console.close()
        await self.connection.close()
        await self.sources.close()
        await self.database.close()
        LOGGER.info("Stopped cleanly")


def _endpoint_argument(arguments: dict[str, Any]) -> int | str:
    endpoint = arguments.get("endpoint")
    if isinstance(endpoint, (str, int)) and not isinstance(endpoint, bool):
        return endpoint
    raise StationError("invalid_request", "endpoint must be a name or stable ID")


def _integer(value: Any, name: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise StationError("invalid_value", f"{name} must be an integer")
    return value


def _optional_integer(value: Any, name: str) -> int | None:
    return None if value is None else _integer(value, name)


def _optional_float(value: Any, name: str) -> float | None:
    if value is None:
        return None
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise StationError("invalid_value", f"{name} must be numeric")
    return float(value)


def _remote_error(
    operation: str, endpoint: int | str, error: BaseException
) -> StationError:
    details: dict[str, Any] = {"type": type(error).__name__}
    if hasattr(error, "code"):
        details["code"] = error.code
    if hasattr(error, "value"):
        details["value"] = normalize(error.value)
    return StationError(
        "remote_error",
        f"{operation} {endpoint!r} failed: {error}",
        details,
    )


def _extension(format_name: str) -> str:
    return {"jsonl": "jsonl", "json": "json", "text": "txt", "cbor": "cbor"}.get(
        format_name, format_name
    )
