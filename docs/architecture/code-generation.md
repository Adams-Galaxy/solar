# Code Generation

Solar has two deterministic generation pipelines.

## Hardware EDT generation

After Zephyr resolves DTS and overlays, the Hardware generator reads
`edt.pickle` through Zephyr's Python devicetree library. It emits C++ selectors
and JSON metadata. Inputs are final EDT, Hardware Kconfig, bindings, and the
generator version. Output adds no runtime registry.

## Remote post-link generation

Remote declarations emit a compact manifest section into `zephyr.elf`. The
post-link generator validates stable IDs and schema/capability relations, then
emits canonical CBOR, JSON, digest, constants, and host clients. The linked
image is authoritative because dead-code/configuration selection has already
resolved its effective surface.

## Documentation generation

Kconfiglib generates the complete configuration page from `zephyr/Kconfig`.
Doxygen emits XML from public headers; Breathe and authored MyST pages render
the curated API reference. Canonical examples are included by named source
regions and compiled independently.

Generated files must be reproducible and changed only through canonical input
or generator code. Tests compare protocol vectors, stable ordering, digests,
and fixture output to detect drift.
