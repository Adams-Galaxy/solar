from __future__ import annotations

import cbor2

from solar_remote.codec import DynamicCodec


def test_cbor_float32_is_quantized_to_the_declared_wire_width() -> None:
    codec = object.__new__(DynamicCodec)
    codec.schemas = {
        1: {
            "name": "fixture.Pair",
            "codec": "cbor",
            "max_encoded_size": 24,
            "fields": [
                {
                    "id": 1,
                    "name": "left",
                    "kind": "float",
                    "width": 32,
                    "required": True,
                },
                {
                    "id": 2,
                    "name": "right",
                    "kind": "float",
                    "width": 32,
                    "required": True,
                },
            ],
        }
    }

    encoded = codec.encode(1, {"left": 0.4, "right": -0.1})

    assert b"\xfa" in encoded
    decoded = cbor2.loads(encoded)
    assert decoded[1] != 0.4
    assert abs(decoded[1] - 0.4) < 1e-6


def _point_field(id_: int, name: str, width: int) -> dict:
    return {
        "id": id_,
        "name": name,
        "kind": "unsigned",
        "width": width,
        "required": True,
        "maximum_length": 0,
        "schema": None,
        "element_kind": None,
    }


def _scan_schemas() -> dict[int, dict]:
    return {
        2: {
            "name": "fixture.Point",
            "shape": "record",
            "fields": [
                _point_field(1, "angle", 16),
                _point_field(2, "distance", 16),
                _point_field(3, "confidence", 8),
            ],
        },
        1: {
            "name": "fixture.Scan",
            "codec": "cbor",
            "max_encoded_size": 256,
            "fields": [
                {
                    "id": 1,
                    "name": "generation",
                    "kind": "unsigned",
                    "width": 64,
                    "required": True,
                    "maximum_length": 0,
                    "schema": None,
                    "element_kind": None,
                },
                {
                    "id": 2,
                    "name": "points",
                    "kind": "array",
                    "width": 0,
                    "required": True,
                    "maximum_length": 4,
                    "schema": 2,
                    "element_kind": "schema",
                },
                {
                    "id": 3,
                    "name": "checksums",
                    "kind": "array",
                    "width": 16,
                    "required": True,
                    "maximum_length": 4,
                    "schema": None,
                    "element_kind": "unsigned",
                },
            ],
        },
    }


def test_array_and_record_round_trip_through_cbor() -> None:
    codec = object.__new__(DynamicCodec)
    codec.schemas = _scan_schemas()

    value = {
        "generation": 7,
        "points": [
            {"angle": 100, "distance": 2000, "confidence": 200},
            {"angle": 200, "distance": 3000, "confidence": 210},
        ],
        "checksums": [1, 2, 3],
    }

    encoded = codec.encode(1, value)
    raw = cbor2.loads(encoded)
    # Wire keys are field IDs, and nested records are also ID-keyed maps.
    assert raw == {1: 7, 2: [{1: 100, 2: 2000, 3: 200}, {1: 200, 2: 3000, 3: 210}], 3: [1, 2, 3]}

    decoded = codec.decode(1, encoded)
    assert decoded == value


def test_array_bound_is_enforced_on_encode() -> None:
    codec = object.__new__(DynamicCodec)
    codec.schemas = _scan_schemas()

    value = {
        "generation": 1,
        "points": [],
        "checksums": [1, 2, 3, 4, 5],
    }

    try:
        codec.encode(1, value)
    except Exception as error:  # noqa: BLE001
        assert "checksums" in str(error)
    else:
        raise AssertionError("expected an over-capacity array to be rejected")
