# Phase 0 Baseline And Prototype Disposition

Date: 2026-08-03

Status: verified baseline

## Supported Baseline

The production work uses Linux with GCC as its supported host baseline. The
recorded environment is:

- base repository commit: `854a12436a819175a95b2f16398360647dc36b13`;
- Linux host compiler: GCC 14.4.0;
- Zephyr fixture compiler: GCC 13.3.0;
- Zephyr: 4.4.0, build `aa04d57513a5`;
- Python: 3.13.5 for the Linux host suite and 3.12.3 for Zephyr generation;
- CMake: 4.4.0 on macOS and 3.28.3 in the Zephyr container; and
- West: 1.5.0.

AppleClang 16 is not a supported legacy-host baseline. It rejects the legacy
`consteval` diagnostic and manifest-writing patterns, and one compilation can
crash the compiler. The production implementation must not reproduce those
patterns; Clang support is an explicit hardening target.

## Evidence

- GCC 14 clean host configure/build: successful.
- GCC 14 host CTest: 77/77 passing.
- Strict compiler tests: 17/17 passing.
- Zephyr 4.4 `native_sim/native/64` clean build: successful.
- Generated Python client over real TCP: parameter get/set, action, output
  stream, input stream, dynamic access, interface rejection, disconnect, and
  rebind all successful.
- Simulator process restart followed by the same client exercise: successful.
- Clean repeated generation: approximately 30 ms per run; byte determinism is
  covered by the compiler suite.
- Generated artifact size for the representative contract: 48,587 bytes.
- `native_sim` fixture ELF: 224,448 bytes text, 63,828 bytes data, 76,926 bytes
  BSS.
- Canonical static parameter storage: one 32-byte symbol.

## Representative Contract

`tests/prototype/robot.solar.yaml` is the migration contract until production
fixtures replace its path. It deliberately contains:

- closed and open enums;
- bounded scalar parameter metadata;
- an action with request and response structures;
- an output stream;
- an input stream; and
- an additive/rename evolution path exercised by tests.

## Prototype Artifact Disposition

| Artifact | Disposition | Production destination |
| --- | --- | --- |
| `tools/prototype/solar_codegen.py` | rewrite/promote | versioned compiler package below `tools/solar_codegen` |
| `tools/prototype/generate.py` | replace | installed/versioned `solar-codegen` CLI entry point |
| `tools/prototype/generate_shipment.py` | promote after ELF contract redesign | shipment backend in compiler package |
| `include/solar/prototype/parameters.hpp` | rewrite/promote | `solar::parameters::Store` and `StaticStore` |
| `include/solar/prototype/composition.hpp` | rewrite/promote | generic production `solar::System` composer |
| `include/solar/prototype/participation.hpp` | rewrite/promote | production contribution and dispatch machinery |
| `tests/prototype/*` | migrate | production compiler fixtures |
| `tests/host/prototype_modules.cpp` | migrate | module/parameter conformance tests |
| `tests/host/prototype_generated.cpp` | migrate | generated contract/composition test |
| prototype compile-fail fixtures | migrate | production diagnostic fixtures |
| `tests/zephyr/prototype_full_slice` | rewrite/migrate | production generated full-slice fixture |
| generated nullable callback declarations | delete | direct compile-time component dispatch |
| legacy Remote fixture adapter | delete | standalone Remote plus explicit adapters |

Python bytecode cache files are never production artifacts and are excluded
from the disposition and release outputs.

## Closed Defects

The identity allocator now distinguishes authored identity from generated hash
identity. Only generated hash collisions probe. An explicit ID that collides
with an active or retired declaration is rejected. Tests also cover malformed
bounds, incompatible bound types, unknown rename sources, valid rename chains,
attempts to skip back to a retired rename source, and unsupported lock formats.
