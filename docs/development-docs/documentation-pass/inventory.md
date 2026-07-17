# Documentation Surface Inventory

Status: active baseline

Date: 2026-07-17

## Repository Surface

| Surface | Current count | Authority |
| --- | ---: | --- |
| public aggregate headers | 19 | `include/solar/*.hpp` |
| total Solar headers | 193 | `include/solar/**/*.hpp` |
| Solar Kconfig symbols | 173 | `zephyr/Kconfig` |
| accepted static-reform design specs | 16 | `docs/development-docs/static-reform/design-specs/` |
| landed implementation records | 22 | `docs/development-docs/static-reform/implementation-planning/landed/` |
| focused Zephyr test applications | 58 | `tests/zephyr/` |

The public aggregate headers are `solar/solar.hpp`, `version.hpp`, `core.hpp`,
`component.hpp`, `catalog.hpp`, `system.hpp`, `lifecycle.hpp`, `kernel.hpp`,
`execution.hpp`, `hardware.hpp`, `bus.hpp`, `parameters.hpp`, `events.hpp`,
`metrics.hpp`, `log.hpp`, `remote.hpp`, `inspection.hpp`, `health.hpp`, and
`supervisor.hpp`.

## Public Boundary Classification

Headers are classified by role rather than by whether a user can technically
include them.

| Role | Normal classification | Documentation treatment |
| --- | --- | --- |
| aggregate headers | public | primary include and subsystem landing pages |
| `api.hpp` | public | complete API reference |
| `declaration.hpp`, declaration-focused headers | public | declaration guides and reference |
| `types.hpp`, `policy.hpp`, `contribution.hpp` | public | grouped subsystem reference |
| `catalog.hpp`, `descriptor.hpp`, identity helpers | public/advanced | concepts and exact reference |
| Kernel primitive headers | public | one page per primitive family |
| Hardware driver headers | public | one page per supported driver family |
| `protocol.hpp` | advanced public customization | reference with extension warnings |
| `facility.hpp`, `service.hpp` | advanced public ownership types | subsystem and architecture reference |
| `runtime.hpp`, `storage.hpp`, `service_runtime.hpp` | internal implementation | architecture links only; hidden from normal API navigation |
| namespace `detail` | internal | excluded from public API reference |
| generated headers | generated public | generated-reference pages, never hand-copied |

An exception to this table must be recorded in the coverage matrix. Mere
reachability through an aggregate header does not make an implementation helper
a supported extension point.

## Subsystem Inputs

| Topic | Aggregate | Design input | Landed evidence | Executable evidence |
| --- | --- | --- | --- | --- |
| core/errors | `solar/core.hpp` | specs 00, 00a; error refinement | stages 01, 21 | host core, Zephyr core |
| catalog/component | `solar/catalog.hpp`, `component.hpp` | specs 00, 02 | stage 02 | catalog tests |
| system/binding | `solar/system.hpp` | specs 01, 14 | stages 03, 20 | system strict/relaxed tests |
| lifecycle | `solar/lifecycle.hpp` | spec 03 | stage 06 | lifecycle tests |
| Kernel | `solar/kernel.hpp` | specs 03, 12 | stages 04, 05 | kernel tests |
| execution | `solar/execution.hpp` | spec 09 | stage 07 | execution tests |
| hardware | `solar/hardware.hpp` | spec 13 | stages 16, 17 | hardware tests and generator |
| Bus | `solar/bus.hpp` | spec 04 | stage 08 | Bus tests |
| Parameters | `solar/parameters.hpp` | spec 05 | stage 09 | Parameters tests |
| Events | `solar/events.hpp` | spec 06 | stage 10 | Events tests |
| Metrics | `solar/metrics.hpp` | spec 07 | stage 11 | Metrics tests |
| Logging | `solar/log.hpp` | spec 08 | stage 12 | Logging tests |
| Remote | `solar/remote.hpp` | spec 10 | stages 13, 14 | Remote tests, vectors, generator |
| Inspection | `solar/inspection.hpp` | spec 11 | stage 15 | Inspection tests |
| Health | `solar/health.hpp` | spec 12 | stage 18 | Health tests |
| Supervisor | `solar/supervisor.hpp` | spec 12 | stage 19 | Supervisor tests |

## Generated Inputs

- Kconfig reference is derived from `zephyr/Kconfig` using Zephyr's
  `kconfiglib` environment.
- Hardware reference is derived from controlled `native_sim` and Teensy
  devicetree generation fixtures.
- Remote reference is derived from protocol declarations, vectors under
  `tests/vectors/`, and controlled manifest generation.
- C++ API declarations are derived from Doxygen XML over `include/solar/`.

## Existing Material Assessment

The top-level README is stale: it describes reform as incomplete, points to a
companion workspace for design records, and names only the former foundation
smoke test. Stage 2 replaces it.

There is no current public Sphinx tree, Doxygen configuration, documentation
dependency manifest, documentation CI, or buildable examples directory. Stage
1 and Stage 2 create them.

The focused tests are trustworthy behavioral evidence but are not generally
suitable as first-contact tutorials. Canonical user examples will live under
`examples/` and use test fixtures only where a controlled fake is essential.

## Known Documentation Risks

- Many public declarations currently have no Doxygen contract comments.
- Template-heavy internals can overwhelm generated API output unless explicitly
  grouped and filtered.
- Kconfig defaults and dependencies must be generated, not transcribed.
- Generated hardware names differ by board and must not be presented as
  universal aliases.
- Remote has multiple data-flow modes; isolated snippets can accidentally omit
  required execution or link ownership.
- Strict binding is opt-in and must not become the default tutorial ceremony.
- The generated hardware compile test currently reports one Twister case with
  no explicit status despite its configuration passing.

## Stage 0 Decisions

- Public documentation is rebuilt without compatibility redirects.
- The common path uses relaxed binding; strict binding is an advanced guide and
  tested variant.
- Public reference is grouped by supported concepts, not emitted as a flat list
  of every parsed declaration.
- Development specs and landed summaries remain private source material.
- Public examples use only the landed `Result<T, E>` convention.
