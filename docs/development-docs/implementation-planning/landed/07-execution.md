# Stage 07: Services, Executors, And Tasks

Status: landed

Landed date: 2026-07-16

Implementation repository/branch: `/workspaces/solar`, `static_reform`

Relevant commits or change identifiers: uncommitted reform working tree

## 1. Objective

Stage 07 lands Solar's system-integrated execution plane on the Kernel work and
thread primitives from Stage 05 and the lifecycle activation protocol from
Stage 06.

The landed implementation provides:

- statically declared services with one explicitly owned thread and stack;
- explicit application workqueue executor components;
- Zephyr's system workqueue as a first-class non-owning target;
- typed on-demand, delayable, periodic, and poll-triggered registrations;
- native coalescing and bounded counted admission;
- ordinary, synchronous, and ISR-safe submission paths;
- bounded activation, admission closure, cancellation, draining, stopping,
  timeout, and owned forced-containment behavior;
- focused service, executor, registration, and system-target records;
- strict and relaxed global execution frontends over one canonical state;
- no hidden executor, hidden application queue, heap allocation, or runtime
  registration.

The representative green checkpoint runs one cooperative service, one explicit
owned queue, component-contributed jobs, root jobs, periodic and poll-triggered
work, and shared system-workqueue work under one lifecycle.

## 2. Specification Coverage

| Specification | Sections implemented | Notes |
| --- | --- | --- |
| `09-tasks-and-executors.md` | 1-40, initial capability in 40 | Landed across Stages 05 and 07. Stage 05 owns faithful Kernel mechanisms; Stage 07 owns static registration, service, executor, lifecycle, API, records, and policy. |
| `03-lifecycle-kernel-and-configuration.md` | lifecycle activation and execution containment boundary | The generic lifecycle execution protocol now has concrete service, executor, and registration implementations. |
| `01-system-blueprint-and-binding.md` | effective catalogs, typed state ownership, strict/relaxed frontends | Execution catalogs and state derive from the bound `System`; named System boot establishes the same frontend binding used by global calls. |
| `02-identity-contributions-and-catalogs.md` | execution descriptors, root and component contribution aliases, owner/origin retention | `Execution<...>` and conventional `using Tasks` sources normalize into one immutable catalog. |

The accepted deferred capabilities in Section 41 remain deferred. They include
executor pools, work stealing, dynamic registration, arbitrary callable or
payload-bearing jobs, futures/coroutines, per-occurrence cancellation handles,
periodic catch-up queues, overlapping invocation, runtime queue migration,
service mailboxes, reboot reconstruction, supervisory restart, and hidden
application-defined default targets.

Subsystem-owned registrations are supported by the generic contribution and
target protocol. Concrete Bus, Parameter, Event, Logging, Metrics, and Remote
registrations land with their owning stages.

## 3. Public Surface Landed

### 3.1 Headers and namespaces

The public aggregate is:

```cpp
#include <solar/execution.hpp>
```

It exposes focused headers under `include/solar/execution/` for types, policy,
registrations, contribution aliases, workqueue executors, services, protocol,
and API operations. `solar/solar.hpp` includes Execution when
`CONFIG_SOLAR_EXECUTION` is enabled.

### 3.2 Normal application shape

```cpp
using namespace solar::literals;

struct RecalculateControl
{
    static solar::Result<void> execute();
};

using ControlQueue = solar::execution::WorkQueue<
    "control-work",
    solar::execution::StackSize<2048>,
    solar::execution::Priority<2>,
    solar::execution::StopTimeout<200_ms>>;

using Recalculate = solar::execution::OnDemand<
    "recalculate-control",
    RecalculateControl,
    ControlQueue>;

struct Control
{
    static constexpr solar::component::Descriptor descriptor{.name = "control"};
    using Tasks = solar::execution::Tasks<Recalculate>;
};

using Robot = solar::System<solar::Blueprint<
    solar::Facilities<Control>,
    solar::Executors<ControlQueue>>>;

SOLAR_BIND_SYSTEM(Robot);

auto booted = Robot::boot();
auto submitted = solar::execution::submit<Recalculate>();
```

The registration is a leaf. `ControlQueue` is the visible lifecycle component
that owns execution resources.

### 3.3 Service shape

```cpp
struct RemoteService
{
    static constexpr solar::component::Descriptor descriptor{.name = "remote"};

    using Execution = solar::execution::Service<
        solar::execution::StackSize<3072>,
        solar::execution::Priority<3>,
        solar::execution::StopTimeout<250_ms>>;

    static solar::Status run(solar::StopToken stop);
};
```

Accepted service returns are `void`, `Status`, and `Result<void>`. The normal
cooperative form accepts `StopToken`; all service execution is prepared before
the final lifecycle commit and released only after the system reaches
`Running`.

### 3.4 Registration families

```cpp
using Immediate = solar::execution::OnDemand<"immediate", Behavior, Target>;
using Buffered = solar::execution::OnDemand<
    "buffered", Behavior, Target, solar::execution::Counted<8>>;
using Deferred = solar::execution::Delayable<"deferred", Behavior, Target>;
using Tick = solar::execution::Periodic<
    "tick", Behavior, 10_ms, Target, solar::execution::StartImmediately>;
using Triggered = solar::execution::PollTriggered<
    "triggered", Behavior, PollSet, Target, solar::execution::poll::AutoRearm>;
```

Behavior may expose `execute()` or `execute(StopToken)` and return `void`,
`Status`, or `Result<void>`.

### 3.5 Operations and focused queries

The public global surface includes:

- `submit<Registration>()`;
- `try_submit_isr<Registration>()`;
- `schedule<Registration>(delay)` and `reschedule<Registration>(delay)`;
- `cancel<Registration>()`, `cancel_sync<Registration>()`, and
  `flush<Registration>()`;
- `registration<Registration>()` and bounded `registration_page(...)`;
- `registrations()`, `services()`, `executors()`, and `system_target()`;
- `service<Service>()` and `executor<Executor>()`;
- corresponding `execution::Of<Application>` operations for explicit
  application binding.

### 3.6 Kconfig

Stage 07 adds:

- `CONFIG_SOLAR_EXECUTION`;
- `CONFIG_SOLAR_EXECUTION_DEFAULT_SYSTEM_WORKQUEUE`;
- `CONFIG_SOLAR_EXECUTION_MAX_REGISTRATIONS`;
- `CONFIG_SOLAR_EXECUTION_QUIESCENCE_TIMEOUT_MS`;
- `CONFIG_SOLAR_EXECUTOR_STOP_TIMEOUT_MS`;
- `CONFIG_SOLAR_EXECUTOR_ABORT_ON_STOP_TIMEOUT`;
- `CONFIG_SOLAR_SERVICE_STACK_SIZE`;
- `CONFIG_SOLAR_SERVICE_STOP_TIMEOUT_MS`;
- `CONFIG_SOLAR_SERVICE_ABORT_ON_STOP_TIMEOUT`.

Kconfig provides project defaults and hard ceilings. Explicit C++ policy on a
service, registration, or executor overrides the applicable Kconfig default.

## 4. Runtime Ownership

| Owner | Storage/resource | Capacity | Synchronization | Lifetime |
| --- | --- | --- | --- | --- |
| service component | one `kernel::Thread<StackSize>`, stack, stop source, activation semaphore, and record | exactly one per effective service | atomics plus spinlock-protected record; Zephyr join/abort | one static typed System slot |
| application executor component | one `kernel::WorkQueue<StackSize>`, stack, native queue/thread, and executor record | exactly one per effective executor | Zephyr queue synchronization plus spinlock-protected record | one static typed System slot |
| system-workqueue target | non-owning adapter and aggregate record only | one logical target per System | query-time aggregation from registration records | Zephyr-owned queue lifetime |
| execution registration | one exact Kernel work primitive, focused record, admission and lifecycle atomics | one per effective registration | native work synchronization, atomics, and one record spinlock | one static typed System slot |
| execution runtime | last execution failure and uncontained system-registration count | one per System | spinlock and atomic counter | static System lifetime |
| relaxed frontend | non-owning operation binding | one per application/operation specialization | atomic frontend binding established before activation | program lifetime |

No Stage 07 path allocates from the heap. No runtime registry is populated by
constructors. All canonical mutable state is owned by typed `System::StateSlot`
specializations.

The system workqueue remains Zephyr-owned. Solar never drains, plugs, stops, or
aborts it globally. Solar may synchronously cancel its own work items and report
an uncontained system-workqueue registration if bounded quiescence fails.

### 4.1 Conditional registration storage

Optional per-registration resources are selected at compile time:

- ordinary behavior has no stop source;
- only behavior accepting `StopToken` owns `kernel::StopSource`;
- only `Counted<N>` owns the pending atomic;
- only `Periodic` owns ideal-release timing state;
- each registration owns only the native work form its kind requires.

Representative emitted state sizes on `native_sim/native/64` were:

| Registration shape | State size |
| --- | ---: |
| ordinary on-demand | 328 B |
| counted on-demand | 336 B |
| token-aware on-demand | 384 B |
| periodic | 376 B |

Delayable and poll-triggered registrations are larger because their native
Zephyr work objects contain timeout or poll state.

## 5. Compile-Time Behavior

The effective execution catalog combines:

- root `solar::Execution<...>` declarations;
- component-local conventional `using Tasks = execution::Tasks<...>` aliases;
- future subsystem contribution sources through the same typed catalog path.

Catalog entries retain registration type, local identity, semantic owner, and
origin. Registration dependencies combine explicit `DependsOn<...>`, owner
dependencies, and the explicit executor target. Lifecycle containment uses
those dependencies to preserve resources reachable by uncontained work.

Target resolution is compile time:

- explicit `SystemWorkQueue` resolves directly;
- explicit application queues must appear in `Executors<...>` and satisfy the
  workqueue executor concept;
- omitted targets resolve to the system workqueue only when the Kconfig default
  is enabled;
- otherwise omission is a focused compile error.

The hard execution-registration ceiling is checked against the normalized
effective catalog, not authored root entries alone.

Strict and relaxed builds expose the same successful operation spelling and
same canonical state. Named `System::boot()` establishes the application
frontend binding before lifecycle preparation, so `Robot::boot()` followed by
`solar::execution::submit<...>()` is the normal path in both modes.

Stable Stage 07 compile-fail tokens include:

- `SOLAR_DIAGNOSTIC_EXECUTION_TARGET_REQUIRED`;
- `SOLAR_DIAGNOSTIC_INVALID_EXECUTION_BEHAVIOR`;
- `SOLAR_DIAGNOSTIC_MISSING_SERVICE_EXECUTION`;
- `SOLAR_DIAGNOSTIC_INVALID_SERVICE_RUN`;
- `SOLAR_DIAGNOSTIC_UNREGISTERED_EXECUTION_TARGET`;
- `SOLAR_DIAGNOSTIC_INVALID_EXECUTOR_COMPONENT`;
- `SOLAR_DIAGNOSTIC_EXECUTION_DEPENDENCY_ABSENT`;
- `SOLAR_DIAGNOSTIC_EXECUTION_REGISTRATION_CEILING`;
- `SOLAR_DIAGNOSTIC_DUPLICATE_EXECUTION_POLICY_AXIS`;
- `SOLAR_DIAGNOSTIC_EXECUTION_ZERO_COUNTED_CAPACITY`.

## 6. Error And Availability Behavior

Runtime operations return focused `Result<T, execution::Error>` values.
`execution::Error` identifies operation, registration, target kind, current
availability, Solar status, reason, and native error where relevant.

The implemented reasons distinguish:

- inactive and suspended registration;
- invalid caller context;
- stopped or unavailable queue;
- cancellation in progress;
- counted admission full;
- native submission, schedule, trigger, or cancellation failure;
- lifecycle containment timeout.

Native coalescing is success with an explicit `SubmissionDisposition`; it is
not silently collapsed into generic success or treated as failure.

Behavior failures update the registration record. `failure::Suspend` closes
future admission and changes availability to `Suspended`; the default records
the failure and permits later releases. Unexpected service return after
activation changes the owning system to `Failed` and records
`Status::UnexpectedExit`.

Service and executor timeout policy may forcibly abort only threads Solar owns.
The stop report distinguishes clean, forced, timed-out, failed-abort, and
uncontained outcomes.

## 7. Zephyr Integration

Stage 07 builds directly on:

- `k_thread`, statically owned stack memory, join, and abort through
  `kernel::Thread`;
- `k_work`, `k_work_delayable`, and `k_work_poll` through exact Kernel wrappers;
- `k_work_q`, queue drain/plug/stop, and queue thread identity through
  `kernel::WorkQueue`;
- Zephyr's existing system workqueue through a non-owning adapter;
- `k_poll_event` storage owned by the user's fixed poll-set provider;
- `irq_offload` in native tests to exercise the real ISR submission path;
- monotonic Zephyr ticks for periodic release, duration, deadline, and record
  timing.

`try_submit_isr` is the explicit non-waiting ISR spelling. Waiting,
synchronous cancellation, scheduling, rescheduling, flush, boot, stop, and
record-locking operations remain thread-context operations.

Native handles remain available through the Stage 05 Kernel wrappers. The
Execution API intentionally exposes typed architecture rather than raw queue or
work pointers.

## 8. Files Changed

### Added

- `include/solar/execution.hpp`
- `include/solar/execution/api.hpp`
- `include/solar/execution/contribution.hpp`
- `include/solar/execution/policy.hpp`
- `include/solar/execution/protocol.hpp`
- `include/solar/execution/registration.hpp`
- `include/solar/execution/runtime.hpp`
- `include/solar/execution/service.hpp`
- `include/solar/execution/service_runtime.hpp`
- `include/solar/execution/types.hpp`
- `include/solar/execution/work_queue.hpp`
- `tests/zephyr/execution/`
- `tests/zephyr/execution_default_target/`
- `tests/zephyr/execution_compile_fail/`
- `tests/zephyr/check_execution_headers.py`

### Reshaped

- `include/solar/core/time.hpp`
- `include/solar/kernel/work_queue.hpp`
- `include/solar/lifecycle/engine.hpp`
- `include/solar/lifecycle/protocol.hpp`
- `include/solar/system/blueprint.hpp`
- `include/solar/system/frontend.hpp`
- `include/solar/system/system.hpp`
- `include/solar/solar.hpp`
- `zephyr/Kconfig`
- Stage 03 and Stage 06 fixtures that model executor or application protocol
  integration

### Removed

The old Channel-era `task.hpp`, positional task storage, hidden dedicated-task
thread model, and legacy service runtime had already been removed by the hard
reset. No compatibility alias or parallel execution architecture was restored.

## 9. Tests And Evidence

| Command | Platform/configuration | Result | What it proves |
| --- | --- | --- | --- |
| `cmake --build build/host -j2 && ctest --test-dir build/host --output-on-failure` | host C++23 | 47/47 pass | all modern core, catalog, System, binding, multi-TU, LTO, and compile-fail regressions remain green |
| `west twister -T tests/zephyr -p native_sim/native/64 --inline-logs --outdir build/twister-stage07-final --clobber-output --warnings-as-errors -j 1` | Zephyr 4.4, all Stage 00-07 configurations | 19/19 configurations and 88/88 cases pass, no warnings | full native regression matrix on the exact landed tree |
| `./build/stage07-execution/zephyr/zephyr.exe` | representative relaxed execution image | 11/11 pass | activation barrier, service records, system/owned queues, ISR submission, counted admission, delayable/periodic/poll work, cancellation, failure suspension, focused queries, token-aware behavior, forced service containment, and unexpected exit |
| execution Twister strict, relaxed, and no-default-abort variants | `native_sim/native/64` | 3 variants pass | one API/state architecture across binding modes and both forced-abort defaults |
| default-target Twister fixture | Kconfig system-workqueue default enabled | pass | an omitted target resolves without creating a Solar executor |
| ten `execution_compile_fail` Zephyr builds with token matching | Kconfig ceiling 2 and default target disabled | all ten fail for exact intended tokens | architecture and policy rejection contracts |
| `python3 tests/zephyr/check_execution_headers.py --compile-commands build/stage07-execution/compile_commands.json --include-root include` | real Zephyr C++23 command plus `-Werror` | 11/11 headers pass in isolation | public-header self-sufficiency |
| `clang-format --dry-run --Werror ...` and `git diff --check` | touched execution sources | pass | formatting and whitespace closure |
| `size build/stage07-execution/zephyr/zephyr.elf` | representative full execution image | text 189,280 B; data 9,949 B; bss 26,590 B; total 225,819 B | bounded image baseline including nine registrations, service, executor, records, and tests |
| `size build/stage07-default-target/zephyr/zephyr.elf` | minimal Kconfig-default target image | text 69,275 B; data 1,029 B; bss 9,107 B; total 79,411 B | no hidden executor/stack is introduced by default-target resolution |
| undefined-symbol audit with `nm -u -C` | representative execution ELF | no `malloc`, `calloc`, `realloc`, or `operator new` symbols | core execution path has no unresolved dynamic-allocation dependency |

The representative state-symbol audit measured ordinary, counted, token-aware,
and periodic storage separately. The ordinary state no longer carries stop,
counted, or periodic storage it cannot use.

No firmware build is required at the Stage 07 roadmap gate. The Stage 06
foundation firmware gate remains green; subsystem migration resumes at the
Stage 12 integration gate.

## 10. Specification Refinements

### Structural duration policy

Observed contract: registration policy examples require compile-time duration
values, but `std::chrono::duration` is not a structural non-type template
parameter in C++23.

Evidence: direct duration NTTP instantiation is rejected by the supported GNU
C++23 compiler even though ordinary `std::chrono` operations are supported.

Accepted change: `DurationValue` is a structural nanosecond value with `_ns`,
`_us`, `_ms`, and `_s` literals and conversion back to `std::chrono`.

Specifications updated: no architectural ownership changed; the implementation
uses the accepted conceptual duration examples with the structural literal
spelling required by C++23.

Verification added: periodic, deadline, service timeout, executor timeout, and
compile-time nonpositive-duration paths instantiate `DurationValue` policy.

No other specification amendment was required.

## 11. Firmware And Host Impact

Stage 07 does not migrate firmware by roadmap design. Firmware remains on its
Stage 06 minimal System/lifecycle shape until enough core subsystems are
available for the Stage 12 integration gate.

Host core tests continue to compile with Execution excluded outside Zephyr.
The execution implementation itself is Zephyr-native because its semantics are
defined by workqueues, threads, ISR context, timeout, and Kconfig behavior.

## 12. Known Limits And Deferred Work

Accepted limits are:

- one worker thread per explicit application workqueue;
- payload-free static registrations;
- no periodic catch-up queue or overlapping invocation;
- no per-occurrence cancellation handle;
- no runtime registration or queue migration;
- no service mailbox yet;
- no supervisor restart or in-process reboot reconstruction;
- no global stop guarantee for an in-flight system-workqueue handler that does
  not cooperate;
- no subsystem-specific registrations until those subsystem stages land.

These are accepted extension boundaries, not missing Stage 07 requirements.

## 13. Local Implementation Decisions

### Local decision: system-level registration lifecycle protocol

Problem: registrations are deliberate leaves rather than lifecycle components,
but they still require preparation, activation, admission closure, and
containment around component teardown.

Constraints: do not promote tasks into components; preserve dependency-aware
cleanup; keep lifecycle generic.

Options considered: make every registration a component; hide registration
lifecycle inside executor hooks; add one System-level execution protocol.

Decision: lifecycle invokes a typed `SystemExecutionProtocol` around component
activation and teardown.

Why: leaf identity and ownership remain intact while lifecycle receives the
minimum generic containment contract it needs.

Physical implementation: `lifecycle/protocol.hpp`, `lifecycle/engine.hpp`, and
`execution/protocol.hpp`.

Tests/evidence: registration activation barrier, stop ordering, cancellation,
uncontained dependency preservation, and full Stage 06 regressions.

Reversal path: another leaf protocol can replace `SystemExecutionProtocol`
without changing registration declarations, API, records, or executor storage.

### Local decision: named System boot binds the application frontend

Problem: `Robot::boot()` and global `solar::boot()` must establish the same
global execution API behavior without requiring a separate user binding call.

Constraints: one canonical state, no root-header inclusion from components,
strict and relaxed parity.

Options considered: bind only in global boot; require an explicit bind step;
bind inside named System boot.

Decision: named `System::boot<Application>()` establishes the application
protocol before invoking lifecycle boot; global boot delegates to it.

Why: both accepted boot spellings are complete operations and the common path
stays compact.

Physical implementation: `system/system.hpp`, `lifecycle/engine.hpp`, and
`execution/protocol.hpp`.

Tests/evidence: the representative fixture calls `fixture::System::boot()` and
then uses global `solar::execution` APIs in both strict and relaxed variants.

Reversal path: binding can move to a dedicated application bootstrap function
without changing subsystem state if a future composition-root rule requires it.

### Local decision: query-time executor aggregation

Problem: executor records need registration totals and activity counters, but
eager cross-updates would add lock ordering and duplicate canonical facts.

Constraints: focused coherent queries, bounded work, no monolithic snapshot.

Options considered: update executor counters on every registration transition;
store one shared global aggregate; derive aggregates while querying.

Decision: immutable registration membership is compile time, and mutable
executor aggregates are folded from coherent registration records at query
time.

Why: registration records remain canonical and hot execution paths avoid an
additional executor lock.

Physical implementation: `execution/api.hpp` and `execution/runtime.hpp`.

Tests/evidence: executor query tests verify registration count, active count,
submission, completion, and pending facts.

Reversal path: measured hot-query pressure could add executor atomics while
retaining the same public `ExecutorRecord`.

### Local decision: capability-selected registration storage

Problem: an initial uniform state charged every registration for a stop source,
counted counter, and periodic release tick.

Constraints: static exact ownership and negligible common-path cost.

Options considered: retain uniform storage; external side tables; compile-time
empty-base storage selection.

Decision: registration state privately inherits capability-selected storage
policies.

Why: the common path owns only its native work and shared record/lifecycle
state; optional features pay their own exact cost with no runtime branch.

Physical implementation: `execution/runtime.hpp`.

Tests/evidence: token-aware runtime test plus emitted state-size audit showing
328 B ordinary, 336 B counted, and 384 B token-aware on-demand states.

Reversal path: storage policies are private and can move into composed members
or separate slots without changing declarations or APIs.

### Local decision: diagnostic containment after primary rejection

Problem: invalid authored targets or service declarations can cascade through
unrelated catalog and dependency templates, obscuring the primary diagnostic.

Constraints: compile-fail tests need focused stable tokens; rejected programs
do not need runtime semantics.

Options considered: accept compiler cascades; duplicate validation layers;
substitute safe internal template shapes after the primary assertion.

Decision: invalid targets and malformed service policy use private fallback
types only after emitting the owning static assertion.

Why: diagnostics stay local without weakening successful-program validation.

Physical implementation: `execution/runtime.hpp` and
`execution/service_runtime.hpp`.

Tests/evidence: ten compile-fail fixtures each match one stable intended token.

Reversal path: compiler constraint diagnostics can replace the fallback shapes
when toolchain output becomes comparably focused.

## 14. Documentation Handoff

The public documentation pass should explain:

- when a sustained loop is a service and when bounded work is a registration;
- how to choose the system workqueue versus an explicit application executor;
- native coalescing versus `Counted<N>` admission;
- ordinary, delayable, periodic, and poll-triggered declaration examples;
- `schedule` versus `reschedule`, asynchronous versus synchronous cancel, and
  flush semantics;
- lifecycle activation and why work is unavailable before final boot commit;
- cooperative `StopToken` behavior and the limits of system-workqueue
  containment;
- strict and relaxed binding setup;
- Kconfig defaults, ceilings, and forced-abort policy;
- ISR-safe submission and prohibited waiting operations;
- how to read focused service, registration, executor, and system-target
  records.

The executable source in `tests/zephyr/execution/src/main.cpp` is the primary
documentation example seed.

## 15. Closure Statement

Stage 07 is complete. Solar now has one bounded, Zephyr-native execution plane
with explicit resource ownership, typed registration, lifecycle containment,
strict/relaxed API parity, focused records, stable rejection diagnostics, and
no hidden executor or heap dependency.

The exact-tree host and Zephyr regressions are green. Stage 08 Typed Bus is now
unblocked and can use explicit Execution targets for deferred routes without
inventing a worker or scheduler of its own.
