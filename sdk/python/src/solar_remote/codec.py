"""Manifest-driven CBOR and packed value codecs."""

from __future__ import annotations

from dataclasses import asdict, dataclass, is_dataclass
import struct
from typing import Any, Mapping

import cbor2

from .manifest import Manifest


@dataclass(frozen=True, slots=True)
class UnknownEnumValue:
    schema: str
    value: int


class CodecError(ValueError):
    pass


_STRUCT = {
    ("bool", 8): "<?",
    ("unsigned", 8): "<B",
    ("unsigned", 16): "<H",
    ("unsigned", 32): "<I",
    ("unsigned", 64): "<Q",
    ("signed", 8): "<b",
    ("signed", 16): "<h",
    ("signed", 32): "<i",
    ("signed", 64): "<q",
    ("float", 32): "<f",
    ("float", 64): "<d",
}


class DynamicCodec:
    def __init__(self, manifest: Manifest):
        self.manifest = manifest
        self.schemas = {item["id"]: item for item in manifest.schemas}

    @staticmethod
    def _mapping(value: Any) -> Mapping[str, Any]:
        if is_dataclass(value):
            return asdict(value)
        if isinstance(value, Mapping):
            return value
        return vars(value)

    def _enum(self, schema_id: int, value: int) -> int | UnknownEnumValue:
        schema = self.schemas[schema_id]
        if any(item["value"] == value for item in schema["values"]):
            return value
        if not schema["open"]:
            raise CodecError(f"{value} is not declared by closed enum {schema['name']}")
        return UnknownEnumValue(schema["name"], value)

    def _validate(self, field: dict[str, Any], value: Any) -> Any:
        if value is None:
            if field["required"]:
                raise CodecError(f"required field {field['name']} is missing")
            return None
        if field["kind"] == "text":
            if (
                not isinstance(value, str)
                or len(value.encode()) > field["maximum_length"]
            ):
                raise CodecError(f"field {field['name']} exceeds its text bound")
        elif field["kind"] == "bytes":
            if not isinstance(value, bytes) or len(value) > field["maximum_length"]:
                raise CodecError(f"field {field['name']} exceeds its byte bound")
        elif field["kind"] == "bool":
            if not isinstance(value, bool):
                raise CodecError(f"field {field['name']} is not Boolean")
        elif field["kind"] in ("signed", "unsigned"):
            if not isinstance(value, int) or isinstance(value, bool):
                raise CodecError(f"field {field['name']} is not an integer")
            minimum = -(1 << (field["width"] - 1)) if field["kind"] == "signed" else 0
            maximum = (
                (1 << (field["width"] - 1)) - 1
                if field["kind"] == "signed"
                else (1 << field["width"]) - 1
            )
            if not minimum <= value <= maximum:
                raise CodecError(f"field {field['name']} exceeds its integer width")
        elif field["kind"] == "float":
            if not isinstance(value, (int, float)) or isinstance(value, bool):
                raise CodecError(f"field {field['name']} is not numeric")
            numeric = float(value)
            if field["width"] == 32:
                try:
                    # cbor2's canonical encoder chooses the narrowest exact
                    # representation. Quantizing first makes a manifest float32
                    # travel as CBOR float16/32 instead of an incompatible
                    # float64 whenever the original Python value is not exactly
                    # representable at 32 bits.
                    return struct.unpack("<f", struct.pack("<f", numeric))[0]
                except OverflowError as error:
                    raise CodecError(
                        f"field {field['name']} exceeds its float width"
                    ) from error
            if field["width"] == 64:
                return numeric
            raise CodecError(f"field {field['name']} has an unsupported float width")
        elif field["kind"] == "enum":
            numeric = value.value if hasattr(value, "value") else value
            if isinstance(value, UnknownEnumValue):
                numeric = value.value
            if not isinstance(numeric, int) or isinstance(numeric, bool):
                raise CodecError(f"field {field['name']} is not an enum value")
            schema = self.schemas[field["schema"]]
            width = schema["underlying_width"]
            minimum = (
                -(1 << (width - 1)) if schema["underlying_kind"] == "signed" else 0
            )
            maximum = (
                (1 << (width - 1)) - 1
                if schema["underlying_kind"] == "signed"
                else (1 << width) - 1
            )
            if not minimum <= numeric <= maximum:
                raise CodecError(f"field {field['name']} exceeds its enum width")
            self._enum(field["schema"], int(numeric))
            return int(numeric)
        elif field["kind"] == "schema":
            schema = self.schemas[field["schema"]]
            if schema["shape"] == "status-code":
                numeric = value.value if hasattr(value, "value") else int(value)
                if not any(item["value"] == numeric for item in schema["values"]):
                    raise CodecError(f"{numeric} is not a valid Solar status code")
                return numeric
        return value

    def encode(self, schema_id: int, value: Any) -> bytes:
        schema = self.schemas[schema_id]
        source = self._mapping(value)
        if schema["codec"] == "cbor":
            output = {}
            for item in schema["fields"]:
                present = item["name"] in source and source[item["name"]] is not None
                if item["required"] and not present:
                    raise CodecError(f"required field {item['name']} is missing")
                if present:
                    output[item["id"]] = self._validate(item, source[item["name"]])
            encoded = cbor2.dumps(output, canonical=True)
        elif schema["codec"] == "packed":
            output = bytearray(schema["max_encoded_size"])
            for item in schema["fields"]:
                value = self._validate(item, source.get(item["name"]))
                if value is None:
                    raise CodecError("packed schemas cannot contain absent fields")
                kind = item["kind"]
                if kind == "enum":
                    enum_schema = self.schemas[item["schema"]]
                    kind = enum_schema["underlying_kind"]
                fmt = _STRUCT.get((kind, item["width"]))
                if fmt is None:
                    raise CodecError(f"unsupported packed field {item['name']}")
                struct.pack_into(fmt, output, item["packed_offset"], value)
            encoded = bytes(output)
        else:
            raise CodecError(f"schema {schema['name']} has no value codec")
        if len(encoded) > schema["max_encoded_size"]:
            raise CodecError(f"schema {schema['name']} exceeded its encoded bound")
        return encoded

    def decode(self, schema_id: int, payload: bytes) -> dict[str, Any]:
        schema = self.schemas[schema_id]
        if schema["codec"] == "cbor":
            raw = cbor2.loads(payload)
            if not isinstance(raw, dict):
                raise CodecError("object payload is not a CBOR map")
            unknown = set(raw) - {item["id"] for item in schema["fields"]}
            if unknown:
                raise CodecError(f"unknown field IDs: {sorted(unknown)}")
            output = {}
            for item in schema["fields"]:
                if item["id"] not in raw:
                    if item["required"]:
                        raise CodecError(f"required field {item['name']} is missing")
                    output[item["name"]] = None
                    continue
                value = self._validate(item, raw[item["id"]])
                if item["kind"] == "enum":
                    value = self._enum(item["schema"], int(value))
                output[item["name"]] = value
            return output
        if schema["codec"] != "packed" or len(payload) != schema["max_encoded_size"]:
            raise CodecError("packed payload size does not match its schema")
        output = {}
        for item in schema["fields"]:
            kind = item["kind"]
            if kind == "enum":
                enum_schema = self.schemas[item["schema"]]
                kind = enum_schema["underlying_kind"]
            fmt = _STRUCT.get((kind, item["width"]))
            if fmt is None:
                raise CodecError(f"unsupported packed field {item['name']}")
            value = struct.unpack_from(fmt, payload, item["packed_offset"])[0]
            if item["kind"] == "enum":
                value = self._enum(item["schema"], value)
            output[item["name"]] = value
        return output
