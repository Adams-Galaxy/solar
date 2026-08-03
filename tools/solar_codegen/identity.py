"""Stable identity allocation and evolution history."""

from __future__ import annotations

from typing import Any

from .diagnostics import CompileError
from .versions import LOCK_FORMAT


def _fnv1a32(value: str) -> int:
    result = 0x811C9DC5
    for byte in value.encode("utf-8"):
        result ^= byte
        result = (result * 0x01000193) & 0xFFFFFFFF
    return result or 1


class IdentityAllocator:
    def __init__(self, previous: dict[str, Any]):
        self.previous = previous
        self.entries: dict[str, dict[str, Any]] = {}
        self.used: dict[int, str] = {}
        self.declaration_changes: list[dict[str, Any]] = []
        self.scoped_entries: dict[str, dict[str, dict[str, Any]]] = {}
        self.scoped_used: dict[str, dict[int, str]] = {}
        for key, item in previous.get("entries", {}).items():
            stable_id = int(item["id"])
            if stable_id in self.used:
                raise CompileError(f"lock file reuses ID 0x{stable_id:08X}")
            self.used[stable_id] = key
        for key, stable_id_value in previous.get("retired", {}).items():
            stable_id = int(stable_id_value)
            if stable_id in self.used:
                raise CompileError(f"lock file reuses retired ID 0x{stable_id:08X}")
            self.used[stable_id] = f"retired:{key}"
        for scope, state in previous.get("scoped", {}).items():
            used: dict[int, str] = {}
            for key, item in state.get("entries", {}).items():
                stable_id = int(item["id"])
                if stable_id in used:
                    raise CompileError(f"lock scope {scope!r} reuses ID {stable_id}")
                used[stable_id] = key
            for key, stable_id_value in state.get("retired", {}).items():
                stable_id = int(stable_id_value)
                if stable_id in used:
                    raise CompileError(f"lock scope {scope!r} reuses retired ID {stable_id}")
                used[stable_id] = f"retired:{key}"
            self.scoped_used[scope] = used

    def allocate(self, key: str, fingerprint: str, *, explicit: int | None = None,
                 renamed_from: str | None = None) -> tuple[int, str | None]:
        prior_key = renamed_from if renamed_from else key
        prior = self.previous.get("entries", {}).get(prior_key)
        if renamed_from is not None and prior is None:
            raise CompileError(f"rename source {renamed_from!r} does not exist in the interface lock")
        if prior is not None:
            stable_id = int(prior["id"])
            if explicit is not None and explicit != stable_id:
                raise CompileError(
                    f"explicit ID 0x{explicit:08X} conflicts with locked ID 0x{stable_id:08X}")
            change = "renamed" if prior_key != key else None
        else:
            stable_id = explicit if explicit is not None else _fnv1a32(key)
            change = "added"
            if explicit is None:
                while stable_id in self.used and self.used[stable_id] != key:
                    stable_id = (stable_id + 1) & 0xFFFFFFFF or 1
        owner = self.used.get(stable_id)
        if owner not in (None, key, prior_key):
            raise CompileError(f"identity collision: {key} and {owner} use 0x{stable_id:08X}")
        self.used[stable_id] = key
        self.entries[key] = {"id": stable_id, "fingerprint": fingerprint}
        if change == "renamed":
            self.declaration_changes.append(
                {"kind": "compatible", "declaration": key, "renamed_from": prior_key})
        return stable_id, change

    def allocate_scoped(self, scope: str, key: str, fingerprint: str, *,
                        explicit: int | None = None, renamed_from: str | None = None,
                        maximum: int) -> int:
        state = self.previous.get("scoped", {}).get(scope, {})
        entries = state.get("entries", {})
        prior_key = renamed_from or key
        prior = entries.get(prior_key)
        if renamed_from is not None and prior is None:
            raise CompileError(f"rename source {scope}.{renamed_from!r} does not exist in the interface lock")
        if prior is not None:
            stable_id = int(prior["id"])
            if explicit is not None and explicit != stable_id:
                raise CompileError(
                    f"explicit scoped ID {explicit} conflicts with locked ID {stable_id}")
            if prior_key != key:
                self.declaration_changes.append(
                    {"kind": "compatible", "declaration": f"{scope}:{key}",
                     "renamed_from": prior_key})
        else:
            stable_id = explicit if explicit is not None else (_fnv1a32(f"{scope}:{key}") % maximum) + 1
        if not 1 <= stable_id <= maximum:
            raise CompileError(f"scoped ID must be from 1 through {maximum}")
        used = self.scoped_used.setdefault(scope, {})
        owner = used.get(stable_id)
        if owner not in (None, key, prior_key):
            if explicit is not None:
                raise CompileError(f"identity collision: {scope}.{key} and {owner} use {stable_id}")
            while stable_id in used and used[stable_id] not in (key, prior_key):
                stable_id = stable_id % maximum + 1
        used[stable_id] = key
        self.scoped_entries.setdefault(scope, {})[key] = {
            "id": stable_id, "fingerprint": fingerprint}
        return stable_id

    def finish(self) -> tuple[dict[str, Any], list[dict[str, Any]]]:
        changes: list[dict[str, Any]] = list(self.declaration_changes)
        previous_entries = self.previous.get("entries", {})
        for key, item in self.entries.items():
            previous = previous_entries.get(key)
            if previous is None:
                if not any(int(old["id"]) == int(item["id"]) for old in previous_entries.values()):
                    changes.append({"kind": "additive", "declaration": key})
            elif previous.get("fingerprint") != item["fingerprint"]:
                changes.append({"kind": "breaking", "declaration": key})
        current_ids = {int(item["id"]) for item in self.entries.values()}
        retired = dict(self.previous.get("retired", {}))
        for key, item in previous_entries.items():
            if int(item["id"]) not in current_ids:
                retired[key] = int(item["id"])
                changes.append({"kind": "breaking", "declaration": key, "removed": True})
        scoped: dict[str, Any] = {}
        for scope, entries in self.scoped_entries.items():
            previous_state = self.previous.get("scoped", {}).get(scope, {})
            previous_entries_scoped = previous_state.get("entries", {})
            retired_scoped = dict(previous_state.get("retired", {}))
            current_ids_scoped = {int(item["id"]) for item in entries.values()}
            for key, item in entries.items():
                previous = previous_entries_scoped.get(key)
                declaration = f"{scope}:{key}"
                if previous is None and not any(
                    int(old["id"]) == int(item["id"]) for old in previous_entries_scoped.values()
                ):
                    changes.append({"kind": "additive", "declaration": declaration})
                elif previous is not None and previous.get("fingerprint") != item["fingerprint"]:
                    changes.append({"kind": "breaking", "declaration": declaration})
            for key, item in previous_entries_scoped.items():
                if int(item["id"]) not in current_ids_scoped:
                    retired_scoped[key] = int(item["id"])
                    changes.append({"kind": "breaking", "declaration": f"{scope}:{key}",
                                    "removed": True})
            scoped[scope] = {"entries": dict(sorted(entries.items())),
                             "retired": dict(sorted(retired_scoped.items()))}

        return {
            "format": LOCK_FORMAT,
            "entries": dict(sorted(self.entries.items())),
            "retired": dict(sorted(retired.items())),
            "scoped": dict(sorted(scoped.items())),
        }, sorted(changes, key=lambda item: (item["kind"], item["declaration"]))
