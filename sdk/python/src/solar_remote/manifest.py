"""Solar Remote manifest-v2 parsing and compatibility classification."""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import IntEnum
import hashlib
import struct
from typing import Any


class ManifestError(ValueError):
    """The supplied bytes are not a supported, well-formed manifest."""


class RecordKind(IntEnum):
    SCHEMA = 1
    FIELD = 2
    DATA = 3
    ACTION = 4
    TOPIC = 5
    STREAM = 6
    LINK = 7
    ENUM_VALUE = 8
    CAPABILITY = 9
    IN_STREAM_GROUP = 10


RECORD_REQUIRED = 1
CODECS = {0: "none", 1: "cbor", 2: "packed"}
SHAPES = {0: "object", 1: "status-code", 2: "enumeration"}
VALUE_KINDS = {
    1: "bool",
    2: "unsigned",
    3: "signed",
    4: "float",
    5: "enum",
    6: "text",
    7: "bytes",
    8: "schema",
}
DOMAINS = {1: "data", 2: "action", 3: "topic", 4: "stream"}
CAPABILITY_KINDS = {
    1: "query",
    2: "update",
    3: "watch",
    4: "out_stream",
    5: "in_stream",
}
DELIVERIES = {
    0: "none",
    1: "latest",
    2: "queue_drop_oldest",
    3: "queue_drop_newest",
    4: "queue_reject",
    5: "reliable",
}
STATUS_NAMES = (
    "ok",
    "error",
    "invalid",
    "not_ready",
    "not_found",
    "not_supported",
    "busy",
    "already",
    "timeout",
    "cancelled",
    "no_memory",
    "no_space",
    "would_block",
    "empty",
    "interrupted",
    "deadlock",
    "permission_denied",
    "no_buffer",
    "message_too_large",
    "protocol_error",
    "overflow",
    "dependency_failed",
    "unexpected_exit",
)


def _text(data: bytes, begin: int, size: int) -> tuple[str, int]:
    end = begin + size
    if end > len(data):
        raise ManifestError("truncated manifest string")
    try:
        return data[begin:end].decode("utf-8"), end
    except UnicodeDecodeError as error:
        raise ManifestError("manifest string is not UTF-8") from error


@dataclass(slots=True)
class Manifest:
    protocol: tuple[int, int]
    image: bytes
    schemas: list[dict[str, Any]] = field(default_factory=list)
    data: list[dict[str, Any]] = field(default_factory=list)
    actions: list[dict[str, Any]] = field(default_factory=list)
    topics: list[dict[str, Any]] = field(default_factory=list)
    streams: list[dict[str, Any]] = field(default_factory=list)
    links: list[dict[str, Any]] = field(default_factory=list)
    capabilities: list[dict[str, Any]] = field(default_factory=list)
    in_stream_groups: list[dict[str, Any]] = field(default_factory=list)

    @property
    def digest(self) -> bytes:
        return hashlib.sha256(self.image).digest()

    def schema(self, stable_id: int) -> dict[str, Any]:
        return next(item for item in self.schemas if item["id"] == stable_id)

    def to_dict(self) -> dict[str, Any]:
        return {
            "format": 2,
            "protocol": list(self.protocol),
            "manifest_sha256": self.digest.hex(),
            "schemas": self.schemas,
            "data": self.data,
            "actions": self.actions,
            "topics": self.topics,
            "streams": self.streams,
            "links": self.links,
            "capabilities": self.capabilities,
            "in_stream_groups": self.in_stream_groups,
        }


def parse_manifest(image: bytes) -> Manifest:
    """Parse and validate one exact embedded manifest-v2 image."""
    if len(image) < 16 or image[:4] != b"SLRM":
        raise ManifestError("invalid manifest header")
    version, major, minor, count, flags, total = struct.unpack_from("<HBBHHI", image, 4)
    if version != 2:
        raise ManifestError(f"unsupported manifest format {version}")
    if flags or total != len(image):
        raise ManifestError("malformed manifest header")

    manifest = Manifest((major, minor), image)
    schemas: dict[int, dict[str, Any]] = {}
    records: list[tuple[RecordKind, bytes]] = []
    offset = 16
    for _ in range(count):
        if offset + 4 > len(image):
            raise ManifestError("truncated manifest record header")
        raw_kind, record_flags, size = struct.unpack_from("<BBH", image, offset)
        if size < 4 or offset + size > len(image) or record_flags & ~RECORD_REQUIRED:
            raise ManifestError("malformed manifest record")
        try:
            kind = RecordKind(raw_kind)
        except ValueError:
            if record_flags & RECORD_REQUIRED:
                raise ManifestError(
                    "SOLAR_DIAGNOSTIC_REMOTE_MANIFEST_REQUIRED_RECORD: "
                    f"unsupported required manifest record kind {raw_kind}"
                ) from None
            offset += size
            continue
        records.append((kind, image[offset + 4 : offset + size]))
        offset += size
    if offset != len(image):
        raise ManifestError("manifest record count or total size is inconsistent")

    # Schema records are parsed first so field and enum references can be checked.
    for kind, body in records:
        if kind != RecordKind.SCHEMA:
            continue
        if len(body) < 20:
            raise ManifestError("short schema record")
        (
            stable_id,
            schema_version,
            shape,
            codec,
            maximum,
            underlying_kind,
            underlying_flags,
            width,
            name_size,
            description_size,
        ) = struct.unpack_from("<IHBBIBBHHH", body)
        if (
            shape not in SHAPES
            or codec not in CODECS
            or underlying_kind not in VALUE_KINDS
            and underlying_kind != 0
            or underlying_flags & ~1
        ):
            raise ManifestError("malformed schema record")
        if shape == 2:
            if (
                codec
                or maximum
                or underlying_kind not in (2, 3)
                or width not in (8, 16, 32, 64)
            ):
                raise ManifestError("malformed enumeration schema representation")
        elif underlying_kind or underlying_flags or width:
            raise ManifestError("object schema has enumeration representation fields")
        name, cursor = _text(body, 20, name_size)
        description, cursor = _text(body, cursor, description_size)
        if cursor != len(body) or stable_id in schemas:
            raise ManifestError("malformed or duplicate schema record")
        item = {
            "id": stable_id,
            "name": name,
            "description": description,
            "version": schema_version,
            "shape": SHAPES[shape],
            "codec": CODECS[codec],
            "max_encoded_size": maximum,
            "underlying_kind": VALUE_KINDS.get(underlying_kind),
            "underlying_width": width,
            "open": bool(underlying_flags & 1),
            "fields": [],
            "values": [],
        }
        if SHAPES[shape] == "status-code":
            item["values"] = [
                {
                    "value": value,
                    "name": name,
                    "description": "",
                    "deprecated": False,
                }
                for value, name in enumerate(STATUS_NAMES)
            ]
        schemas[stable_id] = item
        manifest.schemas.append(item)

    for kind, body in records:
        if kind == RecordKind.SCHEMA:
            continue
        if kind == RecordKind.FIELD:
            if len(body) < 30:
                raise ManifestError("short field record")
            (
                owner,
                reference,
                field_id,
                packed_offset,
                value_kind,
                field_flags,
                width,
                maximum,
                name_size,
                description_size,
                unit_size,
                reserved,
            ) = struct.unpack_from("<IIHIBBHIHHHH", body)
            if (
                owner not in schemas
                or schemas[owner]["shape"] == "enumeration"
                or value_kind not in VALUE_KINDS
                or field_flags & ~3
                or reserved
                or reference
                and reference not in schemas
                or value_kind in (5, 8)
                and not reference
                or value_kind not in (5, 8)
                and reference
            ):
                raise ManifestError("malformed field record")
            name, cursor = _text(body, 30, name_size)
            description, cursor = _text(body, cursor, description_size)
            unit, cursor = _text(body, cursor, unit_size)
            if cursor != len(body):
                raise ManifestError("malformed field record size")
            schemas[owner]["fields"].append(
                {
                    "id": field_id,
                    "name": name,
                    "description": description,
                    "unit": unit,
                    "kind": VALUE_KINDS[value_kind],
                    "required": bool(field_flags & 1),
                    "deprecated": bool(field_flags & 2),
                    "width": width,
                    "maximum_length": maximum,
                    "schema": reference or None,
                    "packed_offset": None
                    if packed_offset == 0xFFFFFFFF
                    else packed_offset,
                }
            )
        elif kind == RecordKind.ENUM_VALUE:
            if len(body) < 20:
                raise ManifestError("short enum-value record")
            owner, raw_value, value_flags, reserved, name_size, description_size = (
                struct.unpack_from("<IQB3sHH", body)
            )
            if (
                owner not in schemas
                or schemas[owner]["shape"] != "enumeration"
                or value_flags & ~2
                or reserved != b"\0\0\0"
            ):
                raise ManifestError("malformed enum-value record")
            schema = schemas[owner]
            width = schema["underlying_width"]
            if (
                schema["underlying_kind"] == "signed"
                and width
                and raw_value & (1 << (width - 1))
            ):
                raw_value -= 1 << width
            name, cursor = _text(body, 20, name_size)
            description, cursor = _text(body, cursor, description_size)
            if cursor != len(body):
                raise ManifestError("malformed enum-value record size")
            schema["values"].append(
                {
                    "value": raw_value,
                    "name": name,
                    "description": description,
                    "deprecated": bool(value_flags & 2),
                }
            )
        elif kind == RecordKind.ACTION:
            if len(body) < 24:
                raise ManifestError("short action record")
            (
                stable_id,
                request,
                response,
                error,
                version,
                access,
                reserved,
                name_size,
                description_size,
            ) = struct.unpack_from("<IIIIHBBHH", body)
            if reserved or access & ~0x0F:
                raise ManifestError("malformed action record")
            name, cursor = _text(body, 24, name_size)
            description, cursor = _text(body, cursor, description_size)
            if cursor != len(body):
                raise ManifestError("malformed action record size")
            manifest.actions.append(
                {
                    "id": stable_id,
                    "name": name,
                    "description": description,
                    "version": version,
                    "request_schema": request,
                    "response_schema": response,
                    "error_schema": error,
                    "permission_mask": access,
                }
            )
        elif kind in (RecordKind.DATA, RecordKind.TOPIC, RecordKind.STREAM):
            if len(body) < 16:
                raise ManifestError("short endpoint record")
            (
                stable_id,
                schema_id,
                version,
                policy,
                reserved,
                name_size,
                description_size,
            ) = struct.unpack_from("<IIHBBHH", body)
            if reserved or schema_id not in schemas:
                raise ManifestError("malformed endpoint record")
            name, cursor = _text(body, 16, name_size)
            description, cursor = _text(body, cursor, description_size)
            if cursor != len(body):
                raise ManifestError("malformed endpoint record size")
            item = {
                "id": stable_id,
                "name": name,
                "description": description,
                "version": version,
                "schema": schema_id,
            }
            if kind == RecordKind.DATA:
                item["capability_mask"] = policy
                manifest.data.append(item)
            else:
                if policy not in CODECS:
                    raise ManifestError("unknown endpoint codec")
                item["codec"] = CODECS[policy]
                (
                    manifest.topics if kind == RecordKind.TOPIC else manifest.streams
                ).append(item)
        elif kind == RecordKind.LINK:
            if len(body) < 12:
                raise ManifestError("short link record")
            stable_id, version, grants, reserved, name_size, description_size = (
                struct.unpack_from("<IHBBHH", body)
            )
            if reserved or grants & ~0x0F:
                raise ManifestError("malformed link record")
            name, cursor = _text(body, 12, name_size)
            description, cursor = _text(body, cursor, description_size)
            if cursor != len(body):
                raise ManifestError("malformed link record size")
            manifest.links.append(
                {
                    "id": stable_id,
                    "name": name,
                    "description": description,
                    "version": version,
                    "grant_mask": grants,
                }
            )
        elif kind == RecordKind.CAPABILITY:
            if len(body) != 24:
                raise ManifestError("malformed capability record size")
            (
                domain,
                capability,
                permission,
                codec,
                endpoint,
                rate,
                batch,
                window,
                delivery,
                (capability_flags),
                in_stream_flags,
                replacement,
                group,
            ) = struct.unpack_from("<BBBBIIHHBBBBI", body)
            if (
                domain not in DOMAINS
                or capability not in CAPABILITY_KINDS
                or codec not in CODECS
                or delivery not in DELIVERIES
                or permission & ~0x0F
                or capability_flags & ~3
                or in_stream_flags & ~0x0F
                or replacement not in (0, 1, 2)
                or capability != 5
                and (in_stream_flags or replacement or group)
                or capability == 5
                and not in_stream_flags & 1
                or bool(in_stream_flags & 8) != bool(group)
                or bool(group) != bool(replacement)
            ):
                raise ManifestError("malformed capability record")
            manifest.capabilities.append(
                {
                    "domain": DOMAINS[domain],
                    "kind": CAPABILITY_KINDS[capability],
                    "endpoint": endpoint,
                    "permission_mask": permission,
                    "codec": CODECS[codec],
                    "maximum_rate_hz": rate,
                    "maximum_batch": batch,
                    "reliable_window": window,
                    "delivery": DELIVERIES[delivery],
                    "cancellation": bool(capability_flags & 1),
                    "batched": bool(capability_flags & 2),
                    "explicit_open": bool(in_stream_flags & 1),
                    "on_open": bool(in_stream_flags & 2),
                    "on_close": bool(in_stream_flags & 4),
                    "exclusive": bool(in_stream_flags & 8),
                    "replacement": {0: "none", 1: "replace", 2: "reject"}[
                        replacement
                    ],
                    "group": group or None,
                }
            )
        elif kind == RecordKind.IN_STREAM_GROUP:
            if len(body) < 12:
                raise ManifestError("short inbound stream group record")
            stable_id, version, name_size, description_size, reserved = (
                struct.unpack_from("<IHHHH", body)
            )
            name, cursor = _text(body, 12, name_size)
            description, cursor = _text(body, cursor, description_size)
            if (
                not stable_id
                or not name
                or reserved
                or cursor != len(body)
                or any(item["id"] == stable_id for item in manifest.in_stream_groups)
                or any(item["name"] == name for item in manifest.in_stream_groups)
            ):
                raise ManifestError("malformed or duplicate inbound stream group")
            manifest.in_stream_groups.append(
                {
                    "id": stable_id,
                    "name": name,
                    "description": description,
                    "version": version,
                }
            )

    for schema in manifest.schemas:
        schema["fields"].sort(key=lambda item: item["id"])
        schema["values"].sort(key=lambda item: item["value"])
        if schema["shape"] == "enumeration" and not schema["values"]:
            raise ManifestError("enumeration schema has no values")
    for action in manifest.actions:
        if any(
            action[key] not in schemas
            for key in (
                "request_schema",
                "response_schema",
                "error_schema",
            )
        ):
            raise ManifestError("action references an unknown schema")
    for collection in (
        manifest.schemas,
        manifest.data,
        manifest.actions,
        manifest.topics,
        manifest.streams,
        manifest.links,
    ):
        collection.sort(key=lambda item: item["id"])
    domain_order = {name: number for number, name in DOMAINS.items()}
    capability_order = {name: number for number, name in CAPABILITY_KINDS.items()}
    manifest.capabilities.sort(
        key=lambda item: (
            domain_order[item["domain"]],
            item["endpoint"],
            capability_order[item["kind"]],
        )
    )
    manifest.in_stream_groups.sort(key=lambda item: item["id"])
    group_ids = {item["id"] for item in manifest.in_stream_groups}
    for capability in manifest.capabilities:
        if capability["group"] is not None and capability["group"] not in group_ids:
            raise ManifestError("inbound stream capability references an unknown group")
    return manifest


def compatibility(previous: Manifest, current: Manifest) -> dict[str, list[str]]:
    """Classify notable manifest changes into wire, source, and behavior buckets."""
    changes: dict[str, list[str]] = {
        "breaking": [],
        "wire_additive": [],
        "source": [],
        "behavioral": [],
        "metadata": [],
    }
    old_schemas = {item["id"]: item for item in previous.schemas}
    new_schemas = {item["id"]: item for item in current.schemas}
    for stable_id, old in old_schemas.items():
        new = new_schemas.get(stable_id)
        if new is None:
            changes["breaking"].append(f"schema 0x{stable_id:08x} removed")
            continue
        if old["name"] != new["name"]:
            changes["source"].append(f"schema 0x{stable_id:08x} renamed")
        if any(
            old[key] != new[key]
            for key in (
                "shape",
                "codec",
                "max_encoded_size",
                "underlying_kind",
                "underlying_width",
                "open",
            )
        ):
            changes["breaking"].append(
                f"schema {old['name']} changed wire representation"
            )
        old_fields = {item["id"]: item for item in old["fields"]}
        new_fields = {item["id"]: item for item in new["fields"]}
        for field_id, old_field in old_fields.items():
            new_field = new_fields.get(field_id)
            if new_field is None:
                changes["breaking"].append(
                    f"field {old['name']}.{old_field['name']} removed"
                )
            elif any(
                old_field[key] != new_field[key]
                for key in (
                    "kind",
                    "width",
                    "maximum_length",
                    "schema",
                    "packed_offset",
                    "required",
                )
            ):
                changes["breaking"].append(
                    f"field {old['name']}.{old_field['name']} changed wire type"
                )
            elif old_field["name"] != new_field["name"]:
                changes["source"].append(
                    f"field {old['name']}.{old_field['name']} renamed"
                )
            elif any(
                old_field[key] != new_field[key]
                for key in ("description", "unit", "deprecated")
            ):
                changes["metadata"].append(
                    f"field {old['name']}.{old_field['name']} metadata changed"
                )
        for field_id, new_field in new_fields.items():
            if field_id not in old_fields:
                bucket = (
                    "breaking"
                    if new_field["required"] or new["codec"] == "packed"
                    else "wire_additive"
                )
                changes[bucket].append(f"field {new['name']}.{new_field['name']} added")
        if old["description"] != new["description"]:
            changes["metadata"].append(f"schema {new['name']} description changed")
        old_values = {item["value"]: item for item in old["values"]}
        new_values = {item["value"]: item for item in new["values"]}
        for value, old_value in old_values.items():
            if value not in new_values:
                changes["breaking"].append(f"enum {old['name']} value {value} removed")
            elif old_value["name"] != new_values[value]["name"]:
                changes["source"].append(f"enum {old['name']} value {value} renamed")
        for value in new_values.keys() - old_values.keys():
            bucket = "wire_additive" if new["open"] else "behavioral"
            changes[bucket].append(f"enum {new['name']} value {value} added")
    for stable_id in new_schemas.keys() - old_schemas.keys():
        changes["wire_additive"].append(
            f"schema {new_schemas[stable_id]['name']} added"
        )
    for collection_name in ("data", "actions", "topics", "streams", "links"):
        old_items = {item["id"]: item for item in getattr(previous, collection_name)}
        new_items = {item["id"]: item for item in getattr(current, collection_name)}
        for stable_id, old in old_items.items():
            new = new_items.get(stable_id)
            if new is None:
                changes["breaking"].append(
                    f"{collection_name} endpoint {old['name']} removed"
                )
            elif old["name"] != new["name"]:
                changes["source"].append(
                    f"{collection_name} endpoint 0x{stable_id:08x} renamed"
                )
            elif any(
                old.get(key) != new.get(key)
                for key in (
                    "schema",
                    "request_schema",
                    "response_schema",
                    "error_schema",
                )
            ):
                changes["breaking"].append(
                    f"{collection_name} endpoint {old['name']} changed schema"
                )
            elif any(
                old.get(key) != new.get(key)
                for key in ("permission_mask", "grant_mask", "codec")
            ):
                changes["behavioral"].append(
                    f"{collection_name} endpoint {old['name']} policy changed"
                )
        for stable_id in new_items.keys() - old_items.keys():
            changes["wire_additive"].append(
                f"{collection_name} endpoint {new_items[stable_id]['name']} added"
            )

    def capability_key(item: dict[str, Any]) -> tuple[str, int, str]:
        return item["domain"], item["endpoint"], item["kind"]

    old_capabilities = {capability_key(item): item for item in previous.capabilities}
    new_capabilities = {capability_key(item): item for item in current.capabilities}
    for key, old in old_capabilities.items():
        new = new_capabilities.get(key)
        if new is None:
            changes["breaking"].append(f"capability {key} removed")
            continue
        old_rate, new_rate = old["maximum_rate_hz"], new["maximum_rate_hz"]
        if old_rate != new_rate:
            direction = (
                "tightened"
                if new_rate and (not old_rate or new_rate < old_rate)
                else "relaxed"
            )
            changes["behavioral"].append(f"capability {key} rate {direction}")
        if any(
            old[field] != new[field]
            for field in (
                "permission_mask",
                "codec",
                "maximum_batch",
                "reliable_window",
                "delivery",
                "cancellation",
                "batched",
                "explicit_open",
                "on_open",
                "on_close",
                "exclusive",
                "replacement",
                "group",
            )
        ):
            changes["behavioral"].append(f"capability {key} policy changed")
    for key in new_capabilities.keys() - old_capabilities.keys():
        changes["wire_additive"].append(f"capability {key} added")

    old_groups = {item["id"]: item for item in previous.in_stream_groups}
    new_groups = {item["id"]: item for item in current.in_stream_groups}
    for stable_id, old in old_groups.items():
        new = new_groups.get(stable_id)
        if new is None:
            changes["breaking"].append(
                f"inbound stream group {old['name']} removed"
            )
        elif old["name"] != new["name"]:
            changes["source"].append(
                f"inbound stream group 0x{stable_id:08x} renamed"
            )
        elif old["description"] != new["description"]:
            changes["metadata"].append(
                f"inbound stream group {new['name']} description changed"
            )
    for stable_id in new_groups.keys() - old_groups.keys():
        changes["wire_additive"].append(
            f"inbound stream group {new_groups[stable_id]['name']} added"
        )
    return changes
