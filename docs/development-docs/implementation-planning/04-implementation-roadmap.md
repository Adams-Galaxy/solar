# Solar Implementation Roadmap

Status: accepted

This roadmap orders implementation by architectural dependency and testability.
It is a hard migration. Stage numbers describe implementation order, not design
specification numbers.

## 1. Roadmap Rules

- A stage may start only after its required predecessors land.
- Internal work may be red while a stage is active.
- Every stage closes at a scoped green Solar checkpoint.
- Firmware builds occur only at the integration checkpoints named here.
- No stage introduces a compatibility layer for the old architecture.
- Each stage produces one landed summary.
- Relaxed binding is default; strict binding is tested wherever applicable.
- Kconfig is the only build configuration source; there is no fallback config
  header.
- Public documentation is a separate pass after implementation closure.

## 2. Stage Overview

| Stage | Capability | Required predecessors | Firmware gate |
| --- | --- | --- | --- |
| 00 | Repository reset and Zephyr/C++23 test foundation | none | no |
| 01 | Modern Result, Status, and core utilities | 00 | no |
| 02 | Identity, descriptors, contributions, and catalogs | 01 | no |
| 03 | Blueprint, System type, binding, and frontend modes | 02 | no |
| 04 | Kernel synchronization, time, queues, poll, and timers | 01 | no |
| 05 | Kernel threads, work, workqueues, diagnostics, and fatal boundaries | 04 | no |
| 06 | Lifecycle, graph, boot, stop, reports, and activation protocol | 03, 05 | **Foundation integration** |
| 07 | Services, executors, tasks, and Zephyr work integration | 06 | no |
| 08 | Typed Bus | 03, 04, 07 | no |
| 09 | Parameters | 03, 04, 07 | no |
| 10 | Observability Events | 03, 04, 07 | no |
| 11 | Metrics | 03, 04 | no |
| 12 | Logging core, Zephyr frontend, sinks, history, and panic | 04, 07 | **Core subsystem integration** |
| 13 | Remote schema, codec, framing, generation, and host vectors | 02, 03 | no |
| 14 | Remote facility, service, links, sessions, streams, and adapters | 07-13 | **Remote integration** |
| 15 | Inspection collections and unified query adapters | 06-14 | no |
| 16 | Hardware/devicetree generator and foundational endpoint wrappers | 01, 04 | no |
| 17 | Hardware driver families and application Device integration | 16 | **Hardware integration** |
| 18 | Health facility, checks, monitors, and evidence | 05-17 | no |
| 19 | Supervisor service, response policy, and watchdog boundary | 07, 18 | no |
| 20 | Full firmware migration, resource audit, and implementation closure | all | **Final native, Teensy, and hardware gates** |

Stages 04-05 and 13 or 16 may be developed on separate branches after their
predecessors land, but the default implementation program remains serial so
cross-stage behavior is reviewed coherently.

## 3. Stage 00: Repository Reset And Build Foundation

Implementation status: landed 2026-07-15

### Objective

Reduce Solar to an honest, buildable Zephyr module foundation and establish the
test machinery used by every later stage.

### Scope

- require C++23 in Solar and test applications;
- rebuild the root CMake and module integration around Zephyr-first use;
- create structured top-level Kconfig with only foundation symbols;
- add `CONFIG_SOLAR_STRICT_CATALOG_BINDING=n` as the default mode symbol;
- remove fallback configuration headers;
- remove the positional System, entry profiles, channels, old tasks, old
  observability implementations, old Remote implementation, generated Remote
  artifacts, and superseded tests;
- retain kernel files marked `Reshape`, but do not expose them as completed;
- establish host, compile-fail, and Zephyr test directory scaffolding;
- implement a supported compile-fail harness for Zephyr 4.4;
- add a minimal native Zephyr module smoke application;
- clean generated build products from source accounting;
- remove the firmware `solar_old` fallback link;
- mark current public docs as pre-reform without rewriting them.

### Explicit exclusions

- no final Result or System API;
- no firmware migration;
- no compatibility aliases;
- no subsystem implementation.

### Green checkpoint

- Solar module is discovered through `module.yml`;
- native smoke app configures, compiles, links, and runs under C++23;
- compile-pass and expected-failure harness self-tests pass;
- Kconfig disabled/enabled smoke variants configure;
- repository contains no active old architecture aggregate;
- test runner loads all retained test metadata without schema errors.

### Landed summary

`landed/00-repository-reset.md`

## 4. Stage 01: Modern Core Result And Utilities

Implementation status: landed 2026-07-15

### Objective

Land the vocabulary every later subsystem can depend upon without creating a
universal error or runtime object.

### Scope

- narrow `Status` and errno/native conversion;
- `Error` conventions and focused subsystem error mapping;
- `Result<T, E>` based on `std::expected`;
- `Result<void, E>` and monadic C++23 use;
- `[[nodiscard]]` policy;
- fixed compile-time strings;
- chrono-facing time primitives independent of kernel waits;
- generic type-list/pack algorithms with no positional graph assumptions;
- assertion/panic boundary declarations only where required by the core spec;
- stable aggregate include boundaries.

### Tests

- host constexpr and monadic composition tests;
- move-only value/error tests;
- error mapping tests;
- no-exception/no-RTTI Zephyr build;
- compile-fail invalid conversion tests;
- C++23 library capability test.

### Green checkpoint

Core host tests and native Zephyr core tests pass with no heap requirement.

### Landed summary

`landed/01-modern-core.md`

## 5. Stage 02: Identity, Contributions, And Catalogs

Implementation status: landed 2026-07-15

### Objective

Implement one generic compile-time identity and catalog system shared by every
subsystem.

### Scope

- `Descriptor` conventions and descriptor concepts;
- local, stable external, owner, source, origin, and provenance identities;
- `CatalogEntry` enrichment without modifying authored descriptor types;
- contribution-source customization point;
- compact conventional aliases: `Messages`, `Parameters`, `Events`, `Metrics`,
  Remote aliases, execution aliases, and Health Checks;
- root contribution sections;
- collection, flattening, ordering, filtering, uniqueness, and provenance;
- focused diagnostics for malformed reserved aliases, duplicate ownership,
  duplicate stable IDs, and invalid descriptors;
- descriptor string stripping capability hooks without implementing every
  subsystem's Kconfig yet.

### Tests

- host compile-pass catalog derivation;
- owner/source/origin provenance assertions;
- empty and large bounded catalogs;
- malformed alias and duplicate compile-fail cases;
- independent library contribution customization;
- deterministic catalog order across translation units.

### Green checkpoint

At least two fake subsystems collect through the same generic mechanism without
hard-coded central contribution traits.

### Landed summary

`landed/02-identity-and-catalogs.md`

## 6. Stage 03: Blueprint, System, Binding, And Frontends

Implementation status: landed 2026-07-16

### Objective

Land the static architecture blueprint and make global Solar APIs feasible in
both relaxed and strict modes.

### Scope

- tagged, order-independent `Blueprint<...>` sections;
- `Devices`, `Facilities`, `Services`, `Executors`, `Execution`, and subsystem
  catalog/configuration section classification;
- component categories: Device, Facility, Service, Executor;
- `Dependencies<...>` and DAG validation;
- effective blueprint and explicit built-in inclusion classes;
- subsystem configuration precedence framework;
- static `System<Blueprint>` type with separated type-owned state slots;
- `system_binding<Application>` customization and binding macros;
- explicit `Of<Application>` alternate test surfaces;
- relaxed typed frontend slot/operation binding infrastructure;
- strict dependent compile-time frontend infrastructure;
- global boot/lifecycle function declarations sufficient for later definition;
- graph and catalog compile-time queries.

### Tests

- section order and omission;
- duplicate/malformed section failures;
- missing dependency and cycle failures;
- contribution-to-effective-catalog trace;
- demand-, Kconfig-, and required-derived inclusion fixtures;
- missing/duplicate application binding failures;
- relaxed ordinary inline method without root visibility;
- strict out-of-line method with root visibility;
- strict/relaxed same API and type assertions;
- multi-translation-unit frontend identity;
- LTO and non-LTO builds.

### Green checkpoint

A representative compile-time robot blueprint normalizes and exposes focused
catalog/graph facts in both modes. Runtime boot is not yet required.

### Landed summary

`landed/03-blueprint-and-binding.md`

## 7. Stage 04: Kernel Core Primitives

Implementation status: landed 2026-07-16

### Objective

Provide typed C++23 ergonomics over Zephyr synchronization and timing without
introducing Solar system integration.

### Scope

- durations, time points, timeouts, deadlines;
- priorities and current-thread operations;
- mutex and recursive mutex as distinct wrappers;
- lock guards and unique locks with accurate capabilities;
- semaphores;
- typed queues/message queues;
- event flags;
- polling and poll events;
- timers;
- interrupt context and scoped interrupt locks;
- scheduler-focused operations;
- native handle escape hatches;
- consistent ordinary, non-blocking, timeout, and ISR forms;
- direct mapping of Zephyr errors into focused Results.

### Tests

- native Zephyr behavior for success, timeout, cancellation, full, empty, and
  unavailable states;
- type and lifetime tests;
- ISR-safe operation tests where supported;
- compile-fail non-trivial queue payload and invalid ISR operation cases;
- no hidden dynamic allocation;
- comparison against relevant Zephyr tests/samples.

### Green checkpoint

Kernel core primitives can be used by an ordinary Zephyr application with no
System, Blueprint, lifecycle, or binding.

### Landed summary

`landed/04-kernel-primitives.md`

## 8. Stage 05: Kernel Threads, Work, And Diagnostics

Implementation status: landed 2026-07-16

### Objective

Complete the kernel surface required by lifecycle, execution, supervision, and
subsystem deferred processing.

### Scope

- statically owned Thread and stack storage;
- start/suspend/resume/join/abort boundaries supported by Zephyr;
- StopSource/StopToken primitive boundary;
- Work and DelayableWork;
- system WorkQueue adapter;
- statically owned WorkQueue;
- memory slabs and other accepted missing primitives;
- focused thread records, stack margin, state, runtime stats availability, and
  thread iteration adapters;
- fatal/error hook boundary needed by lifecycle and logging panic;
- Kconfig dependencies for optional diagnostics;
- no Solar service/executor interpretation in kernel records.

### Tests

- thread lifecycle and static stack tests;
- work submit, resubmit, delay, cancel, drain, and queue stop tests;
- system and owned workqueue tests;
- cooperative stop primitive tests;
- diagnostic available/unavailable variants;
- stack/runtime-stat Kconfig matrix;
- finite timeout and forced abort primitives.

### Green checkpoint

Kernel can prepare a thread or work item without executing user work until an
explicit release action, enabling the lifecycle activation barrier.

### Landed summary

`landed/05-kernel-execution-foundation.md`

## 9. Stage 06: Lifecycle, Graph, Boot, And Stop

Implementation status: landed 2026-07-16

### Objective

Implement complete static-system lifecycle orchestration and establish the
first migrated firmware checkpoint.

### Scope

- lifecycle/system states, operation records, failure subjects, and reports;
- optional exact hook forms for `init`, `start`, `stop`, and `deinit`;
- topological boot and reverse-order rollback/stop;
- absent hook recording;
- component-local last error integration where defined;
- serialized boot/stop and reject-reboot policy;
- built-in lifecycle participation;
- service/executor prepare/activate protocol interfaces;
- final `Running` commit and activation barrier;
- bounded boot and stop reports;
- focused lifecycle and graph queries;
- global `solar::boot()` and `solar::stop()` definitions;
- Kconfig lifecycle defaults and hard ceilings.

### Tests

- full state transition matrix;
- hook return normalization;
- dependency ordering;
- init/start failure rollback;
- stop/deinit failure reporting;
- repeated boot rejection;
- concurrent lifecycle operation serialization;
- no user service/job execution before final commit;
- report capacity and paging;
- strict and relaxed boot applications.

### Foundation firmware integration gate

Hard-migrate firmware to:

- C++23;
- a minimal `Blueprint` and `System<Blueprint>`;
- one `SOLAR_BIND_SYSTEM` declaration;
- direct `solar::boot()` in `main()`;
- no board/peripheral/channel/task/runtime positional groups;
- no old entry profile or `solar_old` link.

Temporarily omit Remote and unfinished subsystems. Build firmware for native
simulation and Teensy 4.0.

### Landed summary

`landed/06-lifecycle-and-system.md`

## 10. Stage 07: Services, Executors, And Tasks

Implementation status: landed 2026-07-16

### Objective

Implement Solar's system-integrated execution plane using the completed Kernel
and lifecycle activation protocol.

### Scope

- static service contract and execution declaration;
- service thread preparation, run, cooperative stop, timeout, and forced abort;
- explicit custom executor component concept;
- Zephyr system workqueue execution target;
- owned workqueue executor adapter;
- task/job registrations and triggers;
- immediate, deferred, periodic, and on-demand work;
- cancellation, admission closure, and stop behavior;
- no hidden default executor;
- Kconfig-selectable system workqueue default;
- focused service/executor/job records and accounting.

### Tests

- activation barrier with real service threads;
- service unexpected exit;
- stop timeout and forced abort;
- system and owned workqueue targets;
- periodic and on-demand registration;
- cancellation before/after queue admission;
- omitted target normalization failure;
- execution records and Kernel correlation;
- stack ownership and no hidden thread tests.

### Green checkpoint

A representative system runs one service, one dedicated registration, and
several shared work registrations with bounded stop and focused records.

### Landed summary

`landed/07-execution.md`

## 11. Stage 08: Typed Bus

Implementation status: landed 2026-07-16

### Objective

Replace Channels permanently with compile-time message catalogs and explicit
route architecture.

### Scope

- message descriptors and `Messages` contributions;
- subscriptions and route normalization;
- inline, queued, deferred, and latest-value delivery;
- source/target ownership and duplicate route diagnostics;
- ordinary, non-blocking, and ISR emission;
- route-owned bounded storage and overflow policy;
- execution-target integration;
- focused route and accounting records;
- relaxed and strict frontends;
- Kconfig capability and hard ceilings.

### Green checkpoint

Multi-producer fan-out works across inline and deferred subscribers; ISR route
compatibility is compile-time validated; no Channel implementation remains.

### Landed summary

`landed/08-bus.md`

## 12. Stage 09: Parameters

Implementation status: landed 2026-07-16

### Objective

Implement typed static system variables with validation, coherent access,
change hooks, and optional persistence.

### Scope

- descriptors, defaults, validation, access, and policy precedence;
- catalog-derived static storage;
- atomic, mutex, and immutable concurrency policies;
- get/set/update/reset and focused records;
- change hooks with inline or execution target policy;
- persistence adapter contract and deferred writes;
- relaxed and strict frontends;
- runtime `NotReady`, `Disabled`, and `NotRegistered` behavior;
- Kconfig capability, accounting, strings, and ceilings.

### Green checkpoint

A control-style component can read and update coherent parameters, receive a
change hook, and exercise fake persistence in both binding modes.

### Landed summary

`landed/09-parameters.md`

## 13. Stage 10: Observability Events

Implementation status: landed 2026-07-16

### Objective

Implement structured historical observations distinct from application Bus
messages.

### Scope

- event descriptors, domains, sources, severity, payloads, and ownership;
- canonical bounded ingress/history and sequence;
- ordinary and ISR observation;
- processors and execution-target integration;
- filtering, accounting, overflow, and reserved critical capacity;
- records and focused queries;
- event-to-metric/log adapter interfaces without requiring those facilities to
  be implemented in the same stage;
- strict/relaxed binding and Kconfig variants.

### Green checkpoint

Thread and ISR producers generate ordered canonical records with explicit loss
accounting and no Bus semantic overlap.

### Landed summary

`landed/10-events.md`

## 14. Stage 11: Metrics

Implementation status: landed 2026-07-16

### Objective

Implement typed current and aggregate measurements with explicit concurrency
and focused records.

### Scope

- counter, gauge, sample, timer, histogram, and accepted view policies;
- units and descriptor schemas;
- catalog-derived static storage;
- atomic, mutex, and external concurrency;
- reset and timestamp policy;
- ordinary and ISR operations;
- event-to-metric adapter now that Events exists;
- strict/relaxed binding and Kconfig variants;
- hot-path relaxed binding benchmark.

### Green checkpoint

Metric kinds, concurrency variants, ISR updates, views, reset, and event
adapters pass native tests with measured relaxed/strict hot-path data.

### Landed summary

`landed/11-metrics.md`

## 15. Stage 12: Logging

Implementation status: landed 2026-07-16

### Objective

Implement Solar's early, structured, bounded logging architecture and Zephyr
frontend integration.

### Scope

- Trace, Debug, Info, Notice, Warning, Error, Fatal levels;
- source/domain descriptors and compile-time filtering;
- bounded argument capture and format validation;
- variable-length MPSC canonical record ring;
- reserved elevated and emergency capacity;
- runtime capture and per-sink filtering;
- shared-execution deferred processing;
- independent leaf sinks and slow-sink isolation;
- retained history;
- Zephyr log frontend and `printk` integration policy;
- event-to-log adapter with recursion prevention;
- Remote adapter interface without Logging depending on Remote;
- flush, shutdown drain, and panic-safe sink behavior;
- Kconfig-selected inclusion and disabled behavior.

### Core subsystem firmware integration gate

Migrate representative firmware components to Parameters, Events, Metrics,
Logging, and Execution as applicable. Build native and Teensy targets. Remote
may remain absent until Stage 14.

### Green checkpoint

Early boot, concurrent capture, overflow, filtering, sink isolation, Zephyr
frontend, event adapter, flush, and panic tests pass. Logging has no dependency
edge on Remote or transport services.

### Landed summary

`landed/12-logging.md`

## 16. Stage 13: Remote Protocol And Generation

Implementation status: landed 2026-07-16

### Objective

Freeze and verify Remote's transport-independent wire, schema, and generated
artifact contract before introducing runtime links.

### Scope

- stable identities and schema fingerprint;
- Data capabilities, Actions, Topics, Streams, links, and permissions schema;
- deterministic CBOR codec;
- explicit packed Stream encoding;
- frame boundaries, integrity, resynchronization, versions, and errors;
- request/response correlation and cancellation messages;
- generation inputs and deterministic firmware/host outputs;
- host SDK protocol core replacement;
- in-memory golden vectors and malformed-frame corpus;
- no physical transport or service thread.

### Green checkpoint

C++ and Python implementations round-trip shared golden vectors; generated
artifacts are deterministic and compile/import; protocol behavior needs no
physical link.

### Landed summary

`landed/13-remote-protocol.md`

## 17. Stage 14: Remote Runtime And Integration

Implementation status: landed 2026-07-16

### Objective

Implement Remote as an async protocol facility and service with replaceable
links and explicit subsystem exposure adapters.

### Scope

- facility/service/link ownership;
- async receive/transmit contracts;
- in-memory and fake-DMA links;
- sessions, negotiation, schema mismatch, permissions, and trust;
- bounded requests/responses and Action execution modes;
- Query, Update, Watch, OutStream, and InStream;
- Push, Poll, and Loaned acquisition;
- source-owned coherent data acquisition;
- topics, streams, batching, fragmentation, sequence, and loss;
- per-session priority queues, credits, and backpressure;
- reconnect/session reset;
- explicit adapters for Parameters, Events, Metrics, Logs, lifecycle,
  execution, graph, and selected Bus facts;
- generated host SDK client integration;
- focused runtime records and accounting.

### Remote firmware integration gate

Add an in-memory/native link and the first real board-supported link where
available. Build native and Teensy firmware. Run host SDK interoperability
against native firmware.

### Green checkpoint

A slow or disconnected client cannot block producers; control responses remain
responsive during streams; no subsystem canonical state is duplicated.

### Landed summary

`landed/14-remote-runtime.md`

## 18. Stage 15: Inspection

Implementation status: landed 2026-07-16

### Objective

Provide narrow generic enumeration, paging, and formatting over existing
canonical descriptors and records.

### Scope

- collection descriptors and provider adapters;
- compile-time descriptor versus runtime record boundaries;
- bounded paging, filtering, and formatting;
- unavailable, disabled, unsupported, and stale distinctions;
- ownership/provenance correlation;
- Remote and local consumers using the same collections;
- no universal snapshot or duplicate storage.

### Green checkpoint

An unfamiliar host can enumerate selected system surfaces through the same
collection providers used by local tests, with bounded costs and no copied
canonical histories.

### Landed summary

`landed/15-inspection.md`

## 19. Stage 16: Hardware Generator And Foundations

Implementation status: landed

### Objective

Make Zephyr devicetree and driver-backed endpoints ergonomic and typed without
hiding or replacing Zephyr ownership.

### Scope

- module CMake hook reading `edt.pickle` through Zephyr Python APIs;
- deterministic generated type aliases, descriptors, capabilities, and
  summaries;
- stable public hardware namespace and endpoint concepts;
- device readiness/error mapping;
- GPIO and interrupt wrappers;
- foundational async completion/callback adapters;
- devicetree fixtures and generated-output tests;
- no System/lifecycle integration;
- no advanced portable DMA abstraction.

### Green checkpoint

Generated aliases compile for native fixtures and Teensy devicetree; GPIO and
interrupt wrappers preserve Zephyr node/spec visibility and error semantics.

### Landed summary

`landed/16-hardware-foundations.md`

## 20. Stage 17: Hardware Driver Families And Devices

Implementation status: landed 2026-07-16

### Objective

Cover the accepted hardware surface and migrate application hardware-backed
Device types.

### Scope

- SPI;
- I2C;
- UART including async/DMA-backed Zephyr driver paths where supported;
- ADC;
- PWM;
- Counter;
- Watchdog;
- capability-specific callback, buffer ownership, timeout, and async contracts;
- explicit native escape hatches for unsupported driver-specific behavior;
- application Device examples with lifecycle and Health-ready boundaries;
- firmware board aliases generated from actual devicetree.

### Hardware firmware integration gate

Migrate firmware board/console/device code to Solar Hardware and application
Device types. Build native and Teensy targets, then run applicable physical GPIO,
UART, bus, timing, and watchdog smoke tests.

### Green checkpoint

Each supported family has devicetree compile coverage, fake/native behavior,
and at least one appropriate target validation. Normal Zephyr driver DMA paths
work without a Solar DMA abstraction.

### Landed summary

`landed/17-hardware-drivers.md`

## 21. Stage 18: Health

Implementation status: landed

### Objective

Implement passive system assessment, component-owned checks, and focused
evidence without assigning recovery policy to the facility.

### Scope

- Health subjects for effective components;
- dimensions, observations, condition, readiness, availability, degradation,
  and failure records;
- component `assess()` and optional nested named Checks;
- push reports and compact ISR ingress;
- progress, stack margin, execution, signal, and checker monitors;
- adapters for lifecycle, Kernel, Execution, Events, Metrics, Logging, Remote,
  and hardware facts;
- bounded evidence references and histories;
- cadence, freshness, unavailable, and stale behavior;
- Kconfig-selected inclusion;
- no active recovery policy.

### Green checkpoint

Fake IMU connection failure, service progress stall, stack-margin warning, and
Remote degradation produce coherent Health assessments without duplicating
source histories.

### Landed summary

`landed/18-health.md`

## 22. Stage 19: Supervisor

Implementation status: landed

### Objective

Implement bounded active monitoring and response policy over Health and
execution facts.

### Scope

- Supervisor service and progress record;
- periodic assessment at low configurable cadence;
- policy mapping from Health conditions to notify, retry, recover, isolate,
  stop, abort, watchdog, or fatal responses;
- component-owned recovery/action hooks where accepted;
- cooldown, escalation, attempt limits, and recursion prevention;
- thread stall and stack risk handling;
- watchdog provider boundary and fake provider tests;
- Supervisor self-health without recursive response;
- focused response records and evidence correlation.

### Green checkpoint

Deterministic fake-clock tests cover warning, recovery success/failure,
escalation, service stall, Supervisor stall/watchdog starvation, and bounded
shutdown response.

### Landed summary

`landed/19-supervisor.md`

## 23. Stage 20: Full Integration And Closure

Implementation status: landed

### Objective

Prove the implemented Solar architecture works as one bounded Zephyr-native
system and produce the complete public-documentation handoff.

### Scope

- complete firmware composition root and component migration;
- remove every remaining old Solar API and generated artifact;
- full native Solar test suite;
- strict and relaxed complete-system builds;
- native and Teensy firmware builds;
- host Remote SDK/CLI interoperability;
- applicable physical target smoke tests;
- static storage, stack, thread, workqueue, timer, and binary-size inventory;
- Kconfig capability/exclusion audit;
- compile-time and diagnostic quality audit;
- include-cost and representative build-time measurement;
- final implementation inventory update;
- verify all stage summaries;
- create public-documentation input index.

### Final green checkpoint

- all landed Solar tests pass;
- all compile-fail tests fail for expected reasons;
- relaxed default firmware passes native and target gates;
- strict firmware build passes;
- no hidden heap, thread, executor, or canonical state exists;
- no component header includes the composition root;
- no old positional System, Channels, entry profile, or context object remains;
- the resource model is measured and documented;
- all accepted specifications map to implementation or an explicit accepted
  deferral;
- the public-documentation pass can begin without reverse-engineering history.

### Landed summary

`landed/20-integration-closure.md`

## 24. Public Documentation Handoff

Public docs are not Stage 21 of implementation. They are a separate pass using:

```text
accepted design specs
    + landed stage summaries
    + final headers and generated references
    + executable examples/tests
    + measured resource and target behavior
```

That pass should build conceptual guides, API references, Zephyr integration
instructions, Kconfig reference, subsystem guides, migration-free examples,
Remote host documentation, hardware/devicetree documentation, and a tested
representative robot tutorial.

## 25. Accepted Later Extensions

The core implementation roadmap intentionally leaves these extensions until
measured need or platform maturity justifies them:

- controlled in-process reboot and complete subsystem reset semantics;
- cached relaxed-mode handles for measured hot paths;
- broader RTIO integration beyond hardware operations that benefit directly;
- persistent crash-history formats and reboot-spanning diagnostic retention;
- advanced SoC-specific DMA helpers outside normal Zephyr driver paths;
- additional hardware driver families beyond the accepted GPIO, SPI, I2C,
  UART, ADC, PWM, Counter, and Watchdog surface;
- compatibility across multiple Zephyr release lines;
- C++26 reflection migration after toolchain and Zephyr support mature;
- optional dynamic-allocation implementations for capabilities that can prove a
  meaningful advantage while preserving static core operation.

These are not compatibility work and do not justify placeholders in the core
architecture.

## 26. Initial Tracker

| Stage | Status | Summary |
| --- | --- | --- |
| 00 Repository reset | landed | `landed/00-repository-reset.md` |
| 01 Modern core | landed | `landed/01-modern-core.md` |
| 02 Identity/catalogs | landed | `landed/02-identity-and-catalogs.md` |
| 03 Blueprint/binding | landed | `landed/03-blueprint-and-binding.md` |
| 04 Kernel primitives | landed | `landed/04-kernel-primitives.md` |
| 05 Kernel execution foundation | landed | `landed/05-kernel-execution-foundation.md` |
| 06 Lifecycle/System | landed | `landed/06-lifecycle-and-system.md` |
| 07 Execution | landed | `landed/07-execution.md` |
| 08 Bus | landed | `landed/08-bus.md` |
| 09 Parameters | landed | `landed/09-parameters.md` |
| 10 Events | landed | `landed/10-events.md` |
| 11 Metrics | landed | `landed/11-metrics.md` |
| 12 Logging | landed | `landed/12-logging.md` |
| 13 Remote protocol | landed | `landed/13-remote-protocol.md` |
| 14 Remote runtime | landed | `landed/14-remote-runtime.md` |
| 15 Inspection | landed | `landed/15-inspection.md` |
| 16 Hardware foundations | landed | `landed/16-hardware-foundations.md` |
| 17 Hardware drivers | landed | `landed/17-hardware-drivers.md` |
| 18 Health | landed | `landed/18-health.md` |
| 19 Supervisor | landed | `landed/19-supervisor.md` |
| 20 Integration closure | landed | `landed/20-integration-closure.md` |
