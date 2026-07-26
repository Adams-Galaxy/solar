"""Solar Remote connection supervision and manifest restoration."""

from __future__ import annotations

import asyncio
import logging
from collections.abc import Callable
from pathlib import Path
from typing import Any

from ..config import StationConfig
from ..database import StationDatabase
from ..errors import StationError
from ..models import RobotState
from .events import EventHub
from .sources import SourceRegistry

LOGGER = logging.getLogger("solar_station.remote")


class SolarConnectionSupervisor:
    def __init__(
        self,
        config: StationConfig,
        database: StationDatabase,
        events: EventHub,
        sources: SourceRegistry,
        *,
        session_factory: Callable[[str, Path | None, float], Any] | None = None,
        inputs: Any = None,
    ):
        self.config = config
        self.database = database
        self.events = events
        self.sources = sources
        self.session_factory = session_factory or self._default_session_factory
        self.inputs = inputs
        self.state = RobotState.DISCONNECTED
        self.target: str | None = None
        self.session: Any = None
        self.manifest: Any = None
        self.build_id: int | None = None
        self.session_id: int | None = None
        self.last_error: str | None = None
        self._task: asyncio.Task[None] | None = None
        self._stop = asyncio.Event()
        self._reconnect = asyncio.Event()
        self._state_changed = asyncio.Condition()
        self._online_generation = 0
        self._logged_error: str | None = None

    @staticmethod
    def _default_session_factory(
        target: str, cache: Path | None, timeout: float
    ) -> Any:
        from solar_remote import connect

        return connect(target, cache=cache, timeout=timeout)

    async def start(self) -> None:
        self._task = asyncio.create_task(self._run(), name="station-solar-connection")

    async def request_reconnect(self) -> None:
        LOGGER.info("Reconnect requested")
        self._reconnect.set()
        if self.session is not None:
            await self.session.close()

    async def reconnect(self, wait_seconds: float) -> dict[str, Any]:
        if wait_seconds <= 0:
            raise StationError("invalid_value", "reconnect timeout must be positive")
        generation = self._online_generation
        await self.request_reconnect()
        try:
            async with asyncio.timeout(wait_seconds):
                async with self._state_changed:
                    await self._state_changed.wait_for(
                        lambda: self._online_generation > generation
                        or self._stop.is_set()
                    )
        except TimeoutError as error:
            raise StationError(
                "request_timeout",
                f"robot did not reconnect within {wait_seconds:g} seconds",
                {
                    "state": self.state.value,
                    "target": self.target,
                    "last_error": self.last_error,
                },
            ) from error
        if self._stop.is_set():
            raise StationError("server_unavailable", "Station server is stopping")
        return {
            "connected": True,
            "state": self.state.value,
            "target": self.target,
            "build_id": self.build_id,
        }

    async def _set_state(self, state: RobotState, **details: Any) -> None:
        self.state = state
        if state == RobotState.ONLINE:
            self._online_generation += 1
        if state == RobotState.ONLINE:
            LOGGER.info(
                "Connected to %s (build=%s, manifest=%s)",
                details.get("target", self.target),
                details.get("build_id"),
                _digest_text(details.get("manifest_digest")),
            )
        elif state in (RobotState.CONNECTING, RobotState.RESOLVING_MANIFEST):
            LOGGER.debug("State: %s", state.value)
        await self.events.publish_server_state(state.value, **details)
        async with self._state_changed:
            self._state_changed.notify_all()

    async def _resolve_target(self) -> str:
        if self.config.remote_target != "auto":
            return self.config.remote_target
        from .discovery import discover_remote_target

        target = await discover_remote_target(
            vid=self.config.usb_vid, pid=self.config.usb_pid
        )
        if target is None:
            raise StationError(
                "robot_offline", "no Solar Remote USB device or simulator found"
            )
        return target

    async def _run(self) -> None:
        delay = self.config.reconnect_initial
        while not self._stop.is_set():
            self._reconnect.clear()
            reason = "connection closed"
            try:
                await self._set_state(RobotState.CONNECTING)
                target = await self._resolve_target()
                self.target = target
                session = self.session_factory(
                    target, self.config.manifest_cache, self.config.request_timeout
                )
                self.session = session
                await session.open()
                await self._set_state(RobotState.RESOLVING_MANIFEST, target=target)
                if session.manifest is None:
                    raise StationError(
                        "manifest_mismatch",
                        "connected firmware did not provide a Remote manifest",
                    )
                self.manifest = session.manifest
                information = session.server_information
                self.build_id = information.build_id if information else None
                await self.database.store_manifest(
                    self.manifest.digest, self.manifest.image
                )
                self.session_id = await self.database.begin_robot_session(
                    target, self.build_id, self.manifest.digest
                )
                self.events.robot_session_id = self.session_id
                await self._set_state(
                    RobotState.RESTORING_SOURCES,
                    target=target,
                    build_id=self.build_id,
                    manifest_digest=self.manifest.digest,
                )
                await self.sources.online(session, self.manifest)
                await self._set_state(
                    RobotState.ONLINE,
                    target=target,
                    build_id=self.build_id,
                    manifest_digest=self.manifest.digest,
                )
                self.last_error = None
                self._logged_error = None
                delay = self.config.reconnect_initial
                stop_wait = asyncio.create_task(self._stop.wait())
                reconnect_wait = asyncio.create_task(self._reconnect.wait())
                owned_waiters: set[asyncio.Task[Any]] = {stop_wait, reconnect_wait}
                waiters: set[asyncio.Future[Any]] = set(owned_waiters)
                reader_task = getattr(session, "_reader", None)
                if isinstance(reader_task, asyncio.Future):
                    waiters.add(reader_task)
                await asyncio.wait(waiters, return_when=asyncio.FIRST_COMPLETED)
                for waiter in owned_waiters:
                    waiter.cancel()
                await asyncio.gather(*owned_waiters, return_exceptions=True)
                reason = (
                    "reconnect requested"
                    if self._reconnect.is_set()
                    else "Remote transport disconnected"
                )
            except asyncio.CancelledError:
                reason = "server stopping"
                raise
            except BaseException as error:
                reason = str(error) or type(error).__name__
                self.last_error = reason
                if reason != self._logged_error:
                    LOGGER.warning("Connection unavailable: %s", reason)
                    self._logged_error = reason
                else:
                    LOGGER.debug("Connection still unavailable: %s", reason)
            finally:
                if self.inputs is not None:
                    await self.inputs.offline()
                await self.sources.offline(reason)
                if self.session is not None:
                    try:
                        await self.session.close()
                    except BaseException:
                        pass
                self.session = None
                self.manifest = None
                self.build_id = None
                if self.session_id is not None:
                    await self.database.end_robot_session(self.session_id, reason)
                self.session_id = None
                self.events.robot_session_id = None
                if not self._stop.is_set():
                    if reason in (
                        "reconnect requested",
                        "Remote transport disconnected",
                    ):
                        LOGGER.warning("%s", reason)
                    await self._set_state(RobotState.DISCONNECTED, reason=reason)
            if not self._stop.is_set():
                stop_wait = asyncio.create_task(self._stop.wait())
                reconnect_wait = asyncio.create_task(self._reconnect.wait())
                done, pending = await asyncio.wait(
                    {stop_wait, reconnect_wait},
                    timeout=delay,
                    return_when=asyncio.FIRST_COMPLETED,
                )
                del done
                for waiter in pending:
                    waiter.cancel()
                await asyncio.gather(*pending, return_exceptions=True)
                delay = min(self.config.reconnect_maximum, delay * 2)

    def require_session(self) -> Any:
        if self.session is None or self.state != RobotState.ONLINE:
            raise StationError("robot_offline", "robot is not connected")
        return self.session

    async def close(self) -> None:
        self._stop.set()
        self._reconnect.set()
        if self.session is not None:
            try:
                await self.session.close()
            except BaseException:
                pass
        task, self._task = self._task, None
        if task is not None:
            task.cancel()
            try:
                await task
            except asyncio.CancelledError:
                pass
        self.state = RobotState.STOPPING


def _digest_text(value: Any) -> str:
    if isinstance(value, bytes):
        return value.hex()[:12]
    return str(value) if value is not None else "unknown"
