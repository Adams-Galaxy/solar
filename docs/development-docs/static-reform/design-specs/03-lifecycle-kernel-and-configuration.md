# Lifecycle, Kernel, Execution, And Configuration Contract

Date: 2026-07-15

Status: accepted design

Owning phase: Phase 3

Depends on:

- `00-design-conventions.md`
- `00a-modern-cpp-result-and-status.md`
- `01-system-blueprint-and-binding.md`
- `02-identity-contributions-and-catalogs.md`

## 1. Purpose

This specification defines the common runtime contract that every later Solar
subsystem must obey.

It establishes:

- which effective-blueprint entries participate in lifecycle;
- optional static lifecycle hooks and their accepted return types;
- dependency-ordered initialization, start, stop, rollback, and deinit;
- lifecycle semantics for passive components;
- service and executor containment rules;
- focused lifecycle and execution query surfaces;
- the boundary between Solar-owned execution and Zephyr-owned infrastructure;
- the public role of typed kernel primitives;
- Kconfig and typed C++ policy responsibilities;
- configuration precedence and invalid-combination diagnostics;
- the current rejection of in-process reboot.

The common application path remains direct:

```cpp
auto boot = solar::boot();

if (!boot)
{
    const auto report = solar::lifecycle::boot_report();
    // Application-specific failure response.
}
```

Components remain static types. Lifecycle orchestration is owned directly by
the bound `solar::System<Blueprint>` specialization and its type-owned static
state. No runtime system object, context object, service instance, or global
service locator is introduced.

## 2. Non-Goals

This specification does not define:

- subsystem-specific catalogs or record fields;
- task and executor registration syntax;
- executor scheduling policy beyond lifecycle and containment requirements;
- automatic service restart;
- controlled in-process reboot;
- dynamic component registration;
- runtime dependency mutation;
- a universal system snapshot;
- application-specific failure policy;
- a replacement scheduler for Zephyr;
- ownership of arbitrary application-created Zephyr threads;
- subsystem-specific typed errors beyond their lifecycle boundary mapping.

Later subsystem specifications may add focused initialization and execution
details. They must use this lifecycle rather than inventing another system
initialization model.

## 3. Architectural Separation

Solar exposes three related but distinct runtime domains.

### 3.1 Lifecycle

Lifecycle answers:

- which effective components were admitted into the system;
- which lifecycle operations were present and attempted;
- which operations succeeded or failed;
- which component caused boot or stop failure;
- whether rollback or cleanup completed;
- the current logical state of the system and each component.

Lifecycle does not claim that every running component owns a thread.

### 3.2 Execution

Execution answers:

- which services and executors own sustained work;
- whether their Solar-owned execution was created and started;
- whether cooperative stop was requested;
- whether execution exited, joined, timed out, or was aborted;
- whether execution remains capable of accessing dependencies;
- which optional native diagnostics are available.

Execution records are focused facts about active work. They are not lifecycle
records with another name.

### 3.3 Kernel

Kernel provides typed building blocks over Zephyr primitives. It answers
low-level questions about a particular primitive or native thread when such a
query is supported.

Using a Solar kernel wrapper does not register a component and does not grant
Solar lifecycle ownership of the surrounding application type.

### 3.4 No universal runtime object

These domains share identifiers and may reference one another, but they do not
merge into one mutable `Runtime`, `Context`, or `Snapshot` aggregate.

The global API resolves through the application binding established by Phase
1:

```cpp
solar::lifecycle::record<RemoteService>();
solar::execution::service<RemoteService>();
solar::kernel::thread_diagnostics(thread);
```

Each query has a specific owner, synchronization rule, and availability
contract.

## 4. Lifecycle Participation

### 4.1 Effective components participate

Every component in the normalized effective component graph receives a
lifecycle record and participates in dependency ordering.

The component categories are:

- device;
- facility;
- service;
- executor.

This revises the old component-kind vocabulary by removing boards,
peripherals, channels, and tasks and adding executors. Boards and raw hardware
belong to the Hardware layer. The typed application bus replaces channels.
Tasks and other jobs are execution registrations rather than component
branches.

### 4.2 Catalog declarations are not components

Parameters, events, metrics, logs, Remote Data, Actions, Topics, Streams,
subscriptions, and jobs do not receive base lifecycle records merely because
they appear in a catalog.

They are declarations owned by a component or subsystem facility. A runtime
failure involving one of these declarations is attributed to its owning
component and may additionally carry a focused catalog reference.

### 4.3 Built-in facilities

Every built-in belongs to one explicit inclusion class:

- **demand-derived:** Bus, Parameters, Events, Metrics, and Remote are included
  only when enabled and demanded by the effective blueprint;
- **Kconfig-selected:** Logging, Health, and Inspection are included whenever
  their Kconfig capability is selected, even if no application catalog entry
  independently demands them;
- **required-derived:** a built-in is included because another selected
  facility or service requires it, such as Supervisor requiring Health or a
  configured Remote link requiring the Remote facility and service.

In every class, effective-blueprint normalization produces the one canonical
component and validates that its implementation is enabled.

The user does not need to list ordinary Solar facilities manually. Explicit
listing may be supported where a facility has application-selected policy,
but it must normalize to one effective component rather than duplicate the
built-in.

An enabled but unused demand-derived built-in is not included solely to produce
a lifecycle record. A Kconfig-disabled built-in is absent, not permanently
represented in the `Disabled` state. Intentional use of a disabled capability
is a normalization error.

### 4.4 User facilities

Applications may declare their own facilities. They receive the same optional
lifecycle hooks, dependency behavior, descriptor rules, and contribution
opportunities as other components.

### 4.5 Leaf registrations

Tasks, deferred jobs, periodic jobs, subscriptions, and similar registrations
are leaves. Their owning executor or facility participates in lifecycle.

Activation failure for a leaf is reported through the owner with an optional
leaf catalog reference. It does not create an independent component lifecycle
branch.

## 5. Component Hook Contract

### 5.1 Optional hooks

A component may declare any of the following static functions:

```cpp
static solar::Status init();
static solar::Status start();
static solar::Status stop();
static solar::Status deinit();
```

Each hook is optional unless a later component-category contract explicitly
requires it.

No system type, system access type, runtime object, or context object is passed
to a lifecycle hook. A component includes and calls its direct typed
dependencies normally.

### 5.2 Accepted return forms

The permanent accepted hook return forms are:

```cpp
solar::Status
solar::Result<void>
```

`solar::Result<void>` means `std::expected<void, solar::Status>` under the
Phase 0 result contract.

Normalization is exact:

- `Status::Ok` becomes success;
- every other `Status` becomes `std::unexpected(status)`;
- `Result<void>` success remains success;
- `Result<void>` failure preserves its `Status`;
- `bool`, integers, arbitrary result-like types, and unrelated return types are
  rejected at compile time.

Supporting both accepted forms is intentional. `Status` is compact for simple
lifecycle boundaries, while `Result<void>` composes naturally with C++23
expected-based implementation code.

### 5.3 Typed subsystem errors

A subsystem may retain a richer typed error in its own focused record and API.
It must map that error deliberately to `solar::Status` before crossing the
generic lifecycle hook boundary.

```cpp
solar::Result<void> ParameterStore::init()
{
    return load_partition()
        .transform([](const auto&) {})
        .transform_error(status_of);
}
```

Generic lifecycle storage does not type-erase arbitrary error objects into an
opaque byte buffer or variant of every application error type.

### 5.4 Void migration

Legacy `void` lifecycle hooks may receive narrowly scoped migration support
during implementation, but `void` is not part of the final accepted contract.
Migration support must be diagnosable and removable.

A missing hook and a present `void` hook are not permanently conflated.

### 5.5 Hook detection

Hook detection is expression-based and occurs after effective-blueprint
formation. It must not require instantiating application method bodies before
the global system binding is visible.

This preserves the include and implementation model established by Phase 1.

### 5.6 No exceptions across the boundary

Lifecycle hooks must use non-throwing control flow. Solar does not catch and
translate arbitrary exceptions from component hooks. Architecture and build
configuration should disable or avoid exceptions consistently with the Phase
0 platform contract.

## 6. Service Run Contract

### 6.1 Required service operation

A service represents sustained active behavior and must provide one accepted
run form:

```cpp
static solar::Status run(solar::StopToken stop);
```

or:

```cpp
static solar::Result<void> run(solar::StopToken stop);
```

The return normalization rules are identical to lifecycle hooks.

### 6.2 Stop token

`solar::StopToken` is a small public cooperative-cancellation vocabulary type.
It may be backed by kernel machinery, but service code does not need to name a
system, runtime, or native Zephyr thread.

At minimum, a token must allow a service to:

- query whether stop was requested;
- wait or block through a supported cancellation-aware primitive;
- pass a non-owning copy to helper code.

Cancellation is cooperative until the configured timeout and abort policy are
applied by Solar.

### 6.3 Expected return

A successful return after stop was requested is a clean service exit.

A successful return before stop was requested is normalized to
`Status::UnexpectedExit`. Returning an explicit failure preserves that status
regardless of whether cancellation was requested.

### 6.4 No hidden readiness claim

Successful thread creation means the service execution was launched. It does
not imply an application-specific network connection, sensor calibration, or
protocol handshake has completed.

A service that requires synchronous readiness before dependants start must
establish it in `init()` or `start()`, or expose an explicit typed dependency
contract in a later subsystem design. Boot must not infer readiness from a
thread merely existing.

## 7. Lifecycle States

### 7.1 System states

The public system lifecycle states are:

```cpp
enum class SystemState
{
    Dormant,
    Initializing,
    Starting,
    Running,
    Stopping,
    Deinitializing,
    RollingBack,
    Stopped,
    Failed,
};
```

The exact underlying integer type is an implementation detail. The semantic
values are stable public vocabulary.

### 7.2 Component states

The public component lifecycle states are:

```cpp
enum class ComponentState
{
    Registered,
    Initializing,
    Initialized,
    Starting,
    Running,
    Stopping,
    Stopped,
    Deinitializing,
    Deinitialized,
    Failed,
};
```

`Disabled` is removed. Disabled or unrequired compile-time features are absent
from the effective component graph.

### 7.3 Meaning of running

For a component, `Running` means admitted and available within the running
system. It does not mean the component owns a thread.

A passive facility with no `start()` hook still enters `Running` when its
start phase is successfully traversed. Execution records determine whether a
service or executor has active work.

### 7.4 Transitional observability

Solar updates the public state before invoking each hook and records the
outcome after the hook returns. Concurrent thread-context queries may therefore
observe transitional states while boot or stop is in progress.

This is truthful observability, not an invitation to mutate lifecycle from a
hook or query callback.

## 8. Hook Presence And Outcomes

Lifecycle storage records hook presence separately from invocation outcome.

```cpp
enum class HookOutcome
{
    NotPresent,
    NotAttempted,
    Succeeded,
    Failed,
};
```

The distinction is required:

- `NotPresent` means the component did not declare the optional hook;
- `NotAttempted` means the hook exists but orchestration did not reach it;
- `Succeeded` means an invocation returned success;
- `Failed` means an invocation returned a non-OK status.

An absent optional hook is a successful lifecycle omission. It is not
`Status::NotSupported`, not a warning, and not a boot failure.

If a present hook explicitly returns `Status::NotSupported`, that is a real
failed invocation. The author declared the operation and then reported that it
could not complete.

## 9. Dependency Ordering

### 9.1 One effective component DAG

Lifecycle order comes from the effective component dependency DAG produced by
blueprint normalization.

Solar does not impose fixed category phases such as all devices before all
services. If one component must be ready
before another, the architecture expresses that relationship as a typed
dependency.

### 9.2 Stable topological order

When multiple valid topological orders exist, Solar uses a deterministic order
derived from normalized component order. This makes reports and tests stable
without turning source order into a dependency.

### 9.3 Two forward sweeps

Boot uses two complete dependency-ordered sweeps:

1. initialize every component in topological order;
2. start every component in topological order.

The first sweep ensures the complete admitted graph is initialized before
active start behavior begins. The second sweep still guarantees that each
dependency is started before its dependant.

### 9.4 Reverse teardown

Stop, rollback, and deinit traverse reverse topological order so dependants
release their use of a dependency before that dependency is torn down.

## 10. Boot Algorithm

### 10.1 Entry conditions

`solar::boot()` resolves the bound application system and is valid only from
`SystemState::Dormant`.

The conceptual signature is:

```cpp
solar::Result<solar::BootReport, solar::BootError> solar::boot();
```

Report storage is type-owned, bounded, and queryable separately. The exact
concrete report type may carry a capacity derived from the bound system while
remaining available through the global API.

### 10.2 Boot sequence

A valid boot performs:

1. serialize entry against other lifecycle-changing operations;
2. initialize lifecycle and execution records from immutable catalogs;
3. transition the system to `Initializing`;
4. invoke or omit each `init()` hook in topological order;
5. transition the system to `Starting`;
6. invoke or omit each `start()` hook in topological order;
7. prepare each service's execution after that service's `start()` succeeds,
   but hold its user `run()` body behind the system activation barrier;
8. prepare executor-owned registrations and keep their admission gated;
9. transition each successfully admitted component to `Running`;
10. commit the system to `Running`;
11. release the activation barrier, allowing service `run()` bodies and
    executor registrations to execute;
12. publish the completed boot report and return success.

An executor is a component. Its later detailed task and job activation model
must fit into steps 6 through 8 without introducing a second boot mechanism.

### 10.3 Component start atomicity

For a service, successful component start includes both:

- successful completion or omission of `start()`;
- successful creation and preparation of its Solar-owned execution.

Failure to create the thread or other required execution resource is a start
failure attributed to that service. Preparation must use a Zephyr-native
suspended, delayed, or explicitly gated mechanism so user code cannot run
before the final system commit. Releasing a prepared execution is not a second
fallible boot phase.

### 10.4 Subsystem facility failure

A built-in subsystem facility reports initialization and start failures in the
same way as an application component. It is not granted a hidden failure path.

When failure concerns a particular catalog declaration, the lifecycle failure
subject contains:

- the owning facility or executor component reference;
- the lifecycle operation;
- the normalized `Status`;
- an optional typed catalog reference to the affected leaf declaration.

The facility's focused subsystem record retains any richer typed error.

### 10.5 Failure during final activation

If owned execution exits unexpectedly before boot reaches `Running`, boot fails
and enters rollback.

An unexpected exit after successful boot updates the service execution record,
marks the service failed, and transitions the system to `Failed`. The base
contract does not automatically restart the service or begin global teardown.
The application may call `solar::stop()` to perform best-effort containment.

## 11. Boot Rollback

### 11.1 Primary failure is stable

The first failure that prevents boot remains the primary boot failure.
Cleanup failures do not replace or obscure it.

### 11.2 Rollback sequence

On initialization failure, Solar:

1. transitions the system to `RollingBack`;
2. deinitializes components whose initialization succeeded, in reverse order;
3. records every attempted cleanup outcome;
4. transitions the system to `Failed`.

On start or activation failure, Solar:

1. transitions the system to `RollingBack`;
2. requests cancellation of started Solar-owned execution;
3. contains that execution according to timeout and abort policy;
4. invokes `stop()` for components whose start phase succeeded;
5. invokes `deinit()` for components whose initialization succeeded;
6. records cleanup failures separately from the primary failure;
7. transitions the system to `Failed`.

### 11.3 Attempt only earned cleanup

Solar tracks successful lifecycle stages explicitly:

- `stop()` is attempted only for a component admitted through start;
- `deinit()` is attempted only for a component admitted through init;
- missing cleanup hooks are successful omissions;
- failed cleanup does not cause the same hook to be called repeatedly.

### 11.4 Continue where safe

Rollback continues after ordinary hook failures so reports describe the full
best-effort cleanup.

If execution remains uncontained, Solar preserves the transitive dependency
closure that execution may still access. Independent graph branches may still
be cleaned up.

## 12. Stop And Deinitialization

### 12.1 Public stop

The conceptual API is:

```cpp
solar::Result<solar::StopReport, solar::StopError> solar::stop();
```

Normal stop is valid from `Running`. Stop is also valid from `Failed` when
resources from a previously running system remain active and require
best-effort containment.

Calls from `Dormant`, an already fully `Stopped` system, or a boot failure that
already completed rollback are rejected without erasing the retained report.

### 12.2 Stop sequence

Solar performs:

1. transition the system to `Stopping`;
2. request cooperative stop from active services and executors in reverse
   dependency order;
3. contain each owned execution using join, timeout, and abort policy;
4. invoke or omit `stop()` in reverse dependency order;
5. transition the system to `Deinitializing`;
6. invoke or omit `deinit()` in reverse dependency order;
7. publish the stop report;
8. transition to `Stopped` if teardown completed, otherwise `Failed`.

Cancellation requests may be issued to all active owned execution before Solar
blocks waiting for individual joins. Resource teardown still respects reverse
dependency order.

### 12.3 Stop-hook failure

An ordinary `stop()` or `deinit()` hook failure is recorded and teardown
continues. A generic non-OK hook does not by itself prove that code remains
concurrently capable of accessing a dependency.

Only known uncontained execution automatically blocks dependency teardown.
A later component contract may expose a stronger explicit containment result,
but it must not infer that condition from an arbitrary status code.

### 12.4 Final state

An entirely clean stop ends in `Stopped`.

A forced abort, failed hook, join timeout, abort failure, or preserved
dependency produces a failed `StopReport`. If all execution was contained,
Solar may still finish deinitialization, but abnormal cleanup remains visible.

## 13. Thread Containment

### 13.1 Containment sequence

For each Solar-owned service thread, shutdown performs:

1. request stop through the service's stop source;
2. join for the effective configured timeout;
3. if joined, mark execution cleanly contained;
4. if timed out and abort is enabled, attempt native abort;
5. if abort succeeds, mark execution forcibly contained;
6. if abort is disabled or fails, mark execution uncontained.

Timeout value zero means an immediate non-blocking join attempt. It does not
mean forever.

### 13.2 Forced abort

Forced abort remains enabled by the current Kconfig default. It may be
overridden by typed policy according to the configuration precedence rules.

A successful forced abort permits dependency teardown because execution is no
longer running. It remains an abnormal outcome and causes the stop or rollback
report to record failure.

Solar may abort only execution that Solar created and owns. It must never abort
an arbitrary thread merely because a native thread identifier was observed.

### 13.3 Uncontained execution

When execution remains alive, its component and every transitive dependency
remain potentially in use.

Solar must not call destructive stop or deinit hooks on those dependencies.
Records identify them as cleanup-blocked rather than pretending deinit
succeeded.

The implementation may continue cleaning independent branches whose resources
cannot be reached by the uncontained execution.

### 13.4 Executor containment

Phase 9 will define exact executor and job APIs. Every executor design must
provide equivalent answers:

- can new work be prevented from entering;
- can queued work be drained or cancelled;
- has active work stopped;
- does any work remain capable of accessing dependencies;
- is native abort available and permitted.

An executor cannot claim lifecycle stop success while owned work remains
uncontained.

## 14. Repeated Boot And Reset

### 14.1 Current policy

Only the first boot from `Dormant` is supported.

Rejected cases are distinguished:

- another lifecycle operation is active: `BootError::Busy`;
- the system is already running: `BootError::AlreadyRunning`;
- the system is stopped or failed: `BootError::RebootUnsupported`.

Exact enumerator spelling may be refined during implementation, but these
reasons must remain distinguishable.

### 14.2 No public reset

Solar exposes no production reset operation in this design generation.

Safe in-process reboot would require every participating component, facility,
service, executor, subsystem record store, and native resource to define which
state is reconstructed. That contract is deliberately deferred.

### 14.3 Reports survive rejection

A rejected repeated boot does not clear or replace the original boot report.
A rejected stop similarly does not destroy the last meaningful stop report.

### 14.4 Test isolation

Tests obtain isolated static state by binding distinct application or test tags
to distinct system types. A hidden production reset hook must not be added only
to make tests convenient.

## 15. Lifecycle Records

### 15.1 Canonical record ownership

The bound system owns one fixed lifecycle record for every effective component.
Records are indexed by the typed local component identity established in Phase
2.

The component type is not required to declare a mutable `last_error` member.
Canonical cross-component lifecycle facts belong to lifecycle storage.

### 15.2 Minimum component record

A lifecycle record contains at least:

- immutable component descriptor reference or descriptor view;
- local component ID and component category;
- current component state;
- presence and outcome for `init`, `start`, `stop`, and `deinit`;
- last lifecycle operation;
- last normalized status;
- first lifecycle failure, if any;
- successful-stage facts required for rollback;
- transition or attempt counters useful for detecting illegal repetition;
- cleanup-blocked state when dependency preservation applies.

Service thread facts do not expand this record into a second execution record.
The lifecycle record may reference the corresponding service execution entry.

### 15.3 Failure subject

A generic lifecycle failure contains:

- component identity;
- lifecycle operation;
- normalized `Status`;
- whether it was the primary or cleanup failure;
- an optional catalog kind and local leaf ID;
- enough ownership metadata to reach the focused subsystem record.

It does not contain an arbitrary application error object.

### 15.4 Synchronization

Mutable lifecycle records and reports are mutex-protected.

Solar never holds the lifecycle record mutex while:

- invoking application hooks;
- joining a thread;
- waiting for executor work;
- aborting execution;
- calling a subsystem adapter that may block.

The operation controller serializes boot and stop separately from the short
record-copy critical sections.

### 15.5 Query context

Mutable lifecycle queries are thread-context APIs unless a future focused API
is explicitly documented as ISR-safe. They must not pretend a Zephyr mutex can
be taken from interrupt context.

Immutable component descriptors retain the broader read rules established by
Phase 2.

## 16. Reports And Errors

### 16.1 Boot report

`BootReport` provides a bounded summary of the attempted boot:

- initial and final system state;
- whether initialization and start completed;
- primary failure, if any;
- completed-operation counts;
- rollback attempted and completed facts;
- cleanup failure count and retained bounded details;
- uncontained execution and preserved dependency counts.

Detailed per-component outcomes remain in lifecycle records rather than being
duplicated without limit into the report.

### 16.2 Boot error

`BootError` classifies why the value result is unavailable, including:

- busy or invalid state;
- already running;
- reboot unsupported;
- component lifecycle failure;
- execution activation failure;
- internal invariant failure.

When boot was attempted, the separately queryable report is authoritative for
the component and operation details.

### 16.3 Stop report and error

`StopReport` similarly records:

- clean and abnormal exits;
- hook failures;
- joins and timeouts;
- abort attempts and outcomes;
- uncontained execution;
- preserved dependencies;
- completed stop and deinit operation counts.

`StopError` distinguishes invalid entry state from an attempted but abnormal
shutdown.

### 16.4 Fixed capacity

Reports use fixed, compile-time capacity derived from the effective graph or a
validated Kconfig ceiling. They do not allocate dynamically during failure
handling.

When a summary retains fewer cleanup details than occurred, it records the
total count and explicit truncation. Per-component lifecycle and execution
records remain available for complete focused inspection.

## 17. Public Lifecycle Surface

The normal global API is:

```cpp
solar::boot();
solar::stop();

solar::lifecycle::state();
solar::lifecycle::components();
solar::lifecycle::record<LeftMotor>();
solar::lifecycle::boot_report();
solar::lifecycle::stop_report();
```

An explicit-system surface may exist for tests and tooling:

```cpp
solar::lifecycle::Of<TestApplication>::record<LeftMotor>();
```

The bound global surface remains the application default.

`components()` returns a coherent bounded value or immutable query view with a
documented lifetime. It does not expose mutable record references after the
mutex is released.

There is no generic `solar::snapshot()`.

### 17.1 Inspection and Remote boundaries

Inspection may aggregate focused lifecycle and execution queries for a user
interface or diagnostic command. That aggregation is a read adapter and does
not become the canonical owner of the underlying records.

Remote may expose selected lifecycle or execution facts through explicit Data,
Actions, Topics, or Streams defined by the later Remote specification. It must
not serialize an internal record layout wholesale or assume build-local IDs are
stable wire identity.

External references use the stable identity rules from Phase 2. A record may
use a dense local component or catalog ID internally, while an adapter resolves
that ID to an explicit or manifest-controlled stable ID before transmission.

Remote lifecycle control, such as requesting stop or future reboot, is a
separately authorized command surface. Merely exposing lifecycle diagnostics
does not grant permission to mutate lifecycle.

## 18. Zephyr And Solar Ownership

### 18.1 Zephyr owns the platform runtime

Zephyr owns:

- scheduling and native thread semantics;
- kernel object implementation;
- interrupt dispatch;
- clocks, ticks, and timeout mechanics;
- device model initialization;
- system work queue implementation;
- native runtime statistics capabilities;
- platform-specific abort and join behavior.

Solar does not replace or conceal these platform truths.

### 18.2 Solar owns what it creates

Solar owns lifecycle and containment only for execution it creates on behalf
of:

- services;
- executors;
- built-in facilities;
- future explicitly Solar-owned worker infrastructure.

Solar records the native relationship but does not claim ownership of every
thread visible to Zephyr.

### 18.3 Application-owned native execution

An application may use raw Zephyr APIs or Solar kernel wrappers directly. Such
execution is application-owned unless it is explicitly registered through a
future executor or ownership adapter.

Observability does not imply ownership. A native handle escape hatch does not
grant Solar permission to stop or abort that object.

## 19. Public Kernel Building Blocks

### 19.1 Intended surface

`solar::kernel` may provide typed, allocation-conscious wrappers for:

- thread and static thread storage;
- stop source and stop token support;
- mutex;
- semaphore;
- queue and message queue;
- work and delayable work;
- owned work queue;
- timer;
- timeout and deadline;
- polling;
- scheduler and current-thread facts;
- interrupt-context facts;
- native handle access.

The exact wrapper inventory follows demonstrated framework needs. A wrapper is
not added merely to rename every Zephyr function.

### 19.2 Wrapper principles

Public kernel wrappers must:

- preserve static storage and deterministic ownership;
- avoid hidden dynamic allocation;
- expose explicit failure through `Status` or `Result`;
- state whether operations are thread-safe or ISR-safe;
- preserve native units and scheduling semantics accurately;
- provide an explicit native-handle escape hatch where needed;
- avoid implicit component or catalog registration.

### 19.3 ISR contracts

Every kernel operation documents one of:

- thread context only;
- ISR-safe;
- ISR-safe only in a non-blocking form;
- context-dependent with a separate explicit variant.

No operation becomes ISR-safe merely because it is a static global call.

## 20. Execution Records

### 20.1 Service execution record

Each effective service has a canonical execution record containing at least:

- owning service component identity;
- configured stack size and priority;
- configured stop timeout and abort policy;
- thread-created and thread-started facts;
- currently running fact;
- stop-requested fact;
- exited fact;
- exited-after-stop-request fact;
- normalized run result;
- join attempted, joined, and timed-out facts;
- abort attempted, succeeded, or failed facts;
- clean, forced, or uncontained containment state;
- native thread identity when available;
- optional stack and runtime diagnostics.

### 20.2 Executor execution record

Each effective executor receives an analogous focused record. Phase 9 may add
queue depth, active job, deadline, overrun, or worker-specific fields.

Those additions remain under `solar::execution`, not lifecycle or a universal
system record.

### 20.3 Public execution surface

The intended query shape is:

```cpp
solar::execution::service<RemoteService>();
solar::execution::services();
solar::execution::executor<ControlExecutor>();
solar::execution::executors();
```

Exact record types may be system-specialized to preserve static capacity.

### 20.4 Future common worker

Phase 9 may define a built-in executor for common deferred, periodic, and
one-shot work. It will be an ordinary effective executor component whose
storage, lifecycle, and diagnostics obey this specification.

Jobs submitted to it remain execution leaves rather than lifecycle components.

## 21. Focused Kernel Diagnostics

### 21.1 Precise naming

The existing broad `ThreadSnapshot` vocabulary should become the more precise
`ThreadDiagnostics`.

The focused API shape is:

```cpp
auto facts = solar::kernel::thread_diagnostics(thread);
```

This operation concerns one thread and does not imply a coherent snapshot of
the entire system.

### 21.2 Partial availability

Aggregate diagnostics represent optional measurements explicitly:

```cpp
struct ThreadDiagnostics
{
    std::optional<std::size_t> stack_used;
    std::optional<std::size_t> stack_free;
    std::optional<std::uint64_t> runtime_cycles;
};
```

Exact fields may evolve, but unavailable information must not be represented by
a believable zero, empty string, null native ID with no presence marker, or
stale cached value presented as current.

### 21.3 Direct optional queries

A direct request for a disabled capability returns an explicit typed failure,
normally mapped to `Status::NotSupported` at a broad boundary.

An aggregate may use `std::optional` because partial availability is expected.
A focused operation whose sole purpose is the unavailable measurement should
use `Result`.

### 21.4 Kconfig capability relationship

Stack usage, thread naming, and runtime cycles are available only when the
required Zephyr and Solar Kconfig capabilities are enabled.

Solar validates required Zephyr symbols and fails configuration or compilation
with a focused message when a requested Solar capability lacks platform
support.

## 22. Configuration Ownership

### 22.1 Kconfig is authoritative for build capability

Solar is a Zephyr module. Kconfig is the authoritative source for build-level
feature inclusion and platform capability.

There is no `config.hpp` fallback that silently invents defaults when generated
Zephyr configuration is absent.

### 22.2 Kconfig responsibilities

Kconfig owns settings such as:

- whether Solar and built-in subsystem implementations are compiled;
- whether kernel diagnostics are compiled;
- whether Zephyr stack information or runtime statistics are required;
- hard global resource ceilings and compiled fixed capacities;
- dynamic-allocation permission or prohibition;
- module-wide logging integration;
- default service stop timeout;
- default forced-abort policy;
- defaults for built-in worker storage and stack sizes;
- platform features that affect Zephyr-generated code or native object layout.

Later subsystem specifications identify their own build capabilities and hard
ceilings under this rule.

### 22.3 Typed C++ policy responsibilities

C++ types own architecture-specific choices that belong to a component,
catalog, or effective blueprint, including:

- service and executor priority;
- per-component stack size;
- per-component stop timeout;
- per-component timeout response;
- executor selection;
- catalog-specific policy;
- subsystem behavior policy;
- typed references to devices, stores, transports, and executors.

Per-component static storage may naturally depend on typed C++ policy. A
Kconfig default supplies an ergonomic common value; the application type
selects a deliberate override when needed.

### 22.4 Configuration is not catalog membership

Configuration sections remain siblings of catalog sections as established in
Phase 1. Declarations and policy are not mixed into one undifferentiated type
pool.

## 23. Configuration Precedence

The general precedence rule is:

```text
explicit component policy
    > blueprint subsystem policy
    > Kconfig default
```

For example:

```cpp
struct RemoteService
{
    using Execution = solar::execution::Service<
        solar::execution::StackSize<2048>,
        solar::execution::Priority<4>,
        solar::execution::StopTimeout<100ms>,
        solar::execution::AbortOnTimeout<false>>;

    static solar::Result<void> run(solar::StopToken stop);
};
```

If an option is omitted, the next level supplies it. Normalization produces one
complete effective policy before runtime storage is formed.

### 23.1 Capability ceilings override preference

Precedence applies only among valid choices. A C++ policy cannot:

- re-enable a subsystem excluded by Kconfig;
- request runtime statistics Zephyr did not compile;
- exceed a hard Kconfig capacity ceiling;
- request dynamic allocation when globally prohibited;
- select a native operation unavailable on the target.

These are compile-time or Kconfig-time errors, not runtime fallback cases.

### 23.2 Intentional use of disabled features

Incidental cross-subsystem integration disappears when the sibling subsystem is
disabled. Code guarded by Solar's internal compile-time capability traits does
not attempt the unavailable integration.

Intentional application registration or API use of a disabled subsystem must
produce a clear compile-time diagnostic where the use is statically known. A
runtime `NotSupported` result is reserved for genuinely runtime-reachable
optional operations.

### 23.3 Invalid combinations

Validation diagnostics identify:

- the component or subsystem policy involved;
- the requested option;
- the Kconfig capability or ceiling that prevents it;
- the nearest valid remediation where practical.

Solar should use focused concepts and assertions rather than one enormous
template error emitted from final `System` instantiation.

## 24. Example Application

### 24.1 Service declaration

```cpp
// services/remote_service.hpp
#pragma once

#include "devices/radio.hpp"

#include <solar/execution.hpp>
#include <solar/result.hpp>

struct RemoteService
{
    using Dependencies = solar::Dependencies<Radio>;

    using Execution = solar::execution::Service<
        solar::execution::StackSize<2048>,
        solar::execution::Priority<4>,
        solar::execution::StopTimeout<100ms>>;

    static solar::Result<void> init();
    static solar::Status start();
    static solar::Result<void> run(solar::StopToken stop);
    static solar::Status stop();
    static solar::Result<void> deinit();
};
```

### 24.2 Passive facility

```cpp
struct CalibrationStore
{
    using Dependencies = solar::Dependencies<SettingsPartition>;

    static solar::Result<void> init();
    static solar::Status deinit();
};
```

`CalibrationStore` has no start or stop hook. Those operations are recorded as
`NotPresent`, and the facility still becomes logically available while the
system is running.

### 24.3 Composition

```cpp
using RobotBlueprint = solar::Blueprint<
    solar::Devices<Radio>,
    solar::Facilities<CalibrationStore>,
    solar::Services<RemoteService>>;

using RobotSystem = solar::System<RobotBlueprint>;

SOLAR_BIND_SYSTEM(RobotSystem);
```

### 24.4 Application entry

```cpp
int main()
{
    auto boot = solar::boot();

    if (!boot)
    {
        const auto report = solar::lifecycle::boot_report();
        return static_cast<int>(boot.error());
    }

    const auto remote = solar::execution::service<RemoteService>();
    return 0;
}
```

The component header does not include the composition root. Definitions that
call bound subsystem APIs follow the out-of-line include model established in
Phase 1.

## 25. Concurrency And Reentrancy

### 25.1 Serialized lifecycle mutation

Only one boot, stop, rollback, or deinit orchestration may mutate lifecycle at
a time.

A concurrent lifecycle-changing call fails as busy. It does not block forever,
enter a second orchestration, or wait while holding a record mutex.

### 25.2 Queries during transition

Thread-context queries may run during lifecycle transition. Each returned
record or report copy is internally coherent at the time it is copied.

Cross-call coherence is not promised. A caller that reads two records in two
calls may observe progress between them.

### 25.3 Hook reentrancy

A lifecycle hook must not call `solar::boot()` or `solar::stop()`. Such a call
fails as busy or reentrant rather than deadlocking.

Hooks may use ordinary subsystem APIs only where those APIs document that the
required facility stage is available. Later subsystem specifications must state
their pre-init, initialized, and running behavior.

## 26. Compile-Time Validation

Effective-system validation must reject:

- component dependency cycles;
- dependencies absent from the effective component graph;
- unsupported lifecycle hook return types;
- a service without an accepted `run(StopToken)` operation;
- duplicate or conflicting effective built-in facilities;
- execution policy values outside Kconfig ceilings;
- requested diagnostics without required Zephyr capabilities;
- executor policy that cannot establish containment semantics;
- compile-time registration of a disabled subsystem.

Runtime results remain for operations that can genuinely fail after a valid
architecture has compiled, such as hardware initialization, native thread
creation, join timeout, or subsystem storage I/O.

## 27. Implementation Direction

The existing implementation should be reformed rather than discarded.

Retain:

- fixed type-owned lifecycle storage;
- mutex-protected record copies;
- the invariant that user hooks execute outside record locks;
- typed kernel wrappers;
- service execution records;
- explicit stop request, join, timeout, and abort mechanics.

Revise:

- positional component descriptors to Phase 2 catalog identities;
- old board/peripheral/device category boot phases to DAG sweeps;
- `Task` and `Channel` component kinds;
- lifecycle hook detection and result normalization;
- absent-hook representation;
- `ThreadSnapshot` naming and diagnostic availability;
- service execution failure and dependency-preservation reporting;
- repeated-boot errors;
- configuration access to rely exclusively on generated Kconfig symbols.

Implementation should proceed in focused slices so lifecycle behavior remains
testable while component catalogs and execution policy are migrated.

### 27.1 Migration map

The intended migration includes:

- replace category-position `ComponentDescriptor` identity with Phase 2
  component catalog entries;
- replace old category `BootPhase` orchestration with two dependency-DAG
  sweeps;
- move bound application calls from `System::lifecycle` convenience surfaces
  to the normal `solar::lifecycle` namespace while retaining explicit-system
  test access;
- replace boolean hook-presence flags with explicit presence and outcome;
- migrate service `void run(...)` declarations to `Status` or `Result<void>`;
- move service-thread facts from lifecycle naming to `solar::execution`;
- rename `ThreadSnapshot` to focused `ThreadDiagnostics`;
- remove `Channel` and `Task` from component lifecycle vocabulary;
- retain bounded queues as kernel or component-owned primitives where needed;
- remove any remaining C++ fallback configuration path in favor of generated
  Zephyr Kconfig symbols.

Compatibility aliases may exist briefly while implementation slices land, but
new specifications and examples use only the accepted vocabulary.

## 28. Verification Requirements

The implementation must eventually cover:

- compile acceptance of `Status` and `Result<void>` hooks;
- compile rejection of `bool`, integral, arbitrary, and invalid hooks;
- compile rejection of services without valid `run`;
- passive components with every hook absent;
- deterministic topological init and start order;
- deterministic reverse stop and deinit order;
- initialization failure rollback;
- start and execution-creation failure rollback;
- preservation of the first failure during cleanup failure;
- unexpected service return before stop request;
- clean service return after stop request;
- join timeout with successful forced abort;
- join timeout with abort disabled;
- abort failure and transitive dependency preservation;
- cleanup of independent graph branches;
- repeated boot rejection without report loss;
- stop from a post-boot failed system;
- concurrent lifecycle-call rejection;
- query coherence under record mutexes;
- unavailable stack and runtime diagnostics;
- Kconfig default, blueprint override, and component override precedence;
- compile diagnostics for attempts to override disabled capabilities.

Tests should use distinct test-bound system types rather than resetting one
production system's static state.

## 29. Rejected Alternatives

### 29.1 Runtime system or context object

Rejected because Solar represents one statically composed firmware system and
the accepted global binding already provides unambiguous access.

### 29.2 Lifecycle only for active services

Rejected because passive devices and facilities still have initialization,
availability, failure, and teardown truth.

### 29.3 Lifecycle records for every catalog declaration

Rejected because parameters, events, subscriptions, metrics, and jobs are
leaves owned by facilities or executors rather than independent lifecycle
branches.

### 29.4 Fixed category boot phases

Rejected because category ordering cannot express the real dependency graph
and can conflict with valid cross-category dependencies.

### 29.5 Per-component init followed immediately by start

Rejected because two full sweeps establish a clearer initialized-system
boundary before active behavior begins.

### 29.6 Missing hooks return `NotSupported`

Rejected because optional absence is an architectural fact, not a failed
runtime attempt.

### 29.7 One mandatory hook return spelling

Rejected because both `Status` and `Result<void>` are precise, allocation-free,
and useful. Exact normalization avoids ambiguity without imposing ceremony.

### 29.8 Arbitrary result-like hook normalization

Rejected because implicit conversion of bools, integers, and unrelated error
types hides mistakes at a critical system boundary.

### 29.9 Generic type-erased lifecycle errors

Rejected because subsystem-specific records can preserve rich typed errors
without adding opaque storage and interpretation rules to every component
record.

### 29.10 Successful early service return

Rejected because a service represents sustained execution. Returning before
cancellation means its promised active behavior disappeared unexpectedly.

### 29.11 Always tear down dependencies after timeout

Rejected because an uncontained thread may still access those resources.

### 29.12 Treat forced abort as clean success

Rejected because containment was achieved only after cooperative shutdown
failed.

### 29.13 Solar owns every Zephyr thread

Rejected because observation and wrapper use do not imply lifecycle ownership
or permission to abort application execution.

### 29.14 Unavailable diagnostics represented as zero

Rejected because zero can be a valid measurement and would produce false
operational conclusions.

### 29.15 C++ fallback configuration header

Rejected because Solar is a Zephyr module and silent fallback values can create
a build that disagrees with Zephyr's configured capabilities.

### 29.16 C++ policy re-enables Kconfig-disabled features

Rejected because compiled capability is a hard boundary, not a default.

### 29.17 Production reset for test convenience

Rejected because test-bound system types provide isolation without pretending
all static component and native state is safely reconstructible.

### 29.18 Universal system snapshot

Rejected because lifecycle, execution, kernel, and later subsystem records have
different owners, synchronization, and availability semantics.

## 30. Accepted Decisions

1. Lifecycle, execution, and kernel remain distinct focused domains.
2. The bound static system directly owns lifecycle orchestration and storage.
3. Device, facility, service, and executor are components; boards and raw
   hardware endpoints remain outside the component graph.
4. Tasks, jobs, subscriptions, and subsystem declarations are leaves.
5. Every effective component receives a lifecycle record.
6. Built-in facilities use an explicit demand-derived, Kconfig-selected, or
   required-derived inclusion rule.
7. User facilities participate normally and may contribute declarations.
8. `init`, `start`, `stop`, and `deinit` are optional static hooks.
9. Hooks receive no system or context object.
10. Permanent hook returns are exactly `Status` or `Result<void>`.
11. Rich subsystem errors map explicitly to `Status` at lifecycle boundaries.
12. Missing hooks are successful omissions recorded as `NotPresent`.
13. A present hook returning `NotSupported` is a failed invocation.
14. Services require `run(StopToken)` returning `Status` or `Result<void>`.
15. Successful service return before cancellation is `UnexpectedExit`.
16. Component `Running` means logically available, not thread-owning.
17. Disabled compile-time features are absent rather than lifecycle-disabled.
18. Lifecycle ordering follows one effective component DAG.
19. Boot uses complete initialization and start sweeps.
20. Teardown and rollback use reverse dependency order.
21. The first boot failure remains primary through rollback.
22. Cleanup continues after ordinary hook failure where safe.
23. Uncontained execution preserves its transitive dependencies.
24. Independent graph branches may continue cleanup.
25. Forced abort is enabled by Kconfig default and remains abnormal.
26. Solar aborts only execution it owns.
27. Reboot and public reset remain unsupported.
28. Failed post-boot systems retain a best-effort stop path.
29. Reports and records are fixed-capacity and allocation-free.
30. Mutable lifecycle records are mutex-protected.
31. User hooks and blocking waits execute outside lifecycle record locks.
32. No component-local `last_error` member is mandatory.
33. Lifecycle failures may reference focused catalog leaves without promoting
    those leaves to components.
34. Zephyr owns scheduling and native kernel semantics.
35. Solar owns only execution it explicitly creates for framework components.
36. Kernel wrappers are public building blocks, not automatic components.
37. Service and executor facts live under `solar::execution`.
38. Thread diagnostics are focused and represent unavailable fields explicitly.
39. Kconfig is authoritative for compiled capability and hard ceilings.
40. Typed C++ policy owns component and blueprint architecture choices.
41. Precedence is component policy, then blueprint policy, then Kconfig default.
42. Typed policy cannot re-enable or exceed a Kconfig capability boundary.
43. Compile-time architecture errors are compile diagnostics, not runtime
    statuses.
44. There is no universal runtime snapshot.

## 31. Open Questions

There are no blocking open questions for Phase 4.

Later specifications must refine, without changing this contract:

- exact task and executor declaration syntax;
- built-in common worker policy and storage;
- cancellation semantics for queued and active jobs;
- subsystem-specific initialization and record fields;
- exact Kconfig names and hard capacity values;
- stable identity domains for focused failure leaf references;
- whether later controlled reboot policy is practical;
- additional native diagnostics supported by future Zephyr releases.

These are extensions of the accepted lifecycle, ownership, containment, and
configuration model rather than alternate runtime architectures.
