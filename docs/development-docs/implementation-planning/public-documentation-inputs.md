# Public Documentation Input Index

Status: implementation handoff

Date: 2026-07-16

This index is the starting point for the separate Solar public-documentation
pass. It identifies accepted intent, exact landed behavior, executable examples,
generated references, and measured target evidence. Public docs should not need
to reconstruct implementation history from Git diffs.

## 1. Architecture And Language

| Topic | Accepted design | Landed evidence |
| --- | --- | --- |
| conventions and static architecture | `design-specs/00-design-conventions.md` | `landed/00-repository-reset.md`, `landed/02-identity-and-catalogs.md` |
| C++23 Result and errors | `design-specs/00a-modern-cpp-result-and-status.md` | `landed/01-modern-core.md` |
| Blueprint and binding | `design-specs/01-system-blueprint-and-binding.md` | `landed/03-blueprint-and-binding.md` |
| identity and contributions | `design-specs/02-identity-contributions-and-catalogs.md` | `landed/02-identity-and-catalogs.md` |
| integrated architecture | `design-specs/14-integrated-architecture.md` | `landed/20-integration-closure.md` |

## 2. Runtime Foundations

| Topic | Accepted design | Landed evidence |
| --- | --- | --- |
| Kernel | `design-specs/03-lifecycle-kernel-and-configuration.md`, `design-specs/12-health-and-supervision.md` | `landed/04-kernel-primitives.md`, `landed/05-kernel-execution-foundation.md` |
| Lifecycle and System | `design-specs/03-lifecycle-kernel-and-configuration.md` | `landed/06-lifecycle-and-system.md` |
| Execution | `design-specs/09-tasks-and-executors.md` | `landed/07-execution.md` |

## 3. Facilities And Services

| Topic | Accepted design | Landed evidence |
| --- | --- | --- |
| Bus | `design-specs/04-bus.md` | `landed/08-bus.md` |
| Parameters | `design-specs/05-parameters.md` | `landed/09-parameters.md` |
| Events | `design-specs/06-events.md` | `landed/10-events.md` |
| Metrics | `design-specs/07-metrics.md` | `landed/11-metrics.md` |
| Logging | `design-specs/08-logging.md` | `landed/12-logging.md` |
| Remote | `design-specs/10-remote.md` | `landed/13-remote-protocol.md`, `landed/14-remote-runtime.md` |
| Inspection | `design-specs/11-inspection.md` | `landed/15-inspection.md` |
| Health and Supervisor | `design-specs/12-health-and-supervision.md` | `landed/18-health.md`, `landed/19-supervisor.md` |
| Hardware and devicetree | `design-specs/13-hardware-and-devicetree.md` | `landed/16-hardware-foundations.md`, `landed/17-hardware-drivers.md` |

## 4. Executable Examples

- `firmware/include/app/robot.hpp`: complete composition root, generated board
  Hardware alias, application Device, contributions, Remote adapters and link.
- `firmware/src/main.cpp`: global `solar::boot()` and ordinary subsystem calls.
- `solar/tests/zephyr/*/src/main.cpp`: focused native examples for every bound
  subsystem, strict/relaxed behavior, concurrency, ISR, and lifecycle.
- `solar/tests/host`: portable C++23 catalog, protocol, and generation examples.
- `solar/tests/zephyr/supervisor`: explicit recovery, safe state, stall response,
  watchdog provider, and early-wake example.

## 5. Generated References

- Hardware: `build/.../zephyr/include/generated/zephyr/solar/hardware/generated/devicetree.hpp`.
- Remote: `build/.../solar/remote/manifest.{cbor,json,hpp,sha256}`,
  `constants.py`, and `client.py`.
- Kconfig: `solar/zephyr/Kconfig` is the authoritative capability and capacity
  surface.
- Public aggregate headers: `solar/include/solar/*.hpp`.

## 6. Verification And Measurements

- Exact commands, fixture capacities, target results, and binary/storage sizes
  are recorded in each `landed/NN-*.md` file.
- Final cross-system results and accepted unexecuted physical gates are recorded
  in `landed/20-integration-closure.md`.
- The final strict native firmware build is the representative tutorial image.
- Teensy 4.0 compile/link and family-specific devicetree evidence are recorded in
  Stages 16-17.
- Live host interoperability is exercised by
  `firmware/scripts/check_remote_native.py` against the generated client.

## 7. Explicit Later Documentation

The public pass should clearly label accepted extensions rather than document
them as current behavior. Important examples are controlled in-process reboot,
complete convenience host SDK/CLI generation, additional hardware families,
advanced SoC-specific DMA helpers, broader transports, and C++26 reflection.
