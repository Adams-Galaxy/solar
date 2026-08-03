# Application Ergonomics Implementation Record

Date: 2026-08-03

Status: active

This is the anti-drift implementation record for the accepted
[Application ergonomics and Kconfig design](application-ergonomics-and-kconfig.md).
It builds production scaffolding directly; no prototype runtime or compatibility
application layer is permitted.

## Preserved robot baseline

- Authored interface SHA-256:
  `4a72e86b57cd03e8b7d133efe90a0b61f15ef379aaeb59c2d04bd44205dafad1`.
- Linked shipment interface SHA-256:
  `c8aed4857174eb3b3c4db1c90828e1d3cbd1df040da53600cc8e609118ad2d39`.
- Optimized Teensy image: 307,272 bytes flash, 198,040 bytes RAM, 2 KiB DTCM.
- Cockpit active after approximately 22 ms.
- USB request latency: 105.024 ms median, 105.535 ms p95, 107.658 ms maximum.
- Clean optimized build: approximately 30.37 seconds.
- Verified behavior: retained console history, Remote over USB, neutral 20 Hz
  control, encoders, exclusive input replacement, soft reflash, and rebind.

These values are comparison gates. The two interface digests have different
roles: the authored digest covers normalized wire declarations, while the
shipment digest covers the effective linked interface.

## Milestones

- [x] Preserve the representative robot-shaped production fixture.
- [x] Generate hierarchical C++ names, application identity traits, and typed
  endpoint binding aliases without changing wire metadata.
- [x] Teach contribution validation and dispatch to consume one service
  `Endpoints` table while retaining explicit `Contributions`.
- [x] Add the initial high-level Application compiler, parameter elision,
  conventional-device adaptation, and service runner policies.
- [x] Complete synthesized logging, Remote, platform links, and dependency
  ordering.
- [x] Complete Kconfig-driven automatic generation, explanations, Python, and
  linked shipment integration across the build matrix.
- [x] Migrate the robot application, Cockpit, and board platform; remove all
  superseded glue.
- [x] Run host, compile-fail, codegen, Python, Twister, simulator, and Teensy
  acceptance gates and compare the baseline.
- [x] Update public documentation and bump Solar to 0.2.0.
- [x] Create the release
  commit, and create a local annotated `v0.2.0` tag. Never push the tag as part
  of this plan.

## Drift guards

- Wire IDs, authored digest, USB framing, simulator framing, and Station-facing
  Python behavior remain compatible.
- Kconfig controls availability and bounded defaults, never interface identity
  or endpoint ownership.
- Normal builds only read the interface lock.
- Generated integration is attached to participating targets through include
  paths and a target definition; compiler-wide forced includes are forbidden.
- `solar::system::System`, explicit composition, and standalone modules remain
  supported advanced APIs.
- Hardware motion remains neutral unless separately authorized.

## Evidence log

### Generated bindings and initial Application compiler

Date: 2026-08-03

Commands: Clang 22 host configure/build and CTest.

Result: 67/67 existing host, Python, generator, and compile-fail tests pass.
The permanent fixture statically proves that `solar::System<Application>`
desugars to the equivalent explicit composition.

### Completed vertical slice

Date: 2026-08-03

- LLVM 22 host/API, Python SDK, generator, shipment, and compile-fail suite:
  68/68 pass.
- Zephyr Twister on Linux `native_sim/native/64`: 15/15 Solar configurations
  pass (13 in the full run, followed by the two corrected standalone Remote
  configurations); maintained examples: 2/2 pass.
- Native robot simulator and generated Python client exercise mode updates,
  state and actuator reads, Differential input open/send/close, replacement,
  and log transport.
- Optimized/LTO Teensy image: 303,364 bytes flash, 198,040 bytes RAM, 2 KiB
  DTCM. This saves 3,908 bytes of flash with no RAM regression.
- Teensy linked interface SHA-256 remains
  `c8aed4857174eb3b3c4db1c90828e1d3cbd1df040da53600cc8e609118ad2d39`.
- Optimized firmware flashed through the 134-baud integration. Retained USB
  console history, generated-client USB binding, Safe mode, state reads, and
  neutral track feedback were verified without commanding motion.
- Homebrew clangd checks of the firmware entry point and Cockpit header report
  no source diagnostics when using the configured Zephyr cross-driver query.
