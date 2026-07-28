"""Ownership and lifecycle for Station-managed robot input streams."""

from __future__ import annotations

import asyncio
import itertools
from dataclasses import dataclass
from typing import Any

from ..errors import StationError


@dataclass(slots=True)
class InputHandle:
    id: int
    owner: object
    owner_name: str
    endpoint: int | str
    stream: Any

    def to_wire(self) -> dict[str, Any]:
        policy = self.stream.effective
        return {
            "handle": self.id,
            "endpoint": self.endpoint,
            "target": self.stream.target,
            "token": self.stream.token,
            "owner": self.owner_name,
            "minimum_interval_us": (
                policy.minimum_interval_us if policy is not None else None
            ),
            "window": self.stream.credit_window,
            "closed": self.stream.closed,
            "closure_reason": self.stream.closure_reason,
        }


class InputRegistry:
    """Owns the single set of robot producers and their local client handles."""

    def __init__(self, events: Any = None) -> None:
        self.events = events
        self._ids = itertools.count(1)
        self._handles: dict[int, InputHandle] = {}
        self._watchers: dict[int, asyncio.Task[None]] = {}
        self._lock = asyncio.Lock()

    async def open(
        self,
        owner: object,
        owner_name: str,
        session: Any,
        endpoint: int | str,
        *,
        frequency: float | None,
        credit_timeout: float | None,
    ) -> dict[str, Any]:
        stream = session.in_stream(
            endpoint,
            frequency=frequency,
            credit_timeout=credit_timeout,
        )
        try:
            await stream.open()
        except BaseException as error:
            raise StationError(
                "input_open_failed",
                f"could not open input {endpoint!r}: {error}",
                {"type": type(error).__name__},
            ) from error
        async with self._lock:
            handle = InputHandle(
                next(self._ids), owner, owner_name, endpoint, stream
            )
            self._handles[handle.id] = handle
            self._watchers[handle.id] = asyncio.create_task(
                self._watch_closed(handle), name=f"station-input:{handle.id}"
            )
        return handle.to_wire()

    async def send(
        self,
        owner: object,
        handle_id: int,
        value: Any,
        *,
        timeout: float | None,  # noqa: ASYNC109
    ) -> dict[str, Any]:
        handle = await self._owned(owner, handle_id)
        try:
            await handle.stream.send(value, timeout=timeout)
        except BaseException as error:
            raise StationError(
                "input_send_failed",
                f"input handle {handle_id} send failed: {error}",
                {
                    "type": type(error).__name__,
                    "closure_reason": handle.stream.closure_reason,
                },
            ) from error
        return {
            "handle": handle_id,
            "sent": True,
            "token": handle.stream.token,
        }

    async def close(self, owner: object, handle_id: int) -> dict[str, Any]:
        handle = await self._owned(owner, handle_id)
        async with self._lock:
            self._handles.pop(handle_id, None)
            watcher = self._watchers.pop(handle_id, None)
        if watcher is not None:
            watcher.cancel()
            await asyncio.gather(watcher, return_exceptions=True)
        await handle.stream.aclose()
        return {"handle": handle_id, "closed": True}

    async def list(self, owner: object) -> list[dict[str, Any]]:
        async with self._lock:
            return [
                handle.to_wire()
                for handle in self._handles.values()
                if handle.owner is owner
            ]

    async def close_owner(self, owner: object) -> None:
        async with self._lock:
            handles = [
                handle for handle in self._handles.values() if handle.owner is owner
            ]
            watchers: list[asyncio.Task[None]] = []
            for handle in handles:
                self._handles.pop(handle.id, None)
                watcher = self._watchers.pop(handle.id, None)
                if watcher is not None:
                    watcher.cancel()
                    watchers.append(watcher)
        if watchers:
            await asyncio.gather(*watchers, return_exceptions=True)
        await asyncio.gather(
            *(handle.stream.aclose() for handle in handles),
            return_exceptions=True,
        )

    async def offline(self) -> None:
        async with self._lock:
            handles = list(self._handles.values())
            self._handles.clear()
            watchers = list(self._watchers.values())
            self._watchers.clear()
        for watcher in watchers:
            watcher.cancel()
        if watchers:
            await asyncio.gather(*watchers, return_exceptions=True)
        await asyncio.gather(
            *(handle.stream.aclose() for handle in handles),
            return_exceptions=True,
        )
        if self.events is not None:
            for handle in handles:
                await self._publish_closed(
                    handle,
                    handle.stream.closure_reason
                    if handle.stream.closure_reason is not None
                    else 2,
                )

    async def _owned(self, owner: object, handle_id: int) -> InputHandle:
        async with self._lock:
            handle = self._handles.get(handle_id)
            if handle is None:
                raise StationError(
                    "input_not_found", f"input handle {handle_id} does not exist"
                )
            if handle.owner is not owner:
                raise StationError(
                    "input_not_owned",
                    f"input handle {handle_id} belongs to another local client",
                )
            return handle

    async def _watch_closed(self, handle: InputHandle) -> None:
        try:
            reason = await handle.stream.wait_closed()
            async with self._lock:
                if self._handles.get(handle.id) is not handle:
                    return
                self._handles.pop(handle.id, None)
                self._watchers.pop(handle.id, None)
            await self._publish_closed(handle, reason)
        except asyncio.CancelledError:
            raise

    async def _publish_closed(
        self, handle: InputHandle, reason: int | None
    ) -> None:
        if self.events is not None:
            await self.events.publish_server_state(
                "input_closed",
                handle=handle.id,
                endpoint=handle.endpoint,
                owner=handle.owner_name,
                reason=reason,
                persist=False,
            )
