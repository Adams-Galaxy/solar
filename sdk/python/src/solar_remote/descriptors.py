"""Typed, indexed views over one Solar Remote manifest."""

from __future__ import annotations

from collections.abc import Iterator, Mapping, Sequence
from dataclasses import dataclass
from typing import Any, Generic, TypeVar, overload


T = TypeVar("T")


class Catalog(Sequence[T], Generic[T]):
    """Immutable descriptor sequence with constant-time name and ID lookup."""

    def __init__(self, values: Sequence[T]):
        self._values = tuple(values)
        self._by_id = {getattr(value, "id"): value for value in self._values}
        self._by_name = {getattr(value, "name"): value for value in self._values}
        if len(self._by_id) != len(self._values):
            raise ValueError("descriptor catalog contains duplicate stable IDs")
        if len(self._by_name) != len(self._values):
            raise ValueError("descriptor catalog contains duplicate names")

    @overload
    def __getitem__(self, key: int) -> T: ...

    @overload
    def __getitem__(self, key: slice) -> tuple[T, ...]: ...

    @overload
    def __getitem__(self, key: str) -> T: ...

    def __getitem__(self, key: int | slice | str) -> T | tuple[T, ...]:
        if isinstance(key, str):
            return self._by_name[key]
        return self._values[key]

    def __len__(self) -> int:
        return len(self._values)

    def __iter__(self) -> Iterator[T]:
        return iter(self._values)

    def by_id(self, stable_id: int) -> T:
        return self._by_id[stable_id]

    def resolve(self, identity: int | str) -> T:
        return self.by_id(identity) if isinstance(identity, int) else self[identity]

    def get(self, identity: int | str, default: T | None = None) -> T | None:
        try:
            return self.resolve(identity)
        except KeyError:
            return default


@dataclass(frozen=True, slots=True)
class FieldDescriptor:
    id: int
    name: str
    description: str
    unit: str
    kind: str
    required: bool
    deprecated: bool
    width: int
    maximum_length: int
    schema_id: int | None
    packed_offset: int | None


@dataclass(frozen=True, slots=True)
class EnumValueDescriptor:
    value: int
    name: str
    description: str
    deprecated: bool


@dataclass(frozen=True, slots=True)
class SchemaDescriptor:
    id: int
    name: str
    description: str
    version: int
    shape: str
    codec: str
    max_encoded_size: int
    underlying_kind: str | None
    underlying_width: int
    open: bool
    fields: tuple[FieldDescriptor, ...]
    values: tuple[EnumValueDescriptor, ...]


@dataclass(frozen=True, slots=True)
class CapabilityDescriptor:
    domain: str
    kind: str
    endpoint_id: int
    permission_mask: int
    codec: str
    maximum_rate_hz: int
    maximum_batch: int
    reliable_window: int
    delivery: str
    cancellation: bool
    batched: bool
    explicit_open: bool
    on_open: bool
    on_close: bool
    exclusive: bool
    replacement: str
    group_id: int | None


@dataclass(frozen=True, slots=True)
class EndpointDescriptor:
    id: int
    name: str
    description: str
    version: int
    schema_id: int
    domain: str
    capabilities: tuple[CapabilityDescriptor, ...] = ()

    def supports(self, kind: str) -> bool:
        return any(capability.kind == kind for capability in self.capabilities)


@dataclass(frozen=True, slots=True)
class DataEndpoint(EndpointDescriptor):
    capability_mask: int = 0


@dataclass(frozen=True, slots=True)
class TopicEndpoint(EndpointDescriptor):
    codec: str = "cbor"


@dataclass(frozen=True, slots=True)
class StreamEndpoint(EndpointDescriptor):
    codec: str = "cbor"


@dataclass(frozen=True, slots=True)
class ActionEndpoint:
    id: int
    name: str
    description: str
    version: int
    request_schema_id: int
    response_schema_id: int
    error_schema_id: int
    permission_mask: int
    domain: str = "action"
    capabilities: tuple[CapabilityDescriptor, ...] = ()


@dataclass(frozen=True, slots=True)
class LinkDescriptor:
    id: int
    name: str
    description: str
    version: int
    grant_mask: int


@dataclass(frozen=True, slots=True)
class InStreamGroupDescriptor:
    id: int
    name: str
    description: str
    version: int


@dataclass(frozen=True, slots=True)
class ManifestCatalog:
    schemas: Catalog[SchemaDescriptor]
    data: Catalog[DataEndpoint]
    actions: Catalog[ActionEndpoint]
    topics: Catalog[TopicEndpoint]
    streams: Catalog[StreamEndpoint]
    links: Catalog[LinkDescriptor]
    capabilities: tuple[CapabilityDescriptor, ...]
    in_stream_groups: Catalog[InStreamGroupDescriptor]

    @classmethod
    def from_raw(
        cls,
        *,
        schemas: Sequence[Mapping[str, Any]],
        data: Sequence[Mapping[str, Any]],
        actions: Sequence[Mapping[str, Any]],
        topics: Sequence[Mapping[str, Any]],
        streams: Sequence[Mapping[str, Any]],
        links: Sequence[Mapping[str, Any]],
        capabilities: Sequence[Mapping[str, Any]],
        in_stream_groups: Sequence[Mapping[str, Any]],
    ) -> "ManifestCatalog":
        typed_capabilities = tuple(
            CapabilityDescriptor(
                domain=str(item["domain"]),
                kind=str(item["kind"]),
                endpoint_id=int(item["endpoint"]),
                permission_mask=int(item.get("permission_mask", 0)),
                codec=str(item.get("codec", "cbor")),
                maximum_rate_hz=int(item.get("maximum_rate_hz", 0)),
                maximum_batch=int(item.get("maximum_batch", 1)),
                reliable_window=int(item.get("reliable_window", 0)),
                delivery=str(item.get("delivery", "none")),
                cancellation=bool(item.get("cancellation", False)),
                batched=bool(item.get("batched", False)),
                explicit_open=bool(item.get("explicit_open", False)),
                on_open=bool(item.get("on_open", False)),
                on_close=bool(item.get("on_close", False)),
                exclusive=bool(item.get("exclusive", False)),
                replacement=str(item.get("replacement", "none")),
                group_id=(
                    int(item["group"]) if item.get("group") is not None else None
                ),
            )
            for item in capabilities
        )

        def endpoint_capabilities(domain: str, endpoint_id: int):
            return tuple(
                capability
                for capability in typed_capabilities
                if capability.domain == domain
                and capability.endpoint_id == endpoint_id
            )

        schema_catalog = Catalog(
            [
                SchemaDescriptor(
                    id=int(item["id"]),
                    name=str(item["name"]),
                    description=str(item.get("description", "")),
                    version=int(item.get("version", 1)),
                    shape=str(item["shape"]),
                    codec=str(item["codec"]),
                    max_encoded_size=int(item.get("max_encoded_size", 0)),
                    underlying_kind=(
                        str(item["underlying_kind"])
                        if item.get("underlying_kind") is not None
                        else None
                    ),
                    underlying_width=int(item.get("underlying_width", 0)),
                    open=bool(item.get("open", False)),
                    fields=tuple(
                        FieldDescriptor(
                            id=int(field["id"]),
                            name=str(field["name"]),
                            description=str(field.get("description", "")),
                            unit=str(field.get("unit", "")),
                            kind=str(field["kind"]),
                            required=bool(field["required"]),
                            deprecated=bool(field.get("deprecated", False)),
                            width=int(field["width"]),
                            maximum_length=int(field.get("maximum_length", 0)),
                            schema_id=(
                                int(field["schema"])
                                if field.get("schema") not in (None, 0)
                                else None
                            ),
                            packed_offset=(
                                int(field["packed_offset"])
                                if field.get("packed_offset") is not None
                                else None
                            ),
                        )
                        for field in item.get("fields", ())
                    ),
                    values=tuple(
                        EnumValueDescriptor(
                            value=int(value["value"]),
                            name=str(value["name"]),
                            description=str(value.get("description", "")),
                            deprecated=bool(value.get("deprecated", False)),
                        )
                        for value in item.get("values", ())
                    ),
                )
                for item in schemas
            ]
        )
        return cls(
            schemas=schema_catalog,
            data=Catalog(
                [
                    DataEndpoint(
                        id=int(item["id"]),
                        name=str(item["name"]),
                        description=str(item.get("description", "")),
                        version=int(item.get("version", 1)),
                        schema_id=int(item["schema"]),
                        domain="data",
                        capabilities=endpoint_capabilities("data", int(item["id"])),
                        capability_mask=int(item.get("capability_mask", 0)),
                    )
                    for item in data
                ]
            ),
            actions=Catalog(
                [
                    ActionEndpoint(
                        id=int(item["id"]),
                        name=str(item["name"]),
                        description=str(item.get("description", "")),
                        version=int(item.get("version", 1)),
                        request_schema_id=int(item["request_schema"]),
                        response_schema_id=int(item["response_schema"]),
                        error_schema_id=int(item["error_schema"]),
                        permission_mask=int(item.get("permission_mask", 0)),
                        capabilities=endpoint_capabilities(
                            "action", int(item["id"])
                        ),
                    )
                    for item in actions
                ]
            ),
            topics=Catalog(
                [
                    TopicEndpoint(
                        id=int(item["id"]),
                        name=str(item["name"]),
                        description=str(item.get("description", "")),
                        version=int(item.get("version", 1)),
                        schema_id=int(item["schema"]),
                        domain="topic",
                        capabilities=endpoint_capabilities("topic", int(item["id"])),
                        codec=str(item.get("codec", "cbor")),
                    )
                    for item in topics
                ]
            ),
            streams=Catalog(
                [
                    StreamEndpoint(
                        id=int(item["id"]),
                        name=str(item["name"]),
                        description=str(item.get("description", "")),
                        version=int(item.get("version", 1)),
                        schema_id=int(item["schema"]),
                        domain="stream",
                        capabilities=endpoint_capabilities(
                            "stream", int(item["id"])
                        ),
                        codec=str(item.get("codec", "cbor")),
                    )
                    for item in streams
                ]
            ),
            links=Catalog(
                [
                    LinkDescriptor(
                        id=int(item["id"]),
                        name=str(item["name"]),
                        description=str(item.get("description", "")),
                        version=int(item.get("version", 1)),
                        grant_mask=int(item.get("grant_mask", 0)),
                    )
                    for item in links
                ]
            ),
            capabilities=typed_capabilities,
            in_stream_groups=Catalog(
                [
                    InStreamGroupDescriptor(
                        id=int(item["id"]),
                        name=str(item["name"]),
                        description=str(item.get("description", "")),
                        version=int(item.get("version", 1)),
                    )
                    for item in in_stream_groups
                ]
            ),
        )
