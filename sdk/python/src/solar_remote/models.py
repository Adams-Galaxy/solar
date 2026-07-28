"""Manifest-driven Python model generation."""

from __future__ import annotations

from dataclasses import field, make_dataclass
from enum import IntEnum
import keyword
import re
from types import MappingProxyType
from typing import Any

from .descriptors import ManifestCatalog, SchemaDescriptor


class OpenIntEnum(IntEnum):
    """IntEnum base which preserves values added by newer firmware."""

    @classmethod
    def _missing_(cls, value: object):
        if not isinstance(value, int):
            return None
        member = int.__new__(cls, value)
        member._name_ = f"UNKNOWN_{value}"
        member._value_ = value
        cls._value2member_map_[value] = member
        return member


def python_identifier(value: str) -> str:
    result = re.sub(r"\W", "_", value)
    if not result or result[0].isdigit():
        result = "_" + result
    if keyword.iskeyword(result):
        result += "_"
    return result


class ModelRegistry:
    """Creates and caches one exact set of Python types for a manifest."""

    def __init__(self, catalog: ManifestCatalog):
        self.catalog = catalog
        self._types: dict[int, type[Any]] = {}
        self._build_enums()
        self._build_objects()
        self.types = MappingProxyType(self._types)

    def _build_enums(self) -> None:
        for schema in self.catalog.schemas:
            if schema.shape not in ("enumeration", "status-code"):
                continue
            members = {
                python_identifier(value.name).upper(): value.value
                for value in schema.values
            }
            base = OpenIntEnum if schema.open else IntEnum
            self._types[schema.id] = base(
                python_identifier(schema.name.rsplit(".", 1)[-1]),
                members,
                module="solar_remote.dynamic",
            )

    def _annotation(self, kind: str, schema_id: int | None) -> Any:
        if schema_id is not None:
            return self._types.get(schema_id, object)
        return {
            "bool": bool,
            "unsigned": int,
            "signed": int,
            "float": float,
            "text": str,
            "bytes": bytes,
        }.get(kind, object)

    def _build_objects(self) -> None:
        pending = [
            schema
            for schema in self.catalog.schemas
            if schema.shape == "object"
        ]
        # Current manifest v2 does not permit nested object fields, but this
        # loop keeps construction deterministic when that representation lands.
        while pending:
            progressed = False
            for schema in tuple(pending):
                unresolved = [
                    value.schema_id
                    for value in schema.fields
                    if value.schema_id is not None
                    and value.schema_id not in self._types
                ]
                if unresolved:
                    continue
                required = [value for value in schema.fields if value.required]
                optional = [value for value in schema.fields if not value.required]
                definitions = []
                for value in required:
                    definitions.append(
                        (
                            python_identifier(value.name),
                            self._annotation(value.kind, value.schema_id),
                        )
                    )
                for value in optional:
                    definitions.append(
                        (
                            python_identifier(value.name),
                            self._annotation(value.kind, value.schema_id) | None,
                            field(default=None),
                        )
                    )
                self._types[schema.id] = make_dataclass(
                    python_identifier(schema.name.rsplit(".", 1)[-1]),
                    definitions,
                    frozen=True,
                    slots=True,
                    namespace={
                        "__module__": "solar_remote.dynamic",
                        "__solar_schema_id__": schema.id,
                        "__solar_schema_name__": schema.name,
                    },
                )
                pending.remove(schema)
                progressed = True
            if not progressed:
                names = ", ".join(schema.name for schema in pending)
                raise ValueError(f"cannot resolve dynamic schema models: {names}")

    def type_for(self, schema: int | SchemaDescriptor) -> type[Any]:
        stable_id = schema.id if isinstance(schema, SchemaDescriptor) else schema
        return self._types[stable_id]

    def construct(self, schema_id: int, values: dict[str, Any] | int) -> Any:
        model = self.type_for(schema_id)
        schema = self.catalog.schemas.by_id(schema_id)
        if schema.shape in ("enumeration", "status-code"):
            return model(values)
        if not isinstance(values, dict):
            raise TypeError(f"{schema.name} requires an object value")
        converted = dict(values)
        for descriptor in schema.fields:
            if descriptor.schema_id is None or descriptor.name not in converted:
                continue
            converted[descriptor.name] = self.construct(
                descriptor.schema_id, converted[descriptor.name]
            )
        return model(**converted)
