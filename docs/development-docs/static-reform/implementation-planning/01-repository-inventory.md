# Repository Inventory And Disposition

Status: accepted planning baseline

This inventory describes the working trees observed at the end of the design
pass. It is a migration decision, not a claim that current dirty-worktree
changes should be reverted. Implementation must work with the active
`static_reform` Solar branch and preserve unrelated user changes until their
own stage deliberately replaces them.

## 1. Disposition Vocabulary

| Disposition | Meaning |
| --- | --- |
| **Keep** | The file or artifact remains useful with only routine maintenance. |
| **Reshape** | The implementation contains reusable foundations, but its API, ownership, or coverage must change. |
| **Replace** | The accepted design supersedes the implementation. Delete it when its replacement stage begins. |
| **Remove** | The concept no longer belongs in Solar and receives no compatibility layer. |
| **Defer** | Preserve only until the named later stage or documentation pass; do not build new architecture on it. |

No `Keep` classification exempts code from C++23, naming, Zephyr, error, and
test audits.

## 2. Current Baseline

The current Solar repository is a header-oriented Zephyr module with roughly
12,000 lines across public headers, tests, Kconfig, and Remote schema input.
The current application architecture remains transitional:

- `include/solar/system.hpp` is a roughly 1,400-line positional `System` that
  owns orchestration for board, peripherals, devices, facilities, services,
  tasks, channels, and runtime policy;
- entry profiles pass or infer the old system shape;
- channels and tasks still participate in the old graph vocabulary;
- current Events, Metrics, Logging, and Remote implementations predate their
  accepted specifications;
- kernel wrappers cover a useful portion of Zephyr and are the strongest
  reusable implementation area;
- current CMake and firmware configuration still select C++20;
- current Kconfig mixes useful module scaffolding with obsolete subsystem
  options;
- current tests cover valuable lifecycle intentions but target the superseded
  system architecture.

### 2.1 Baseline verification result

The existing native firmware build is red. It fails because the partially
reformed service runtime requires `static run(StopToken)`, while the old Remote
service does not implement that service contract.

The existing Solar Twister suite cannot currently be loaded completely under
Zephyr 4.4 because `tests/system/graph_compile_fail/testcase.yaml` uses the
unsupported `build_error` property.

These are known baseline facts. Stage 00 establishes a new green module and
test baseline instead of repairing the transitional architecture.

## 3. Solar Repository Root

Repository: `/workspaces/solar`

| Path | Disposition | Action |
| --- | --- | --- |
| `LICENSE` | Keep | Preserve license and headers. |
| `VERSION` | Keep | Preserve, then define reform versioning before release. |
| `.clang-format` | Keep | Re-run against C++23 syntax during Stage 00. |
| `.clangd` | Reshape | Point at the canonical native build and C++23 flags. |
| `.vscode/` | Defer | Keep as local convenience; it is not an architecture dependency. |
| `.gitignore` | Reshape | Ensure generated build, Twister, and hardware-codegen outputs are ignored appropriately. |
| `CMakeLists.txt` | Reshape | Make Zephyr-module use primary, require C++23, add generated include plumbing, and retain only justified standalone support. |
| `zephyr/module.yml` | Reshape | Preserve module registration and add build/codegen hooks when required. |
| `zephyr/Kconfig` | Replace | Rebuild as a structured Solar menu with capability, ceiling, default, binding-mode, and integration symbols from the accepted specs. |
| `README.md` | Defer | Replace only in the final public-documentation pass; keep a minimal truthful build notice during implementation. |
| `docs/` | Defer | Current public docs describe the old architecture. Do not update piecemeal except to mark them pre-reform; rebuild after implementation. |
| `build/` | Remove | Generated output; never use as source or migration evidence beyond recorded baseline failures. |

## 4. Core And Metaprogramming

| Path | Disposition | Replacement responsibility |
| --- | --- | --- |
| `include/solar/core/fixed_string.hpp` | Reshape | Retain compile-time string utility, then align it with descriptor and stable-identity rules. |
| `include/solar/core/type_list.hpp` | Reshape | Retain generic pack algorithms that remain correct; remove positional graph validation and add normalized catalog algorithms. |
| `include/solar/core/time.hpp` | Reshape | Preserve chrono-facing aliases only where they match kernel and descriptor contracts. |
| `include/solar/core/status.hpp` | Replace | Implement the C++23 `std::expected`-based `Result<T, E>` model and the revised narrow `Status`. |
| `include/solar/core/lifecycle.hpp` | Replace | Move accepted lifecycle vocabulary and records to the focused lifecycle subsystem. Remove board/peripheral kinds. |
| `include/solar/core/boot.hpp` | Replace | Implement final `BootReport`, `BootError`, phases, and bounded failure reporting. |
| `include/solar/core/stop.hpp` | Replace | Implement accepted stop report and error semantics with the lifecycle design. |
| `include/solar/core.hpp` | Reshape | Become an intentional convenience aggregate over stable core headers, not a transitive architecture dependency. |
| `include/solar/contribution.hpp` | Replace | Implement generic contribution-source customization, provenance-preserving `CatalogEntry`, alias collection, normalization, and diagnostics. |
| `include/solar/system.hpp` | Replace | Delete the positional system implementation and build `Blueprint`, normalization, `System<Blueprint>`, bindings, effective graph, and static ownership. |
| `include/solar/solar.hpp` | Reshape | Rebuild only after subsystem include boundaries stabilize; avoid forcing every heavy subsystem into every translation unit. |

## 5. Obsolete Object/Profile Architecture

| Path | Disposition | Reason |
| --- | --- | --- |
| `include/solar/entry.hpp` | Remove | Application entry uses the bound global `solar::boot()`/`solar::stop()` API. |
| `include/solar/entry/profile.hpp` | Remove | Profile-owned system and callback orchestration conflict with the static bound system. |
| `include/solar/entry/simulated.hpp` | Remove | Native tests use ordinary test applications and explicit bindings. A future runner must be designed independently if needed. |
| `include/solar/entry/zephyr.hpp` | Remove | Zephyr `main()` directly invokes global Solar lifecycle operations. |
| `include/solar/device/device.hpp` | Remove | Devices are application static types, not instances or required Solar base/tag wrappers. |
| `include/solar/service.hpp` | Replace | Keep the service concept, static hook detection, execution declaration, and stop token under the accepted lifecycle/execution contracts. |
| `include/solar/services/service.hpp` | Remove | Redundant old alias namespace; new service concepts belong in canonical component/execution headers. |
| `include/solar/services.hpp` | Remove | Old aggregate exists only for superseded service aliases. |
| `include/solar/task.hpp` | Replace | Split kernel work primitives from system-integrated `solar::execution`; tasks become leaf registrations. |
| `include/solar/services/channel.hpp` | Remove | Channels are permanently replaced by Bus, kernel queues, direct calls, or component-owned state. |

## 6. Lifecycle And Execution

| Path | Disposition | Action |
| --- | --- | --- |
| `include/solar/lifecycle.hpp` | Replace | Rebuild focused public lifecycle API. |
| `include/solar/lifecycle/storage.hpp` | Reshape | Preserve useful bounded-storage techniques, then align records, mutex behavior, and type-owned ownership. |
| `include/solar/lifecycle/service_execution.hpp` | Replace | Execution owns service records; lifecycle owns component transitions. Implement final activation barrier and stop policy. |
| current service runtime inside `system.hpp` | Replace | Move service preparation, activation, monitoring, and stopping into focused execution internals. |
| current task/executor runtime inside `task.hpp` | Replace | Implement explicit executors, triggers, registrations, workqueue adapters, admission, cancellation, and records. |

## 7. Kernel Wrappers

The kernel directory is reshaped rather than discarded. Each wrapper must be
checked directly against the workspace Zephyr 4.4 API and the expanded kernel
surface in Specs 03, 09, and 12.

| Path | Disposition | Audit focus |
| --- | --- | --- |
| `kernel/config.hpp` | Replace | Remove fallback configuration; use Kconfig and Zephyr capabilities only. |
| `kernel/native.hpp` | Reshape | Keep explicit native escape hatches narrow and documented. |
| `kernel/time.hpp` | Reshape | Preserve chrono ergonomics and exact timeout semantics. |
| `kernel/priority.hpp` | Reshape | Cover cooperative/preemptive/native mapping without hiding Zephyr policy. |
| `kernel/this_thread.hpp` | Reshape | Standardize lowercase namespace and sleep/yield/current-thread operations. |
| `kernel/thread.hpp` | Reshape | Rebuild ownership, static stack storage, start state, stop integration, names, and diagnostics. |
| `kernel/mutex.hpp` | Reshape | Correct mutex versus recursive-mutex semantics and chrono timeout results. |
| `kernel/semaphore.hpp` | Reshape | Add consistent ordinary, non-blocking, and ISR-capable APIs. |
| `kernel/queue.hpp` | Reshape | Preserve typed bounded messages and define copy/move/ISR constraints. |
| `kernel/event_flags.hpp` | Reshape | Align naming and native event semantics. |
| `kernel/poll.hpp` | Reshape | Cover typed poll events and preserve Zephyr poll behavior. |
| `kernel/timer.hpp` | Reshape | Define callback context, ownership, synchronization, and status clearly. |
| `kernel/deadline.hpp` | Reshape | Integrate with focused timing and execution records without inventing a scheduler. |
| `kernel/interrupt.hpp` | Reshape | Expand ISR-context and interrupt utility surface conservatively. |
| `kernel/critical_section.hpp` | Reshape | Keep narrowly scoped interrupt locking; avoid presenting it as a general mutex. |
| `kernel/scheduler.hpp` | Reshape | Expose focused scheduler state and controls supported by Zephyr. |
| `kernel/work.hpp` | Reshape | Become direct typed wrappers over `k_work` and delayable work. |
| `kernel/work_queue.hpp` | Reshape | Become direct typed wrappers over system and owned Zephyr workqueues. |
| `kernel/diagnostics.hpp` | Replace | Remove broad snapshots; expose focused thread and scheduler records with availability. |
| `kernel/kernel.hpp` | Reshape | Rebuild as a stable aggregate after individual primitives settle. |

The expanded target also needs wrappers not represented adequately today,
including memory slabs, message queues where distinct from typed queues,
condition variables where supported and useful, thread iteration/query hooks,
stack-space queries, runtime statistics adapters, fatal hooks, and watchdog
support boundaries. Exact files are introduced in the kernel stage.

## 8. Bus

There is no current Bus implementation to preserve.

| Current source | Disposition | Destination |
| --- | --- | --- |
| `services/channel.hpp` | Remove | Typed Bus for fan-out behavior; kernel queue or component state for point-to-point ownership. |
| `Channels<...>` and graph handling in `core/type_list.hpp`/`system.hpp` | Remove | `solar::Bus<...>` catalog and subscription route normalization. |

Bus is implemented as a new subsystem after blueprint binding and execution
foundations exist.

## 9. Parameters

No current Solar parameter implementation exists in the working tree.
Parameters are a new subsystem. Do not resurrect older parameter code through
`solar_old`; the accepted specification is the only source of architecture.

## 10. Events

| Path | Disposition | Reason |
| --- | --- | --- |
| `include/solar/events.hpp` | Replace | New focused aggregate. |
| `events/catalog.hpp` | Replace | New descriptor, identity, ownership, domain, and policy contract. |
| `events/facility.hpp` | Replace | New bounded ingress/history/accounting and execution integration. |
| `events/record.hpp` | Replace | New canonical event record and evidence model. |
| `events/filter.hpp` | Replace | Align semantic filtering with accepted policies. |
| `events/format.hpp` | Replace | Formatting becomes a non-owning adapter. |
| `events/sink.hpp` | Replace | Replace direct sink ownership with accepted processing/adaptation model. |
| `facilities/events.hpp` | Remove | Built-in inclusion is normalized automatically; users do not list this alias. |

## 11. Metrics

| Path | Disposition | Reason |
| --- | --- | --- |
| `include/solar/metrics.hpp` | Replace | New focused aggregate. |
| `metrics/catalog.hpp` | Replace | Implement accepted metric kinds, descriptors, units, and ownership. |
| `metrics/policy.hpp` | Reshape | Some bounded reducers may be reusable, but policy and concurrency contracts change. |
| `metrics/value.hpp` | Replace | Remove broad `Snapshot`; implement focused typed values and records. |
| `metrics/facility.hpp` | Replace | Implement catalog-derived static storage, concurrency, records, reset, and hooks. |
| `metrics/group.hpp` | Replace | Align groups/views with accepted catalog and frontend model. |
| `facilities/metrics.hpp` | Remove | Built-in inclusion is normalized automatically. |

## 12. Logging

All current Logging headers are replaced. Small formatting and bounded-writer
algorithms may be consulted, but the current direct logger/sink ownership does
not implement the accepted canonical MPSC record stream, early core, Zephyr
frontend, independent sinks, history, or panic model.

| Paths | Disposition |
| --- | --- |
| `include/solar/log.hpp` | Replace |
| `include/solar/log/level.hpp` | Reshape to include Trace, Debug, Info, Notice, Warning, Error, and Fatal. |
| `include/solar/log/source.hpp` | Replace with descriptor-based source/domain identity. |
| `include/solar/log/record.hpp` | Replace. |
| `include/solar/log/logger.hpp` | Replace. |
| `include/solar/log/sink.hpp` | Replace. |
| `include/solar/log/filter.hpp` | Replace. |
| `include/solar/log/format.hpp` | Reshape only after the record/argument model exists. |
| `include/solar/log/writer.hpp` | Replace with leaf sink adapters and transport-specific integration. |

## 13. Remote

The accepted Remote architecture is a complete replacement.

| Path | Disposition | Destination |
| --- | --- | --- |
| `include/solar/remote.hpp` | Replace | New public Remote aggregate. |
| `remote/schema.hpp` | Replace | Data, capabilities, Actions, Topics, Streams, permissions, links, and stable schema. |
| `remote/protocol.hpp` | Replace | Framing, sessions, request/response, flow control, and deterministic protocol. |
| `remote/codec.hpp` | Replace | Deterministic CBOR plus explicit packed Stream encoding. |
| `remote/generated/core.hpp` | Remove | Generated artifacts must come from the new generator/build integration. |
| `remote/generated/manifest.hpp` | Remove | Same. |
| `services/remote.hpp` | Replace | New async protocol service, facility, links, mailboxes/work, and adapters. |
| `remote/solar/core.solar.yaml` | Replace | New canonical schema/generator inputs after protocol schema is accepted in code. |

Remote implementation is split into protocol/generation and runtime/link stages
so the wire contract can become green without physical transport.

## 14. Inspection

| Path | Disposition | Action |
| --- | --- | --- |
| `facilities/inspection.hpp` | Replace | Remove broad snapshot behavior and implement the narrow non-owning collection/formatting facility. |

Inspection arrives only after canonical query providers exist. It must never be
used as temporary subsystem storage during earlier stages.

## 15. Health And Supervision

No current implementation exists. Introduce new `solar::health` facility,
component checks, monitor adapters, records, and Supervisor service only after
lifecycle, kernel diagnostics, execution, Events, Metrics, and Logging expose
their canonical facts.

## 16. Hardware And Devicetree

No current Solar hardware implementation exists. Introduce it from the Zephyr
devicetree and driver APIs rather than porting the old STM32 HAL ownership
model.

| Reference area | Disposition | Action |
| --- | --- | --- |
| `tmp_old_stm32_hardware_system/` in the application repository | Remove after planning | Ergonomic reference has been captured by Spec 13; it must not become production source. |
| generated `edt.pickle` in build trees | Build input only | Read through Zephyr's Python devicetree APIs during CMake generation; never commit the pickle. |

## 17. Solar Tests

| Current suite | Disposition | Action |
| --- | --- | --- |
| `tests/core/lifecycle` | Replace | Preserve useful state-machine cases in new Result/lifecycle tests. |
| `tests/system/lifecycle` | Replace | Rebuild around `Blueprint`, binding, DAG lifecycle, activation barrier, and global APIs. |
| `tests/system/shutdown` | Replace | Rebuild around bounded stop, reverse dependency order, timeout, forced abort, and reports. |
| `tests/system/graph_compile_fail` | Replace | Build a supported compile-fail harness; current Twister schema is invalid under Zephyr 4.4. |
| `tests/task/execution` | Replace | Rebuild around executor registrations and Zephyr workqueue integration. |

Tests are removed only when the replacement stage owns equivalent or stronger
coverage. Stage 00 may delete suites that cannot load, provided it lands the new
test harness and smoke suite in the same checkpoint.

## 18. Firmware Consumer

Repository: `/workspaces/ENMT301-RoboCup/firmware`

| Path | Disposition | Migration point |
| --- | --- | --- |
| `CMakeLists.txt` | Reshaped at Stage 06 | C++23 and the canonical Solar module are selected; later stages add generated hardware/Remote hooks. |
| `prj.conf` | Replaced to Stage 06 baseline | Only foundation-supported Solar Kconfig remains; later subsystem symbols land with their owners. |
| `include/app/robot.hpp` | Replaced at Stage 06 | Owns the minimal `Blueprint`, `System<Blueprint>`, passive application facility, and one binding. |
| `include/system/system.hpp` | Removed at Stage 06 | Relevant future declarations return in focused component/descriptor headers, not a positional runtime header. |
| `include/system/board.hpp` | Removed at Stage 06 | Hardware aliases return through Stages 16-17 using generated devicetree facts. |
| `include/devices/` | Removed at Stage 06; replacement landed at Stage 17 | `DebugUartDevice` now owns the application lifecycle boundary over the generated board Hardware alias. |
| `src/main.cpp` | Replaced at Stage 06 | Calls `solar::boot()` directly and handles its `Result`; no entry profile remains. |
| `include/generated/remote/` | Removed at Stage 06 | The Stage 13 generator will own replacement artifacts. |
| `lib/solar` symlink | Keep | Canonical local module link. |
| `lib/solar_old` symlink | Remove | Hard migration forbids implementation fallback. |

Firmware is not required to build at every Solar stage. Roadmap integration
gates migrate it only when the required foundation is coherent.

## 19. Host Remote Tools

| Area | Disposition | Action |
| --- | --- | --- |
| former `tools/solar-remote-sdk/` | Removed at Stage 20 | The CRC16/socket protocol was incompatible with the accepted Remote protocol. Canonical protocol/client code now lives in Solar `tools/remote/solar_remote`, with exact clients emitted from the final firmware manifest. |
| generated Python manifest/types | Replaced at Stages 13-14 | Final-ELF generation emits manifest CBOR/JSON/digest, constants, and `FirmwareClient`. |
| SDK client/discovery/transport | Core client landed; convenience SDK deferred | The generated client and shared session/framing runtime interoperate with live firmware. Complete transport/discovery convenience remains an accepted Spec 10 extension. |
| SDK tests | Replaced at Stages 13-14 | Golden vectors, generation determinism, malformed protocol behavior, runtime links, sessions, backpressure, and live PTY interoperability are covered in Solar and firmware tests. |
| former `tools/solar-remote-cli/` | Removed at Stage 20 | It depended on the obsolete host protocol. A manifest-rendered convenience CLI remains an accepted later extension rather than a misleading compatibility shell. |

Host-tool migration begins only after the Remote protocol core has stable golden
vectors. It does not block early firmware architecture stages.

## 20. Public Documentation

Current Solar `docs/` and repository `README.md` are not used as implementation
specifications. They are deferred until the public-documentation pass, which
will consume:

- accepted design specs;
- landed implementation summaries;
- final public headers;
- executable examples and tests;
- measured target behavior.

This prevents repeated documentation rewrites during the hard migration.
