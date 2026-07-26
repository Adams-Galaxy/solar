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
