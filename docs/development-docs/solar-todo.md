# Solar Implementation Tracker

Status: implementation complete; public documentation handoff ready

Current stage: **complete**

This is the working progress tracker for the Solar static-system reform. It is
intentionally broad. Detailed architecture, stage scope, dependencies, tests,
and closure gates belong to the linked planning artifacts and are not repeated
here.

## 1. Authoritative Inputs

Implementation must follow:

- `development-docs/design-specs/00-design-conventions.md` through
  `development-docs/design-specs/14-integrated-architecture.md`;
- `development-docs/implementation-planning/README.md`;
- `development-docs/implementation-planning/01-repository-inventory.md`;
- `development-docs/implementation-planning/02-dependency-map.md`;
- `development-docs/implementation-planning/03-stage-workflow-and-verification.md`;
- `development-docs/implementation-planning/04-implementation-roadmap.md`;
- `development-docs/implementation-planning/landed-summary-template.md`.

When this tracker and a detailed planning artifact differ, the accepted design
specifications and implementation-planning documents take precedence. Update
this tracker to reflect the correction.

## 2. Locked Reform Policy

- [x] The design pass is complete and accepted.
- [x] Migration is hard and intentionally breaking.
- [x] Solar requires no source compatibility with the old architecture.
- [x] No deprecated aliases, compatibility adapters, dual runtime, or in-tree
      legacy archive will be maintained.
- [x] Git history is the archive.
- [x] Relaxed catalog binding is the default development mode.
- [x] Strict catalog binding remains a supported, tested build mode.
- [x] Solar targets C++23 and the workspace-pinned Zephyr baseline.
- [x] Kconfig is the build configuration source; no fallback `config.hpp`
      configuration exists.
- [x] `native_sim/native/64` is the primary runtime test platform.
- [x] Teensy 4.0 is the primary firmware compile and physical validation target.
- [x] Public documentation is rebuilt only after implementation closure.

## 3. Progression Rules

### 3.1 Normal autonomous progression

For every stage:

1. read its roadmap contract and all referenced specifications;
2. inspect the current Solar, Zephyr, firmware, test, and generated-code context;
3. establish or update the stage test scaffold;
4. remove or reshape superseded implementation in scope;
5. implement progressively;
6. test, measure, and adjust;
7. satisfy the stage's declared green checkpoint;
8. write the landed summary;
9. update this tracker;
10. immediately continue into the next unblocked stage.

Do not stop merely to report routine stage completion. When a stage lands
without a major unresolved architectural decision, mark it complete, write its
summary, and continue.

Intermediate work inside an active stage may be red. A stage may be marked
complete only when its scoped green checkpoint and applicable regression gates
pass.

### 3.2 Local implementation decisions

An implementation decision may be made autonomously when it:

- preserves accepted public semantics and ownership;
- does not materially reorder or invalidate later stages;
- does not introduce a new hidden runtime owner, thread, heap requirement, or
  cross-subsystem dependency;
- does not change stable protocol or external identity;
- has a clear and reasonably contained reversal path.

Record every non-obvious local decision in the stage landed summary using:

```text
Problem:
Constraints:
Options considered:
Decision:
Why:
Physical implementation:
Tests/evidence:
Reversal path:
```

The physical implementation entry must name the affected headers, source files,
Kconfig, generated artifacts, and tests. The reversal path should explain how a
different accepted choice could replace it without rediscovering the problem.

Examples of normally local decisions include:

- private helper and storage layout;
- focused internal naming;
- one Zephyr primitive versus another when public behavior is unchanged;
- tuple, array, index-sequence, or fold implementation details;
- test-fixture structure;
- bounded internal batching or lock partitioning within accepted ceilings;
- generated-file placement;
- diagnostic wording and stable diagnostic tokens.

### 3.3 Major decisions requiring a pause

Pause progression and request a decision when evidence would require changing:

- canonical subsystem ownership;
- a public API's semantics or normal user shape;
- component, catalog, contribution, binding, or lifecycle architecture;
- strict/relaxed binding guarantees;
- cross-subsystem dependency direction;
- implementation-stage prerequisites or a large part of the roadmap;
- static versus dynamic allocation policy;
- thread, executor, or hidden runtime ownership;
- stable wire identity, schema compatibility, or Remote protocol behavior;
- safety, recovery, watchdog, or forced-abort policy;
- the supported Zephyr/toolchain baseline;
- a resource assumption that materially affects later firmware architecture;
- an accepted specification in a way that invalidates substantial following
  implementation.

When pausing, state:

1. the concrete problem and evidence;
2. which accepted contracts and later stages are affected;
3. viable options;
4. costs, risks, and reversibility of each option;
5. the recommended resolution;
6. what work can and cannot continue before resolution.

Do not use a pause for routine implementation difficulty, ordinary bugs,
internal naming, or a decision already settled by the specifications.

### 3.4 Specification amendments

If implementation proves an accepted detail infeasible or materially harmful:

- [ ] Record concrete Zephyr, compiler, target, or implementation evidence.
- [ ] Update the owning design specification explicitly.
- [ ] Update affected planning artifacts and dependencies.
- [ ] Add verification covering the amended contract.
- [ ] Record the amendment in the landed summary.
- [ ] Rerun affected gates from any previously landed stage.

No architectural amendment may exist only in code.

## 4. Per-Stage Completion Gate

Every stage uses this gate in addition to its roadmap-specific requirements:

- [ ] Stage specifications and current implementation context were read.
- [ ] Superseded in-scope code was removed or deliberately reshaped.
- [ ] Required implementation behavior landed.
- [ ] New behavior has appropriate compile-pass, compile-fail, native runtime,
      concurrency, ISR, protocol, generation, firmware, or hardware tests.
- [ ] All stage-owned tests pass.
- [ ] Previously landed Solar tests still pass unless intentionally replaced.
- [ ] Strict and relaxed variants pass when the stage owns a bound API.
- [ ] Disabled Kconfig variants pass when the stage owns an optional capability.
- [ ] Applicable resource, stack, storage, binary, and timing evidence is
      recorded.
- [ ] No temporary compatibility architecture remains.
- [ ] Non-obvious local decisions and reversal paths are documented.
- [ ] The landed summary exists under
      `development-docs/implementation-planning/landed/`.
- [ ] This tracker and the roadmap tracker are updated.

Firmware builds are required only at roadmap-designated integration gates.

## 5. Stage Tracker

Detailed scope and verification are defined in
`development-docs/implementation-planning/04-implementation-roadmap.md`.

### Stage 00 - Repository Reset And Build Foundation

Status: **complete**

- [x] Replace the transitional repository with the minimal C++23 Zephyr module
      and test foundation defined by the roadmap.
- [x] Remove old architecture and unsupported test scaffolding in scope.
- [x] Pass the Stage 00 smoke, Kconfig, and test-harness green checkpoint.
- [x] Write `landed/00-repository-reset.md`.
- [x] Mark Stage 00 complete and continue to Stage 01.

### Stage 01 - Modern Core Result And Utilities

Status: **complete**

- [x] Implement the C++23 Result, Status, error, time, fixed-string, and generic
      metaprogramming foundation.
- [x] Pass the Stage 01 host and Zephyr core gates.
- [x] Write `landed/01-modern-core.md`.
- [x] Mark Stage 01 complete and continue to the next unblocked stage.

### Stage 02 - Identity, Contributions, And Catalogs

Status: **complete**

Opening contract:

- Prerequisite: Stage 01 landed with host, native Zephyr, and Teensy
  toolchain evidence.
- In scope: typed local/stable identity, descriptor customization, generic and
  conventional contribution sources, semantic owner/origin preservation,
  immutable catalogs and catalog sets, deterministic local IDs, focused
  validation, immutable descriptor views, and two fake subsystem extensions.
- Excluded: Blueprint normalization, application binding, global subsystem
  frontends, mutable subsystem storage, lifecycle, and subsystem-specific
  runtime behavior.
- Runtime ownership: immutable static descriptor arrays only when referenced;
  no heap, thread, lock, boot hook, or runtime registration.
- Gate: host compile-pass/fail tests, multi-translation-unit identity evidence,
  native Zephyr compile/runtime coverage, and all Stage 00-01 regressions.

- [x] Implement shared descriptors, identity, provenance, contributions, and
      catalog normalization.
- [x] Pass the Stage 02 compile-pass and compile-fail catalog gates.
- [x] Write `landed/02-identity-and-catalogs.md`.
- [x] Mark Stage 02 complete and continue.

### Stage 03 - Blueprint, System, Binding, And Frontends

Status: **complete**

Opening contract:

- Prerequisites: Stage 01 core and Stage 02 catalogs are landed and green.
- In scope: tagged order-independent sections, component categories,
  dependencies and DAG validation, effective Blueprint normalization, generic
  built-in inclusion infrastructure, static `System<Blueprint>`, one default
  and tagged alternate application binding, distributed type-owned state
  slots, and shared strict/relaxed typed frontend machinery.
- Excluded: lifecycle execution, boot reports, component hook invocation,
  kernel implementation, and concrete subsystem state/operations. Stage 03 may
  expose binding declarations needed by those later stages but must not invent
  placeholder runtime behavior.
- Runtime ownership: relaxed mode may own non-owning atomic frontend pointers
  and readiness state per application/operation type. Blueprint, graph,
  catalogs, strict bindings, and System remain compile-time types. No heap,
  thread, lock, constructor registration, or monolithic runtime object.
- Gate: host compile-pass/fail, strict and relaxed API equivalence,
  multi-translation-unit canonical state, LTO/non-LTO, native Zephyr variants,
  and Stage 00-02 regressions.

- [x] Implement Blueprint normalization, static System ownership, application
      binding, and strict/relaxed frontend infrastructure.
- [x] Pass the Stage 03 graph, binding, multi-translation-unit, and LTO gates.
- [x] Write `landed/03-blueprint-and-binding.md`.
- [x] Mark Stage 03 complete and continue.

### Stage 04 - Kernel Core Primitives

Status: **complete**

Opening contract:

- Prerequisites: Stage 01 core Result/time utilities and Stage 03 static System
  architecture are landed and green.
- In scope: Zephyr-native durations, time points, timeouts, deadlines,
  priorities, current-thread operations, distinct mutex/recursive mutex
  wrappers, lock guards and unique locks, semaphores, bounded typed message
  queues, event flags, polling, timers, interrupt context and scoped interrupt
  locks, scheduler operations, native handle escape hatches, and focused Result
  mapping for ordinary, non-blocking, timeout, and ISR forms.
- Excluded: owned Thread and stack storage, stop tokens, work and workqueues,
  broad thread diagnostics, lifecycle, execution registration, and any System
  integration. Those belong to Stages 05-07.
- Runtime ownership: wrappers own only their exact Zephyr primitive and bounded
  inline buffers. No heap, hidden thread, static registry, System slot, or
  constructor registration is permitted.
- Gate: native Zephyr success/timeout/full/empty and poll/timer behavior,
  applicable ISR-context tests, compile-fail payload and invalid-operation
  contracts, public-header checks, storage evidence, and Stage 00-03
  regressions.

- [x] Reshape the accepted Zephyr kernel wrappers for synchronization, timing,
      queues, poll, timers, interrupts, and scheduling.
- [x] Pass the Stage 04 native and ISR kernel gates.
- [x] Write `landed/04-kernel-primitives.md`.
- [x] Mark Stage 04 complete and continue.

### Stage 05 - Kernel Threads, Work, And Diagnostics

Status: **complete**

Opening contract:

- Prerequisites: Stage 04 core kernel primitives are landed and the complete
  Stage 00-04 host/native regression matrix is green.
- In scope: statically owned threads and stack storage, explicit preparation
  and release, suspend/resume/join/abort boundaries, cooperative stop source
  and token primitives, ordinary and delayable work, the system workqueue
  adapter, statically owned workqueues, accepted missing bounded kernel
  facilities such as memory slabs, focused direct thread records and iteration,
  stack/runtime-stat availability, and the fatal/error hook boundary required
  by later lifecycle and panic logging.
- Excluded: component lifecycle orchestration, service semantics, executor and
  task registration, health interpretation, recovery/watchdog policy, and
  hidden default workers. Those belong to Stages 06, 07, 12, and 18-19.
- Runtime ownership: each owning wrapper contains its exact native object,
  compile-time bounded stack/buffer storage, and only explicit callback or stop
  state. The system workqueue adapter is non-owning. No heap, System slot,
  registry, constructor registration, or Solar-created default thread is
  permitted.
- Gate: native thread prepare/release/lifecycle and static-stack tests; work
  submit/resubmit/delay/cancel/drain and owned/system queue tests; cooperative
  stop tests; finite timeout and abort behavior; diagnostic enabled/disabled
  Kconfig matrix; fatal-boundary compile/link fixtures; standalone headers,
  resource evidence, and Stage 00-04 regressions.

- [x] Implement threads, stop primitives, work, workqueues, missing kernel
      facilities, focused diagnostics, and fatal boundaries.
- [x] Pass the Stage 05 thread/work/diagnostic Kconfig matrix.
- [x] Write `landed/05-kernel-execution-foundation.md`.
- [x] Mark Stage 05 complete and continue.

### Stage 06 - Lifecycle, Graph, Boot, And Stop

Status: **complete**

- [x] Implement lifecycle orchestration, reports, rollback, stop, graph queries,
      global boot/stop, and the activation protocol.
- [x] Hard-migrate firmware through the foundation integration gate.
- [x] Pass native and Teensy Stage 06 firmware builds.
- [x] Write `landed/06-lifecycle-and-system.md`.
- [x] Mark Stage 06 complete and continue.

### Stage 07 - Services, Executors, And Tasks

Status: **complete**

Opening contract:

- Prerequisites: Stage 05's owned thread/work foundation and Stage 06's
  lifecycle activation/containment protocol are landed and green.
- In scope: static service run validation and ownership, prepared service
  threads and activation release, cooperative stop/join/abort policy,
  explicit custom executors, Zephyr system-workqueue and owned-workqueue
  integration, typed task/job registrations and triggers, admission closure,
  cancellation, and focused execution records.
- Excluded: a hidden Solar default executor, Bus/Parameter/Event scheduling,
  subsystem-specific workers, supervisor recovery, and firmware migration.
  Those remain with their owning later stages.
- Runtime ownership: each service or owned executor contains its exact thread,
  stack, stop, queue, and record state through a System-owned typed slot.
  Zephyr's system workqueue adapter is non-owning. No heap, hidden worker, or
  constructor registry is permitted.
- Gate: service prepare/release/return/stop/timeout/abort tests; executor
  admission/drain/cancel/containment tests; task trigger and cancellation
  tests; system and owned workqueue variants; compile-fail service/registration
  contracts; standalone headers, resource evidence, and Stage 00-06
  regressions.

- [x] Implement service execution, explicit executors, task registrations,
      Zephyr workqueue integration, cancellation, and focused records.
- [x] Pass the Stage 07 execution and activation-barrier gates.
- [x] Write `landed/07-execution.md`.
- [x] Mark Stage 07 complete and continue.

### Stage 08 - Typed Bus

Status: **complete**

Opening contract:

- Prerequisites: Stage 02 catalog/contribution identity, Stage 03 System and
  frontend binding, Stage 04 bounded Kernel synchronization, Stage 06
  lifecycle ordering, and Stage 07 Execution targets are landed and green.
- In scope: message and subscription catalogs; component-local and central
  routes; inline, queued, latest, and coalesced delivery; bounded overflow and
  stop policy; ordinary, no-wait, and ISR emission; exact route-owned storage;
  demand-derived Bus facility inclusion; generated subscriber/executor
  dependencies; focused descriptors, route records, and errors; strict and
  relaxed frontends; Kconfig capabilities, defaults, and ceilings.
- Excluded: point-to-point mailbox ownership, service mailboxes, retained
  message history, Remote exposure, observability side effects, runtime route
  registration, dynamic allocation, and a Bus-owned worker thread.
- Runtime ownership: one demand-derived typed Bus facility owns route-local
  admission, payload storage, synchronization, records, and generated
  Execution registrations. Named executors own their queues; Zephyr owns the
  system workqueue. Inline routes own no payload storage.
- Gate: compact contribution and root normalization; strict/relaxed and
  disabled variants; compile-fail topology/payload/handler/capacity contracts;
  deterministic fan-out and reentrancy; concurrent and ISR producers; queued,
  latest, coalesced, overflow, drain, and cancel behavior; isolated headers;
  exact storage and no-heap evidence; all Stage 00-07 regressions.
- Primary risks: owner-binding component-local routes without circular
  includes; composing application frontend protocols across subsystems;
  preserving route/subscriber/executor dependencies during containment; and
  avoiding lost wakeups when route work coalesces through Zephyr workqueues.

- [x] Implement message catalogs, subscriptions, route policies, delivery,
      overflow, ISR emission, execution integration, and records.
- [x] Remove all remaining Channel architecture.
- [x] Pass the Stage 08 Bus gates in strict and relaxed modes.
- [x] Write `landed/08-bus.md`.
- [x] Mark Stage 08 complete and continue.

### Stage 09 - Parameters

Status: **complete**

Opening contract:

- Prerequisites: Stage 02 catalog identity, Stage 03 System/frontends, Stage 04
  synchronization, Stage 06 lifecycle, and Stage 07 explicit execution targets
  are landed and green. Bus is available for explicit application adapters but
  Parameters does not depend on it.
- In scope: parameter and change-hook catalogs; compact component and root
  declarations; typed defaults, validation, access, storage, and persistence
  policy; demand-derived facility inclusion and implicit application
  dependency; exact typed slots; mutex, immutable, and lock-free atomic
  storage; get/set/try/reset, coherent snapshots, all-or-none RAM transactions,
  synchronous post-commit hooks and startup coalescing; volatile, immediate,
  deferred, manual, and transactional persistence boundaries; fake and Zephyr
  settings adapters; focused records/errors; strict/relaxed frontends; Kconfig
  capability, defaults, metadata controls, and hard ceilings.
- Excluded: arbitrary component state, automatic Bus/Event/Metric/Log side
  effects, automatic Remote exposure, dynamic registration, general ISR
  mutation, unbounded values, hidden transaction state, and a
  Parameters-owned thread or workqueue.
- Runtime ownership: one demand-derived typed Parameters facility owns one
  exact slot per effective parameter, one facility write gate, change leaf
  state, persistence facts, and at most one generated deferred-persistence
  registration on an explicit Stage 07 target. Stores own their backend state;
  executors own execution resources.
- Gate: contribution/owner normalization; validation/access/storage compile
  contracts; strict/relaxed/disabled variants; mutex and lock-free atomic
  concurrency; coherent snapshots and transactions; startup and runtime hook
  behavior; fake persistence load/save/failure/migration/reset/deferred tests;
  isolated headers, bounded storage/no-heap evidence, and all Stage 00-08
  regressions.
- Primary risks: generated implicit dependency edges without changing authored
  component types; preserving the strong no-commit-on-error invariant across
  validation and immediate persistence; activating startup hook leaves before
  `Running`; and keeping deferred persistence to one explicit execution
  registration without introducing a scheduler or hidden worker.

- [x] Implement typed parameter storage, validation, concurrency, hooks,
      persistence boundaries, records, and bound frontends.
- [x] Pass the Stage 09 parameter and Kconfig gates.
- [x] Write `landed/09-parameters.md`.
- [x] Mark Stage 09 complete and continue.

### Stage 10 - Observability Events

Status: **complete**

- [x] Implement structured Events, bounded ingress/history, ISR observation,
      processors, loss accounting, records, and adapters.
- [x] Pass the Stage 10 Event concurrency and capacity gates.
- [x] Write `landed/10-events.md`.
- [x] Mark Stage 10 complete and continue.

### Stage 11 - Metrics

Status: **complete**

- [x] Implement metric instruments, catalog-derived storage, concurrency,
      reducers/views, ISR operations, records, and Event adapters.
- [x] Measure relaxed and strict hot-path behavior.
- [x] Pass the Stage 11 metric gates.
- [x] Write `landed/11-metrics.md`.
- [x] Mark Stage 11 complete and continue.

### Stage 12 - Logging

Status: **complete**

- [x] Implement early structured Logging, canonical bounded ingress, filtering,
      Zephyr frontend, sinks, history, flush, shutdown, and panic behavior.
- [x] Complete the core-subsystem firmware integration gate.
- [x] Pass native and Teensy Stage 12 firmware builds.
- [x] Write `landed/12-logging.md`.
- [x] Mark Stage 12 complete and continue.

### Stage 13 - Remote Protocol And Generation

Status: **complete**

Opening contract:

- Objective: freeze Remote protocol v1 and produce deterministic firmware and
  host schema artifacts before any transport or session runtime exists.
- Specification scope: `10-remote.md` Sections 5, 9-13, 20, 26, 28, 33, 35,
  36, and the Stage 13 portions of Sections 38-40.
- Excluded: physical links, service execution, sessions, authorization,
  acquisition, dispatch, queues, backpressure, lifecycle, and subsystem
  exposure runtime; those belong to Stage 14.
- Prerequisites: Stages 00-12 are landed, including stable catalog identity,
  System binding, Execution, and observability foundations.
- Reshape: retain the contribution aliases; replace the removed prototype
  codec/protocol/schema and YAML registry with typed declarations, portable
  wire primitives, deterministic generation, and shared vectors.
- Public surface: `solar/remote.hpp` and focused declaration, schema, codec,
  packed, frame, protocol, manifest, and generation headers. Remote remains
  Kconfig-optional and has no fallback configuration header.
- Ownership: this stage adds immutable metadata and caller-owned codec/frame
  buffers only. It owns no heap, thread, stack, queue, timer, work item,
  transport, or canonical application value.
- Gates: enabled/disabled header builds, declaration and schema diagnostics,
  host/Zephyr shared vectors, malformed input corpus, deterministic repeated
  generation, generated C++ compilation and Python import, and the complete
  landed regression matrix.
- Risks: deterministic CBOR agreement across zcbor and host tooling, stable
  envelope offsets and numeric domains, pointer-free emitted metadata, and
  preventing Stage 14 runtime concerns from leaking into the protocol core.

- [x] Implement stable Remote schemas, deterministic CBOR, packed Streams,
      framing, generation, and shared C++/host protocol vectors.
- [x] Pass deterministic generation and protocol golden-vector gates.
- [x] Write `landed/13-remote-protocol.md`.
- [x] Mark Stage 13 complete and continue.

### Stage 14 - Remote Runtime And Integration

Status: **complete**

Opening contract:

- Objective: implement Remote's bounded asynchronous facility, service, links,
  sessions, endpoint runtime, and explicit subsystem exposures on the frozen
  Stage 13 wire.
- Specification scope: `10-remote.md` Sections 4, 6-8, 14-25, 27, 29-34,
  37-42, plus bounded collection/nested-schema completion from Section 12.
- Prerequisites: Stages 07-13 are landed; Execution supplies visible targets,
  observability subsystems retain canonical state, and protocol artifacts are
  frozen.
- Public surface: focused runtime, link, session, acquisition, action,
  publication, subscription, records, configuration, and adapter headers under
  `solar/remote/`, exposed through `solar/remote.hpp`.
- Ownership: one demand-derived facility owns static dispatch and endpoint
  frontends; one demand-derived service owns bounded protocol/session storage
  and either a visible service thread or selected Execution registration. Links
  own transport state and asynchronous leases. No application value becomes
  Remote-owned canonical state.
- Gates: in-memory and fake-DMA links, arbitrary RX fragmentation and short TX,
  handshake/auth/session reset, Actions and all Data capabilities, producer and
  slow-client isolation, bounded queues/credits/fragments, explicit adapters,
  host interoperability, strict/relaxed/disabled builds, and native/Teensy
  integration.
- Risks: adding generated service-category components without weakening graph
  ownership; lock ordering across source acquisition and session queues;
  response reservation and duplicate suppression; bounded fragmentation; and
  preserving protocol responsiveness under telemetry pressure.

Progress:

- [x] Derive a Remote facility and visible Remote service only when the bound
      link catalog is non-empty.
- [x] Land the asynchronous link/lease/event contract and deterministic
      in-memory link.
- [x] Land bounded service wakeup, fragmented RX framing, session epoch and
      ClientHello/ServerHello negotiation, focused initial records, and clean
      lifecycle containment.
- [x] Land the first source-owned Push/Latest acquisition path from
      `remote::write<Data>()` to an encoded host Data frame in relaxed and
      strict builds.
- [x] Land explicit Inline Action authorization, typed request/response CBOR,
      monotonic request admission, bounded response caching, duplicate replay,
      response acknowledgement, and expired-duplicate rejection.
- [x] Land ordinary Action dispatch through one generated, explicitly targeted
      system-workqueue registration while preserving explicit Inline execution.
- [x] Land session-local Push subscriptions, advisory `remote::interested`,
      five protected output lanes, telemetry replacement, and service-owned
      link transmission.
- [x] Land typed Query and Update capability dispatch through the shared
      request executor with explicit Data operation identity and grants.
- [x] Land exact response reservations/caches, pending-work cancellation, and
      session-generation containment for late request completion.
- [x] Land a fake-DMA asynchronous link fixture proving retained Tx lease
      lifetime, short-transfer progression, exact completion identity, and Rx
      lease release.
- [x] Land opt-in constrained ISR Push publication for trivially copyable
      Latest values, including context rejection and ISR-safe service wakeup.
- [x] Land compact subscription negotiation, endpoint/Kconfig rate clamping,
      per-session Push downsampling and delivery accounting, plus an idle-pump
      regression proving normal maintenance timeouts do not stop the service.
- [x] Land subscription-activated Poll acquisition through generated visible
      Execution registrations, authored workqueue targeting, highest-rate
      release scheduling, per-session downsampling, overlap skipping, and final
      unsubscribe containment.
- [x] Land bounded per-lane output rings, policy-sized Push queues with explicit
      overflow behavior, host-decodable batches, and bounded generation-checked
      Loaned staging with prompt release.
- [x] Land Data Watch and standalone Topic publication with independent typed
      subscription domains, bounded ingress, host-visible subscription-kind
      identity, and strict/relaxed coverage.
- [x] Pass the outbound-runtime checkpoint: all 15 Remote Zephyr configurations,
      17 runtime cases, and 57 host/compile-fail tests pass without warnings.
- [x] Land independently bounded multi-session state and prove link-local
      subscriptions, routing, disconnect containment, and surviving global
      endpoint interest.
- [x] Land owned inbound Stream windows, explicit generated execution targets,
      per-link credits, ordered sequence admission, consumer-completion credit
      return, session containment, and credit-violation rejection.
- [x] Land bounded inbound reassembly and outbound logical-message
      fragmentation with ordered fragments, timeout reclamation, lane
      interleaving, and exact host reconstruction.
- [x] Pass the full endpoint-runtime checkpoint: all 21 Remote Zephyr
      configurations, 23 runtime cases, and 57 host/compile-fail tests pass
      without warnings.
- [x] Complete explicit subsystem exposure adapters, generated host client
      integration, focused runtime accounting, and firmware integration.
- [x] Land move-only asynchronous Action responders with exactly-once
      completion, cancellation observation, abandonment, and session
      containment.
- [x] Pass generated-client interoperability against the running native
      firmware over a dedicated Zephyr PTY UART.

- [x] Implement the Remote facility/service, async links, sessions, Actions,
      Data, Topics, Streams, backpressure, and subsystem adapters.
- [x] Complete the Remote firmware and host interoperability gate.
- [x] Pass native and Teensy Stage 14 firmware builds.
- [x] Write `landed/14-remote-runtime.md`.
- [x] Mark Stage 14 complete and continue.

### Stage 15 - Inspection

Status: **complete**

- [x] Implement focused collection descriptors, adapters, bounded paging, and
      shared local/Remote query surfaces without duplicate truth.
- [x] Pass the Stage 15 ownership, availability, and paging gates.
- [x] Write `landed/15-inspection.md`.
- [x] Mark Stage 15 complete and continue.

### Stage 16 - Hardware Generator And Foundations

Status: complete

- [x] Implement Zephyr devicetree generation, endpoint concepts, readiness,
      GPIO, interrupts, and foundational async adapters.
- [x] Pass fixture generation, native, and Teensy compile gates.
- [x] Write `landed/16-hardware-foundations.md`.
- [x] Mark Stage 16 complete and continue.

### Stage 17 - Hardware Driver Families And Devices

Status: complete

- [x] Implement SPI, I2C, UART, ADC, PWM, Counter, Watchdog, and accepted native
      escape hatches over Zephyr drivers.
- [x] Migrate firmware hardware aliases and application Device types.
- [x] Pass native, Teensy, and applicable physical hardware gates.
- [x] Write `landed/17-hardware-drivers.md`.
- [x] Mark Stage 17 complete and continue.

### Stage 18 - Health

Status: **complete**

- [x] Implement Health subjects, observations, component checks, monitor
      adapters, progress, evidence, freshness, and records.
- [x] Pass the Stage 18 assessment, stale, ISR, and integration gates.
- [x] Write `landed/18-health.md`.
- [x] Mark Stage 18 complete and continue.

### Stage 19 - Supervisor

Status: **complete**

- [x] Implement Supervisor monitoring, response policy, escalation, recovery,
      self-health, and watchdog provider boundaries.
- [x] Pass deterministic policy, stall, recovery, and watchdog gates.
- [x] Write `landed/19-supervisor.md`.
- [x] Mark Stage 19 complete and continue.

### Stage 20 - Full Integration And Closure

Status: **complete**

notes: This is an intentionally small stage, this is a quick check and run over what has been implemented. The goal is to ensure that the entire system is integrated and all components work together as expected. Avoid long builds if possible, and prior test are already green.

- [x] Complete full firmware and host-tool migration.
- [x] Remove every remaining old architecture artifact.
- [x] Pass the complete Solar, strict/relaxed, native, Teensy, Remote, resource,
      and applicable physical hardware gates.
- [x] Complete the final ownership, Kconfig, resource, include, and architecture
      audits.
- [x] Write `landed/20-integration-closure.md`.
- [x] Mark implementation complete and exit before hand off to public documentation.

## 6. Global Implementation Closure

- [x] Every Stage 00-20 checkpoint is complete.
- [x] Every stage has a landed summary.
- [x] All accepted specification clauses map to implementation or an explicit
      accepted later extension.
- [x] Solar's complete native test suite is green through the accepted
      previously-green plus focused-stage evidence.
- [x] Strict and relaxed complete-system builds are green.
- [x] Firmware builds for native simulation and Teensy 4.0.
- [x] Physical smoke execution is recorded as unavailable in this workspace;
      native driver behavior and Teensy compile/link gates pass.
- [x] Generated Remote host tooling interoperates with final firmware; the
      obsolete SDK/CLI is removed and complete convenience tooling remains an
      accepted Spec 10 extension.
- [x] No old positional System, entry Profile, Channel, context object,
      compatibility alias, or duplicate runtime remains.
- [x] Runtime ownership, static storage, threads, stacks, timers, workqueues,
      Kconfig, and binary costs are measured and recorded.
- [x] Public-documentation inputs are indexed and complete.

## 7. Public Documentation Pass

Do not build public documentation piecemeal during the reform. After
implementation closure, build it from:

- accepted design specifications;
- stage landed summaries and decision records;
- final public headers and generated references;
- executable examples and tests;
- final target and resource measurements.

- [ ] Replace the old Solar README and public docs.
- [ ] Publish architecture and composition guidance.
- [ ] Publish subsystem API and Kconfig references.
- [ ] Publish Zephyr, devicetree, hardware, Remote, and host-tool guidance.
- [ ] Publish tested representative firmware examples.
- [ ] Verify all public examples against the final implementation.
