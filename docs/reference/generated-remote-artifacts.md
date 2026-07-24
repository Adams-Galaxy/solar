# Generated Remote Artifacts

With `CONFIG_SOLAR_REMOTE_GENERATE_MANIFEST=y`, Solar inspects the linked ELF's
Remote manifest section and writes these deterministic build artifacts:

| File | Purpose |
| --- | --- |
| `manifest.cbor` | Canonical machine-readable manifest |
| `manifest.bin` | Exact embedded image used for discovery and compatibility |
| `manifest.json` | Readable endpoint/schema reference |
| `manifest.sha256` | Firmware/host compatibility digest |
| `constants.py` | Generated stable IDs and protocol constants |
| `client.py` | Typed manifest-specific Python client facade |
| `models.py` | Dataclasses and `IntEnum` models for exported schemas |
| `manifest.hpp` | C++ host/fixture manifest representation |

Artifacts belong to one configured firmware image. Regenerate after any
Blueprint, schema, Kconfig, or endpoint change. Do not hand-edit them or check
them in as canonical source unless a release process intentionally archives a
firmware/host bundle.

The reusable async-first Python SDK lives under `sdk/python`. The generated
client imports that runtime and adds the image-specific schema and endpoint
vocabulary.

`manifest.sha256` is SHA-256 of `manifest.bin`, byte for byte. JSON and
canonical CBOR carry that digest as metadata but are not the hashed source.
