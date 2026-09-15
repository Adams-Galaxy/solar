from __future__ import annotations

from solar_remote.descriptors import ManifestCatalog
from solar_remote.models import ModelRegistry


def _point_field(id_: int, name: str, width: int) -> dict:
    return {
        "id": id_,
        "name": name,
        "description": "",
        "unit": "",
        "kind": "unsigned",
        "required": True,
        "deprecated": False,
        "width": width,
        "maximum_length": 0,
        "schema": None,
        "packed_offset": None,
        "element_kind": None,
    }


def _catalog() -> ManifestCatalog:
    schemas = [
        {
            "id": 2,
            "name": "fixture.Point",
            "description": "",
            "version": 1,
            "shape": "record",
            "codec": "cbor",
            "max_encoded_size": 0,
            "underlying_kind": None,
            "underlying_width": 0,
            "open": False,
            "values": [],
            "fields": [
                _point_field(1, "angle", 16),
                _point_field(2, "distance", 16),
                _point_field(3, "confidence", 8),
            ],
        },
        {
            "id": 1,
            "name": "fixture.Scan",
            "description": "",
            "version": 1,
            "shape": "object",
            "codec": "cbor",
            "max_encoded_size": 256,
            "underlying_kind": None,
            "underlying_width": 0,
            "open": False,
            "values": [],
            "fields": [
                {
                    "id": 1,
                    "name": "generation",
                    "description": "",
                    "unit": "",
                    "kind": "unsigned",
                    "required": True,
                    "deprecated": False,
                    "width": 64,
                    "maximum_length": 0,
                    "schema": None,
                    "packed_offset": None,
                    "element_kind": None,
                },
                {
                    "id": 2,
                    "name": "points",
                    "description": "",
                    "unit": "",
                    "kind": "array",
                    "required": True,
                    "deprecated": False,
                    "width": 0,
                    "maximum_length": 4,
                    "schema": 2,
                    "packed_offset": None,
                    "element_kind": "schema",
                },
                {
                    "id": 3,
                    "name": "checksums",
                    "description": "",
                    "unit": "",
                    "kind": "array",
                    "required": True,
                    "deprecated": False,
                    "width": 16,
                    "maximum_length": 4,
                    "schema": None,
                    "packed_offset": None,
                    "element_kind": "unsigned",
                },
            ],
        },
    ]
    return ManifestCatalog.from_raw(
        schemas=schemas,
        data=(),
        actions=(),
        topics=(),
        streams=(),
        links=(),
        capabilities=(),
        in_stream_groups=(),
    )


def test_record_shape_gets_a_dataclass_and_array_field_is_typed() -> None:
    registry = ModelRegistry(_catalog())

    point = registry.types[2]
    assert point.__name__ == "Point"
    assert {field.name for field in _dataclass_fields(point)} == {
        "angle",
        "distance",
        "confidence",
    }

    scan = registry.types[1]
    fields = {field.name: field for field in _dataclass_fields(scan)}
    assert fields["points"].type == list[point]
    assert fields["checksums"].type == list[int]


def test_construct_builds_nested_typed_records_from_a_decoded_dict() -> None:
    registry = ModelRegistry(_catalog())

    decoded = {
        "generation": 7,
        "points": [
            {"angle": 100, "distance": 2000, "confidence": 200},
            {"angle": 200, "distance": 3000, "confidence": 210},
        ],
        "checksums": [1, 2, 3],
    }

    scan = registry.construct(1, decoded)

    assert scan.generation == 7
    assert scan.checksums == [1, 2, 3]
    assert len(scan.points) == 2
    assert all(type(point) is registry.types[2] for point in scan.points)
    assert scan.points[0].angle == 100
    assert scan.points[1].confidence == 210


def _dataclass_fields(model: type) -> list:
    from dataclasses import fields

    return list(fields(model))
