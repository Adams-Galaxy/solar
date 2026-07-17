# Generated Remote Artifacts

With `CONFIG_SOLAR_REMOTE_GENERATE_MANIFEST=y`, Solar inspects the linked ELF's
Remote manifest section and writes these deterministic build artifacts:

| File | Purpose |
| --- | --- |
| `manifest.cbor` | Canonical machine-readable manifest |
| `manifest.json` | Readable endpoint/schema reference |
| `manifest.sha256` | Firmware/host compatibility digest |
| `constants.py` | Generated stable IDs and protocol constants |
| `client.py` | Typed manifest-specific Python client facade |
| `manifest.hpp` | C++ host/fixture manifest representation |

Artifacts belong to one configured firmware image. Regenerate after any
Blueprint, schema, Kconfig, or endpoint change. Do not hand-edit them or check
them in as canonical source unless a release process intentionally archives a
firmware/host bundle.

The reusable transport-independent Python runtime lives under
`tools/remote/solar_remote`. The generated client imports that runtime and adds
the image-specific schema and endpoint vocabulary.
