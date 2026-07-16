# Solar Static System Architecture Reform

Date: 2026-07-11

Status: proposed architectural direction and implementation guide

This document defines the intended reform of Solar into a static, type-driven
system orchestration layer for Zephyr firmware. It combines the strongest ideas
from `static-design-proposal.md` with the constraints and capabilities of the
current Solar implementation.

It is deliberately broader than the immediate implementation milestone. The
first priority remains Kernel and lifecycle. Later ideas are retained here so
the first implementation does not accidentally close off useful directions.

This document is not a claim that every API shown already exists. Examples are
target shapes. Where a detail is unsettled, that is stated explicitly.

## 1. Architectural Statement

Solar is a compile-time description and orchestration layer for one firmware
image.

The application declares the types that make up the system. Components expose
ordinary static APIs and own their own internal state. Solar validates the
declared graph, coordinates lifecycle, owns execution infrastructure, records
system-level runtime facts, and exposes those facts for inspection.

The division of responsibility is:

- The application defines what exists.
- Components define what they do.
- Solar defines how the declared system is brought to life and observed.
- Zephyr provides the kernel, hardware model, and native operating mechanisms.

Solar is not an object container, dependency injection container, service
locator, or replacement operating system. It is a static operating layer over
Zephyr for a single integrated firmware system.

## 2. Core Design Decisions

### 2.1 There is one statically addressed system

The user names the complete firmware system with a type alias:

```cpp
using RobotSystem = solar::System<
    solar::Board<RobotBoard>,
    solar::Peripherals<DrivePwm, SensorBus>,
    solar::Devices<LeftMotor, RightMotor, Lidar>,
    solar::Facilities<Events, Metrics>,
    solar::Services<Navigation, RemoteService>,
    solar::Tasks<PublishHealth>,
    solar::Channels<PoseChannel>,
    solar::Policies<RobotPolicies>>;
```

The system is used statically:

```cpp
int main()
{
    const auto report = RobotSystem::boot();
    return report ? 0 : -1;
}
```

There is no user-created `RobotSystem` object and no runtime reference that must
be passed around the application.

### 2.2 `System` is the real implementation, not a facade

`solar::System<...>` directly owns the static state required to coordinate its
declared firmware system. It must not merely forward to one hidden monolithic
`Runtime` or `Storage` object that reproduces the old object model behind a
static API.

This does not forbid internal objects. It means ownership should be divided by
responsibility:

```cpp
template<typename... Sections>
struct System {
    struct lifecycle;
    struct kernel;
    struct graph;

private:
    static inline BootReport boot_report_{};
    static inline SystemState system_state_{};
};
```

Lifecycle records, service-thread controls, task executor state, boot reports,
and similar runtime objects may each be static type-owned objects. Their
existence should match a concrete responsibility rather than a desire to place
everything into one generic context.

### 2.3 Components are identities represented by types

A component type represents one logical node in the firmware architecture.

```cpp
using LeftMotor = Motor<LeftPwm, LeftDirectionPin, false>;
using RightMotor = Motor<RightPwm, RightDirectionPin, true>;
using Lidar = TofLidar<SensorBus, 0x29>;
```

The type carries fixed identity and configuration. Runtime state remains
runtime state, stored statically by that component when appropriate:

```cpp
template<typename Pwm, typename DirectionPin, bool Reversed>
struct Motor {
    static solar::Status init();
    static solar::Status set_speed(int speed);
    static int speed();

private:
    static inline bool initialized_ = false;
    static inline int speed_ = 0;
};
```

Static addressability does not mean that all data is `constexpr`, nor does it
forbid ordinary internal objects such as mutexes, buffers, protocol parsers, PID
controllers, and Zephyr handles.

### 2.4 Solar does not provide application dependency access

Solar does not pass a system type, access type, context object, or service
locator into component lifecycle methods.

Application code includes and uses the types it actually depends on:

```cpp
#include "app/devices/motors.hpp"
#include "app/devices/lidar.hpp"

struct Navigation {
    using Dependencies = solar::Requires<
        app::LeftMotor,
        app::RightMotor,
        app::Lidar>;

    static solar::Status init();
    static solar::Status start();
    static solar::Status run(solar::kernel::StopToken stop);
    static solar::Status stop();
};
```

Inside the implementation, it calls those types directly:

```cpp
auto scan = app::Lidar::read();
app::LeftMotor::set_speed(command.left);
app::RightMotor::set_speed(command.right);
```

This is intentional. The complete firmware is one integrated program, and
normal C++ includes already express compile-time visibility. Solar's dependency
graph describes lifecycle and architecture; it is not a mechanism for finding
objects.

### 2.5 Components own behavior; Solar owns coordination

Components own:

- hardware behavior;
- domain behavior;
- internal mutable state;
- local synchronization;
- local data structures;
- component-specific error meanings;
- public domain APIs.

Solar owns:

- system composition;
- graph validation;
- dependency-aware lifecycle ordering;
- central lifecycle records;
- boot and stop coordination;
- Solar-created threads, stacks, and executor machinery;
- boot and stop reports;
- system-level inspection data;
- later failure, health, and supervision policy.

## 3. User-Facing System Declaration

### 3.1 Typed sections

The system declaration is assembled from categorized type lists:

```cpp
namespace app {

using System = solar::System<
    solar::Board<board::RobotBoard>,

    solar::Peripherals<
        board::LeftPwm,
        board::RightPwm,
        board::SensorBus,
        board::Console>,

    solar::Devices<
        LeftMotor,
        RightMotor,
        Lidar>,

    solar::Facilities<
        Events,
        Metrics>,

    solar::Services<
        Navigation,
        RemoteService>,

    solar::Tasks<
        PublishHealth>,

    solar::Channels<
        PoseChannel>,

    solar::Policies<
        RobotPolicies>>;

}
```

Long term, section order should not matter. `System` should discover sections by
their category and supply empty defaults for omitted categories. This makes the
declaration a set of architectural facts instead of a positional template
constructor.

The initial migration may preserve the current positional `System` template to
limit disruption. Section discovery is useful, but it is not required to make
lifecycle truthful.

### 3.2 Board cardinality

A system has zero or one board entry. Prefer an explicit wrapper:

```cpp
solar::Board<board::RobotBoard>
```

This keeps board handling structurally consistent with other sections while
allowing validation to enforce its special cardinality.

### 3.3 System entry

Zephyr entry integration should invoke the system type directly:

```cpp
int main()
{
    return app::System::boot() ? 0 : -1;
}
```

If a profile wrapper remains useful, it should name a type and invoke static
operations:

```cpp
template<typename Profile>
int boot_profile()
{
    using System = typename Profile::System;
    return System::boot() ? 0 : -1;
}
```

It must not construct `static typename Profile::System system{}`.

## 4. Component Categories

Categories describe architectural roles. They should help graph diagnostics,
default lifecycle expectations, and inspection. They should not create rigid
runtime inheritance hierarchies.

### 4.1 Board

The board is the firmware's broad platform entry. It may coordinate board-level
readiness, pin control, clocks, or application-specific platform setup that is
not already handled by Zephyr.

```cpp
struct RobotBoard {
    static constexpr std::string_view Name = "robot-board";

    static solar::Status init();
    static solar::Status start();
    static solar::Status stop();
};
```

Solar should not force a board hook where Zephyr already performs all required
initialization. A hookless board can still exist as an architectural identity.

### 4.2 Peripheral

A peripheral is a low-level Zephyr-facing hardware resource or typed adapter:

```cpp
using SensorBus = board::I2c<DT_NODELABEL(lpi2c1)>;
using LeftPwm = board::Pwm<DT_ALIAS(left_motor_pwm)>;
```

Peripherals should be thin. Solar should not recreate Devicetree or hide native
Zephyr devices. A typed adapter may expose both ergonomic operations and the
underlying Zephyr spec or device pointer.

### 4.3 Device

A device is meaningful physical or logical hardware built from peripherals:

```cpp
using LeftMotor = Motor<LeftPwm, LeftDirectionPin, false>;
using Lidar = TofLidar<SensorBus, 0x29>;
```

Devices are statically addressed and usually own static state or static
type-owned objects.

### 4.4 Facility

A facility is passive, shared software infrastructure:

```cpp
Events::publish(MotorFault{...});
Metrics::increment<PacketsReceived>();
```

Facilities may own internal storage and synchronization, but generally do not
have a dedicated continuously running thread. Examples include Events, Metrics,
Inspection support, configuration stores, and registries.

Facilities must remain a meaningful category, not a miscellaneous bucket.

### 4.5 Service

A service is one logical, long-lived active capability:

```cpp
struct RemoteService {
    using Dependencies = solar::Requires<RemoteTransport>;
    using Execution = solar::DedicatedThread<
        solar::StackSize<4096>,
        solar::Priority<3>>;

    static solar::Status init();
    static solar::Status start();
    static solar::Status run(solar::kernel::StopToken stop);
    static solar::Status stop();
};
```

A service type is unique within one system. Solar does not instantiate service
objects, and two copies of the same service do not run concurrently. If the
application needs multiple concurrent units of the same behavior, that is a
task, worker, executor, or separately configured service type.

Solar owns the execution infrastructure requested by the service. The service
owns its behavior and internal application state.

### 4.6 Task

A task describes work that is invoked, often periodically or in response to an
event:

```cpp
struct PublishHealth {
    using Dependencies = solar::Requires<SystemHealth, RemoteTransport>;

    static solar::Status execute();
};
```

Task behavior should eventually be separated from execution policy:

```cpp
using Tasks = solar::Tasks<
    solar::Scheduled<
        PublishHealth,
        solar::Periodic<500ms>,
        solar::Executor<SystemExecutor>>>;
```

Several tasks may share one executor. A dedicated thread remains available for
work that genuinely requires one. This redesign is important, but it is later
than the first lifecycle reform.

### 4.7 Channel

A channel is a typed communication endpoint:

```cpp
using PoseChannel = solar::LatestValue<Pose>;
using EventQueue = solar::Queue<SystemEvent, 32>;
```

Channels may wrap Zephyr queues, message buses, ring buffers, or pub/sub
mechanisms. Their type is their identity.

Whether a channel has a complete lifecycle record depends on whether it has a
meaningful initialization and shutdown process. At minimum, registered channels
must be visible as graph entries. The first lifecycle implementation should
avoid pretending a passive compile-time-only channel is `Running` if that state
has no useful meaning.

## 5. Lifecycle Contract

### 5.1 What lifecycle means

Lifecycle is Solar's authoritative record of where the complete system and each
managed graph entry are in the process of becoming available, operating, and
shutting down.

It serves four purposes:

1. It drives deterministic boot and stop behavior.
2. It prevents the framework from claiming work succeeded when it did not.
3. It preserves the component and stage associated with failures.
4. It provides structured facts to Inspection, Health, Supervisor, Remote, and
   tests.

Lifecycle is not the component's entire domain state. A motor may be idle,
driving, braking, or fault-latched while its Solar lifecycle state is `Running`.
Solar tracks orchestration state; the component tracks domain state.

### 5.2 Static optional hooks

Components expose plain static lifecycle hooks. Solar detects which hooks exist.

The target common forms are:

```cpp
static solar::Status init();
static solar::Status start();
static solar::Status stop();
static solar::Status deinit();
```

For a continuously executing service:

```cpp
static solar::Status run(solar::kernel::StopToken stop);
```

For a scheduled task:

```cpp
static solar::Status execute();
```

Not every component needs every hook. Missing hooks are successful no-ops for
orchestration purposes, while metadata should retain whether a hook actually
exists when that distinction is diagnostically useful.

For consistency, lifecycle hooks should converge on `solar::Status`, including
`stop()` and `deinit()`. Permitting `void` temporarily may ease migration, but a
`void` stop cannot report a hardware or cleanup failure. Solar should normalize
supported signatures internally and deprecate ambiguous variants deliberately.

### 5.3 No Solar template parameter on hooks

The following is not part of the target user contract:

```cpp
template<typename System>
static solar::Status init();
```

Nor is:

```cpp
template<typename Access>
static solar::Status init();
```

Components include their dependencies and use them by type. Framework-provided
components such as Remote may be configured with catalog types or bound by an
internal adapter, but user lifecycle methods remain ordinary static functions.

### 5.4 Lifecycle states

The recommended first state model is:

```cpp
enum class LifecycleState {
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
    Disabled,
};
```

`Registered` means present in the declared graph but not yet initialized.

`Initialized` means `init()` completed successfully, or the entry required no
init hook and Solar accepted it as initialized.

`Running` means startup completed and the entry is available in the role Solar
manages. For a service with a run loop, this also implies its thread was started
and has not yet reported exit.

`Stopped` means coordinated stop completed. It does not mean memory was
destroyed; static component state continues to exist.

`Failed` means the most recent lifecycle operation failed or an active service
run loop exited with failure.

`Disabled` is reserved for policy-controlled omission. It should not be used as
a vague substitute for uninitialized or unsupported.

The transition states are valuable because hooks may block and inspection may
occur concurrently. If the first implementation cannot expose concurrent
inspection safely, they should still be recorded around calls because this
makes the state machine truthful and prepares it for later supervision.

### 5.5 Service run-loop exit

A service run loop needs more detail than one lifecycle enum can carry. Solar
should record at least:

- whether execution was started;
- whether the run loop has exited;
- the returned `Status`;
- whether stop had been requested before exit;
- whether joining timed out;
- whether Solar aborted the thread after timeout;
- the native thread identifier when available.

Recommended interpretation:

- Successful exit after a stop request becomes `Stopped` once stop coordination
  completes.
- Failed exit becomes `Failed` immediately.
- Unexpected successful exit while the service is expected to remain active is
  also abnormal. Record the successful return separately, but set lifecycle to
  `Failed` using a Solar error such as `UnexpectedServiceExit`.
- A service that intentionally completes should not be modeled as a long-lived
  service; it is better represented as a task or a service with an explicit
  completion policy.

### 5.6 Failed stop

Do not add `StoppedWithError` to the base lifecycle enum initially. Keep the
primary state as `Failed` and retain operation-specific details in the lifecycle
record:

```cpp
record.last_operation == LifecycleOperation::Stop;
record.last_status == ...;
```

This avoids multiplying states for every operation while preserving the actual
fact: stop failed.

## 6. Lifecycle Ownership

### 6.1 Recommended hybrid storage

Use a hybrid model:

- `System` owns canonical lifecycle records for all declared graph entries.
- Specialized runtime wrappers own operational state needed to perform their
  work, such as a service thread and stop source.
- Canonical records reference or copy relevant operational facts for querying.

Conceptually:

```cpp
template<typename Component>
struct LifecycleRecordStorage {
    static inline LifecycleRecord value{};
};

template<typename Service>
struct ServiceExecutionStorage {
    static inline solar::kernel::Thread thread{};
    static inline solar::kernel::StopSource stop_source{};
    static inline ServiceRunRecord run{};
};
```

These storage types should be scoped to the `System` specialization so the same
component type can be used by separate test systems without accidentally sharing
Solar's lifecycle metadata:

```cpp
template<typename... Sections>
struct System {
private:
    template<typename Component>
    struct LifecycleStorage;

    template<typename Service>
    struct ServiceRuntime;
};
```

The component's own static data still belongs to the component type. Solar's
metadata belongs to the system specialization.

### 6.2 Why not one runtime object

A monolithic runtime object would restore object ownership and references as the
hidden architectural center. It would also mix unrelated lifetimes and make the
static system API cosmetic.

Distributed type-owned storage gives each concern a clear identity:

- lifecycle records store lifecycle facts;
- service runtimes control service execution;
- task executors schedule tasks;
- reports preserve orchestration outcomes;
- the system state records whole-system progress.

This is still real storage. It is simply organized around responsibilities and
owned directly by the system type.

## 7. Lifecycle Query API

### 7.1 Avoid one broad `snapshot()`

A single `snapshot()` becomes vague as soon as it includes graph identity,
lifecycle, health, service execution, kernel diagnostics, metrics, and failure
history. It either returns an oversized unstable structure or silently omits
important detail.

The system should expose focused query areas:

```cpp
RobotSystem::lifecycle::state();
RobotSystem::lifecycle::components();
RobotSystem::lifecycle::record<RemoteService>();
RobotSystem::lifecycle::failures();

RobotSystem::kernel::service_threads();
RobotSystem::kernel::thread<RemoteService>();

RobotSystem::graph::components();
RobotSystem::graph::dependencies<Navigation>();

RobotSystem::boot_report();
RobotSystem::stop_report();
```

The exact names can evolve, but the separation is important.

### 7.2 Lifecycle is the canonical source; Inspection is the consumer

Lifecycle ownership belongs to `System`, because Solar's boot and stop machinery
creates those facts. Inspection should consume the canonical lifecycle API and
provide formatting, filtering, paging, and transport-friendly views.

```cpp
auto records = RobotSystem::lifecycle::components();
auto page = Inspection::component_page(records, request);
```

Later, Health and Supervisor consume the same records:

```cpp
const auto remote = RobotSystem::lifecycle::record<RemoteService>();
Health::evaluate(remote);
Supervisor::handle(remote);
```

This keeps lifecycle truthful even when Inspection, Remote, Health, or
Supervisor are not registered.

### 7.3 Suggested record structures

```cpp
enum class ComponentKind {
    Board,
    Peripheral,
    Device,
    Facility,
    Service,
    Task,
    Channel,
};

enum class LifecycleOperation {
    None,
    Init,
    Start,
    Run,
    Stop,
    Deinit,
};

struct ComponentDescriptor {
    ComponentId id;
    std::string_view name;
    ComponentKind kind;
};

struct LifecycleRecord {
    ComponentDescriptor component;
    LifecycleState state;
    LifecycleOperation last_operation;
    Status last_status;
    std::uint64_t transition_count;
};
```

Timestamps should be added when there is one agreed monotonic time source and a
real consumer. They are useful, but not necessary for the first truthful state
model.

The query should return bounded, allocation-free views suitable for firmware:

```cpp
static std::span<const LifecycleRecord> components();
```

If records are generated through heterogeneous per-type storage, Solar may build
a fixed-size system-owned projection array when queried or update it during
transitions. Avoid heap allocation.

## 8. Graph And Dependencies

### 8.1 Dependencies are declared by type

```cpp
struct Navigation {
    using Dependencies = solar::Requires<
        LeftMotor,
        RightMotor,
        Lidar>;
};
```

Dependencies mean:

- the required type must be registered in the system graph;
- it must initialize before the dependant;
- it must start before the dependant where ordering is applicable;
- it must stop after the dependant;
- it should appear in graph inspection.

Dependencies do not grant access. Includes and normal C++ names grant access.

### 8.2 Validate identity by type

Presence and uniqueness should be type-based, not name-based. Names exist for
humans and external diagnostics; types are the architectural identity.

Solar should diagnose:

- duplicate registered component types;
- missing required dependency types;
- dependency cycles;
- invalid category/cardinality combinations;
- unsupported execution declarations;
- duplicate service types.

Names should also be unique where external protocols require stable lookup, but
two types sharing a display name is a naming error, not proof they are the same
component.

### 8.3 Ordering

The target lifecycle order is a topological ordering of the complete managed
graph.

Broad category phases remain useful for understandable reports and constraints,
but they should not contradict dependencies. A device depending on a facility
must not be forced before that facility merely because a fixed category list
says devices come first.

An incremental route is:

1. Make current category ordering and state transitions truthful.
2. Validate dependencies by type.
3. Produce one topological initialization order.
4. Use reverse topological order for stop.
5. Decide whether explicit broad-phase barriers are still needed.

### 8.4 Direct calls and declared graph drift

Solar cannot inspect a function body and infer every static type it calls.
Therefore dependency declarations are an architectural and lifecycle contract,
not a complete C++ access-control mechanism.

The default model is intentionally loose:

- any visible static type can be called;
- Solar only orchestrates registered components;
- code review and tests ensure meaningful runtime dependencies are declared.

Templates can encode dependencies structurally where valuable:

```cpp
template<typename Left, typename Right, typename RangeSensor>
struct NavigationService {
    using Dependencies = solar::Requires<Left, Right, RangeSensor>;
};
```

Solar should not force every component into that shape.

### 8.5 Optional dependencies: deferred

Preserve this future shape:

```cpp
using OptionalDependencies = solar::Optional<Metrics, Tracing>;
```

Optional dependencies are appropriate for genuinely optional instrumentation or
diagnostics. They should not hide missing core hardware or silently alter safety
behavior.

Because Solar does not provide a system access type, conditional use needs an
explicit compile-time configuration known to the component, or a system query
used in code already coupled to the concrete system. The exact API remains
deferred. Do not reintroduce `Use<System>` solely to support this feature.

## 9. Boot Semantics

### 9.1 Required properties

Boot must be:

- deterministic;
- allocation-free unless a component explicitly chooses otherwise;
- truthful about the first failure;
- ordered so dependencies are ready before dependants;
- prevented from starting active execution before mandatory initialization is
  complete;
- inspectable after success or failure.

### 9.2 Whole-system state

The system itself needs a state distinct from component lifecycle:

```cpp
enum class SystemState {
    Dormant,
    Booting,
    Initialized,
    Starting,
    Running,
    Stopping,
    Stopped,
    Failed,
};
```

`RobotSystem::lifecycle::state()` returns this whole-system state.

### 9.3 Conceptual boot sequence

The target sequence is:

```text
Validate graph at compile time
        |
Reset boot report and lifecycle records
        |
Mark system Booting
        |
Initialize entries in dependency order
        |
Initialize Solar-owned execution storage
        |
Start non-executing components in dependency order
        |
Start service threads and task executors
        |
Mark system Running
```

No service run loop should observe a partially initialized mandatory system.

The current category-based implementation can migrate in stages, but its phase
labels must be accurate. Facility failures must use facility phases, real
component names should replace generic labels, and the board must participate in
the same reporting model.

### 9.4 Boot report

The first implementation should preserve:

```cpp
struct BootReport {
    Status status;
    BootPhase failed_phase;
    ComponentDescriptor failed_component;
    std::size_t completed_operations;
};
```

The exact representation of “no failed component” should be explicit, such as
an optional descriptor or invalid component ID.

Retain the first failure. Later cleanup failures may be placed in a separate stop
or rollback report rather than overwriting the cause of boot failure.

### 9.5 Boot failure rollback

When boot fails, Solar should stop only entries whose relevant startup stage
succeeded, in reverse order. Rollback should continue after individual failures
and preserve both:

- the original boot failure;
- rollback failures.

Full rollback reporting may be deferred, but the state model should not mark an
entry `Stopped` if its stop hook failed.

## 10. Stop Semantics

### 10.1 Full graph shutdown

Stop is not only “stop service threads.” It is coordinated teardown of the
managed graph.

Target behavior:

```text
Mark system Stopping
        |
Stop task scheduling / request task completion
        |
Request service run-loop stop
        |
Join service threads
        |
Call service stop hooks
        |
Stop remaining entries in reverse dependency order
        |
Optionally deinitialize entries in reverse dependency order
        |
Mark system Stopped, or Failed if any stop failed
```

With current categories, the approximate reverse order is tasks, services,
devices, facilities, peripherals, board. Dependency ordering ultimately takes
precedence.

### 10.2 Continue after failure

Stop should continue after an individual failure. Firmware teardown often needs
to make as much of the system safe as possible. Solar should retain the first
stop failure and count or retain bounded details for additional failures.

```cpp
struct StopReport {
    Status status;
    ComponentDescriptor first_failed_component;
    LifecycleOperation failed_operation;
    std::size_t failure_count;
};
```

### 10.3 Service timeout policy

The current practical behavior of request stop, join with timeout, then abort on
timeout is acceptable as an explicit first policy. It must be documented and
observable.

Record:

- stop requested;
- join timeout duration;
- timeout occurrence;
- forced abort occurrence;
- final run-loop status when available.

Later, timeout and recovery action can move into service execution policy or
Supervisor policy. For the first milestone, one predictable default is better
than an unfinished policy framework.

## 11. Service Execution

### 11.1 Static service, type-owned runtime

The service remains a static type. Solar creates one execution runtime for that
service within the system specialization:

```cpp
template<typename Service>
struct ServiceRuntime {
    static inline solar::kernel::Thread thread{};
    static inline solar::kernel::StopSource stop_source{};
    static inline ServiceRunRecord run_record{};
};
```

There is no `Service*` member. The entry function invokes `Service::run(token)`
directly.

### 11.2 Execution declaration

Services describe required execution without creating Zephyr threads:

```cpp
using Execution = solar::DedicatedThread<
    solar::StackSize<4096>,
    solar::Priority<3>>;
```

Solar owns:

- thread control block;
- stack storage;
- stop source/token;
- startup and join;
- thread name;
- diagnostics;
- timeout and abort behavior.

The service owns:

- its run-loop algorithm;
- its internal synchronization;
- its protocol/domain state;
- its reaction to a cooperative stop request.

### 11.3 Kernel query surface

Kernel-specific operational facts belong under `System::kernel`:

```cpp
auto threads = RobotSystem::kernel::service_threads();
auto remote = RobotSystem::kernel::thread<RemoteService>();
```

A thread record may contain:

```cpp
struct ServiceThreadRecord {
    ComponentId service;
    bool created;
    bool running;
    bool stop_requested;
    bool exited;
    bool join_timed_out;
    bool aborted;
    std::size_t configured_stack_bytes;
    OptionalValue<std::size_t> unused_stack_bytes;
    OptionalValue<std::size_t> used_stack_bytes;
    OptionalValue<NativeThreadId> native_id;
    Status run_status;
};
```

Use an explicit availability representation for diagnostics that depend on
Zephyr configuration. Zero must not ambiguously mean either “none used” or “not
available.”

Only Solar-owned threads should be listed initially. Enumerating every Zephyr
thread is a separate system-wide diagnostic feature.

## 12. Tasks And Executors

The current Solar task model owns one object and one thread per task. That is a
valid transitional implementation, but it should not define the long-term
meaning of a task.

The target separation is:

```cpp
struct UpdateOdometry {
    static solar::Status execute();
};

using ScheduledOdometry = solar::Scheduled<
    UpdateOdometry,
    solar::Periodic<10ms>,
    solar::Executor<ControlExecutor>>;
```

Execution policies may include:

- periodic shared executor;
- event-triggered shared executor;
- Zephyr work queue;
- dedicated thread;
- manually invoked task;
- one-shot delayed work.

Multiple registrations of the same behavior with different policy require an
explicit registration identity, not duplicate raw component types. This API is
deferred until after static services and lifecycle are stable.

## 13. Remote And System Catalogs

Remote currently relies on a runtime context to discover system-contributed
methods, topics, observables, metrics, and events. The context should be removed
without forcing application components to know a Solar access abstraction.

Two reasonable internal shapes are:

### Configured service alias

```cpp
using RemoteService = solar::services::Remote<
    RemoteTransport,
    RobotRemoteCatalog>;
```

`RobotRemoteCatalog` can be assembled from component contribution lists during
the application system declaration.

### System-bound internal adapter

```cpp
using BoundRemote = solar::detail::BindService<
    RemoteService,
    RobotSystem::catalogs>;
```

This adapter is framework implementation detail. The user service lifecycle API
still has no `System`, `Use<System>`, or `Context` parameter.

Prefer explicit configured aliases if they remain readable. Use internal binding
only where circular type formation makes explicit catalog construction
impractical.

Remote should query focused system surfaces:

```cpp
RobotSystem::lifecycle::components();
RobotSystem::kernel::service_threads();
RobotSystem::boot_report();
```

It should not depend on a universal snapshot structure.

## 14. Contributions, Metrics, And Events

The current contribution-list idea remains compatible with the static system:

```cpp
struct Navigation {
    using Metrics = solar::Metrics<LoopTime, NavigationCycles>;
    using Events = solar::Events<PathLost, GoalReached>;
    using RemoteMethods = solar::RemoteMethods<SetGoal>;
};
```

`System` can collect these type lists at compile time. Collection does not
require component objects or lifecycle context.

Catalog ownership should remain separate from metric/event runtime storage:

- the graph determines what descriptors exist;
- the relevant facility owns runtime values, buffers, and subscriptions;
- Remote exports selected views;
- lifecycle only reports orchestration state.

This prevents lifecycle records from becoming another broad snapshot.

## 15. Health And Supervisor

Health and Supervisor are later consumers of lifecycle and kernel facts. They do
not own the underlying truth.

### Health

Health answers evaluative questions such as:

- Is the system operational?
- Which required components are failed?
- Is a service thread unexpectedly absent?
- Is a component degraded but usable?
- Are stack or timing margins unhealthy?

### Supervisor

Supervisor decides actions such as:

- record and continue;
- restart a service;
- retry initialization;
- disable an optional component;
- transition to a safe mode;
- reboot the system.

The relationship should be:

```text
System lifecycle + kernel records
              |
              v
            Health
              |
              v
          Supervisor policy
```

The first milestone builds accurate records. It should not prematurely implement
automatic restart or degraded-mode policy.

## 16. Logging And Errors

Logging should become C++ ergonomics over Zephyr logging, not a parallel runtime
object graph. Components should log directly through a type-aware static API or
Zephyr-compatible macros.

Component-specific errors remain defined near the component. Solar wraps
lifecycle failures with system context:

```cpp
struct LifecycleFailure {
    ComponentDescriptor component;
    LifecycleOperation operation;
    Status status;
};
```

Later error policy may decide whether boot aborts, degrades, retries, or enters a
safe state. For the first implementation:

- mandatory init/start failure aborts boot;
- the first failure is preserved;
- failure is recorded on the component;
- already-started components are stopped as safely as possible.

## 17. Names And Metadata

Every externally inspectable component needs stable metadata. Prefer a simple
static convention initially:

```cpp
struct Navigation {
    static constexpr std::string_view Name = "navigation";
};
```

Solar may derive a compile-time type name for developer diagnostics, but protocol
and user-facing names should not depend on compiler-specific pretty-function
strings.

Metadata should distinguish:

- type identity used by compile-time graph algorithms;
- stable component ID used by bounded runtime records;
- display/protocol name used by humans and Remote;
- component category.

Avoid dependency validation by name.

## 18. Testing Model

Static components remain testable through compile-time substitution:

```cpp
using TestMotor = Motor<FakePwm, FakeDirectionPin, false>;

using TestSystem = solar::System<
    solar::Devices<TestMotor>>;
```

Solar-owned lifecycle storage is scoped to `TestSystem`, so tests can create
separate system specializations with separate orchestration records.

Component static state needs deliberate test reset support where repeated tests
share one process:

```cpp
#ifdef CONFIG_SOLAR_TESTING
static void reset_for_test();
#endif
```

The first lifecycle tests should cover:

- successful static boot;
- each hook transition;
- missing hooks;
- named first boot failure;
- service thread startup and normal cooperative exit;
- unexpected service exit;
- service join timeout and forced abort;
- full reverse stop;
- stop continuing after one failure;
- board lifecycle visibility;
- truthful component records after partial boot failure;
- unavailable kernel diagnostics represented explicitly.

Compile-time graph tests should later cover missing types, duplicate types, and
cycles.

## 19. Proposed Public API Example

### Hardware and devices

```cpp
// app/devices/motors.hpp
#pragma once

#include "board/drive.hpp"
#include <solar/core/status.hpp>

namespace app {

template<typename Pwm, typename Direction, bool Reversed>
struct Motor {
    using Dependencies = solar::Requires<Pwm, Direction>;

    static solar::Status init()
    {
        speed_ = 0;
        return Direction::configure_output();
    }

    static solar::Status start()
    {
        return set_speed(0);
    }

    static solar::Status stop()
    {
        return set_speed(0);
    }

    static solar::Status set_speed(int speed)
    {
        speed = std::clamp(speed, -100, 100);
        Direction::write(Reversed ? speed < 0 : speed >= 0);
        Pwm::set_duty(std::abs(speed));
        speed_ = speed;
        return solar::ok();
    }

    static int speed()
    {
        return speed_;
    }

private:
    static inline int speed_ = 0;
};

using LeftMotor = Motor<board::LeftPwm, board::LeftDirection, false>;
using RightMotor = Motor<board::RightPwm, board::RightDirection, true>;

}
```

### Service

```cpp
// app/services/navigation.hpp
#pragma once

#include "app/devices/lidar.hpp"
#include "app/devices/motors.hpp"
#include <solar/kernel/stop_token.hpp>

namespace app {

struct Navigation {
    static constexpr std::string_view Name = "navigation";

    using Dependencies = solar::Requires<LeftMotor, RightMotor, Lidar>;
    using Execution = solar::DedicatedThread<
        solar::StackSize<4096>,
        solar::Priority<3>>;

    static solar::Status init()
    {
        mode_ = Mode::Idle;
        return solar::ok();
    }

    static solar::Status start()
    {
        mode_ = Mode::Searching;
        return solar::ok();
    }

    static solar::Status run(solar::kernel::StopToken stop)
    {
        while (!stop.stop_requested()) {
            const auto scan = Lidar::read();
            if (!scan) {
                LeftMotor::set_speed(0);
                RightMotor::set_speed(0);
                continue;
            }

            const auto command = calculate_command(*scan);
            LeftMotor::set_speed(command.left);
            RightMotor::set_speed(command.right);
            solar::kernel::sleep_for(10ms);
        }

        return solar::ok();
    }

    static solar::Status stop()
    {
        mode_ = Mode::Stopped;
        LeftMotor::set_speed(0);
        return RightMotor::set_speed(0);
    }

private:
    enum class Mode { Idle, Searching, Stopped };
    static inline Mode mode_ = Mode::Idle;
};

}
```

### Task

```cpp
// app/tasks/publish_health.hpp
#pragma once

#include "app/facilities/system_health.hpp"
#include "app/services/remote.hpp"

namespace app {

struct PublishHealth {
    static constexpr std::string_view Name = "publish-health";
    using Dependencies = solar::Requires<SystemHealth, RemoteService>;

    static solar::Status execute()
    {
        return RemoteService::publish(SystemHealth::report());
    }
};

}
```

### System

```cpp
// app/system.hpp
#pragma once

#include "app/devices/lidar.hpp"
#include "app/devices/motors.hpp"
#include "app/facilities/system_health.hpp"
#include "app/services/navigation.hpp"
#include "app/services/remote.hpp"
#include "app/tasks/publish_health.hpp"
#include "board/robot_board.hpp"
#include <solar/system.hpp>

namespace app {

using System = solar::System<
    solar::Board<board::RobotBoard>,
    solar::Peripherals<
        board::LeftPwm,
        board::RightPwm,
        board::LeftDirection,
        board::RightDirection,
        board::SensorBus>,
    solar::Devices<LeftMotor, RightMotor, Lidar>,
    solar::Facilities<SystemHealth>,
    solar::Services<Navigation, RemoteService>,
    solar::Tasks<
        solar::Scheduled<PublishHealth, solar::Periodic<500ms>>>,
    solar::Policies<RobotPolicies>>;

}
```

### Entry and queries

```cpp
#include "app/system.hpp"

int main()
{
    const auto boot = app::System::boot();
    if (!boot) {
        return -1;
    }

    const auto navigation =
        app::System::lifecycle::record<app::Navigation>();

    const auto service_threads =
        app::System::kernel::service_threads();

    return 0;
}
```

Application modules do not need to include `app/system.hpp` merely to call one
another. They include the domain headers they use. Code that asks questions
about the complete system naturally includes the system definition.

## 20. Migration From Current Solar

The current implementation is object-oriented at its center. `System` stores
component tuples and service runtime objects, creates a `Context`, and passes
object references into lifecycle hooks. Services such as Remote use the context
to access system catalogs. Entry code constructs a static system object.

The migration should proceed in contained slices.

### Phase A: truthful lifecycle on the current graph

1. Expand and settle lifecycle state and operation enums.
2. Add one system-scoped lifecycle record for every category and board.
3. Update records before and after each existing hook.
4. Correct boot phase labels and real component names.
5. Record service run-loop outcome, exit, timeout, and abort.
6. Implement full graph stop and a stop report.
7. Add focused `lifecycle` and `kernel` query surfaces.

This phase may temporarily adapt current object calls internally. Its output must
already be semantically compatible with the static target.

### Phase B: static `System`

1. Make `System::boot()` and `System::stop()` static.
2. Replace board and component tuple instances with type-list iteration.
3. Move framework state into responsibility-specific static storage.
4. Remove the hidden top-level system object from Zephyr entry.
5. Keep query APIs stable across this internal change.

### Phase C: static components and services

1. Detect and call static component hooks.
2. Remove `Context` from lifecycle signatures.
3. Remove stored service pointers from service runtimes.
4. Invoke `Service::run(stop_token)` directly.
5. Convert Remote to explicit catalog configuration or internal system binding.
6. Convert firmware components to direct includes and static calls.

### Phase D: graph correctness

1. Validate dependency presence by type.
2. Validate duplicate types and names separately.
3. Detect cycles.
4. Generate dependency-aware init/start order.
5. Generate reverse dependency stop order.

### Phase E: task execution reform

1. Separate task behavior from execution policy.
2. Add shared executor scheduling.
3. Adapt dedicated tasks as an explicit policy.
4. Integrate work items and Zephyr work queues where appropriate.

### Phase F: observability consumers

1. Build Inspection formatting and paging over focused query APIs.
2. Expose lifecycle and kernel records through Remote.
3. Add Health evaluation.
4. Add Supervisor policy only after reliable failure records exist.

## 21. Immediate Kernel And Lifecycle Implementation Slice

The next implementation should remain smaller than the entire reform. It should
complete the foundation on which the static conversion depends.

### Required now

- Finalize lifecycle states and operation semantics.
- Adopt system-scoped canonical lifecycle records.
- Include board, peripherals, devices, facilities, services, tasks, and relevant
  channels.
- Record transitions and first failure truthfully.
- Split facility boot phases from service phases.
- Replace generic failed-component labels with real descriptors.
- Record service thread execution details and final run status.
- Implement full graph stop with continue-on-failure behavior.
- Preserve first stop failure.
- Add `System::lifecycle` and `System::kernel` focused queries.
- Keep thread diagnostics bounded and explicit about unavailable values.
- Add tests for successful, failed, partial, and stopped states.

### Next, directly after the foundation

- Convert `System` operations and storage to static ownership.
- Remove the entry-created system object.
- Convert lifecycle hooks to static, parameterless calls.
- Remove `Context` from user components.
- Convert services to static types with Solar-owned runtimes.
- Adapt Remote catalog access.

### Deferred intentionally

- Optional dependencies.
- Policy-driven degraded boot.
- Automatic service restart.
- Shared task executors.
- Rich event history and timestamps.
- Runtime enumeration of non-Solar Zephyr threads.
- General component membership access control.
- Universal snapshots.
- Dynamic registration or plugin-style runtime components.

## 22. Important Constraints

### No dynamic component registry

Solar describes a firmware image known at compile time. Dynamic runtime
registration is outside the intended default architecture.

### No implicit component instances

Registering a device or service type must not cause Solar to manufacture an
application object. Its type is the component identity; its static storage or
type-owned internal objects hold state.

### No hidden service duplication

A service type appears at most once in a system. Distinct configured aliases are
distinct service identities only when they truly represent different logical
services.

### No broad context parameter

Lifecycle and run hooks receive only capability-specific values that are part of
the operation, such as a stop token. They do not receive a universal context.

### No heap requirement

System records and query views should be fixed-size and allocation-free by
default.

### No false observability

Unknown diagnostic data is represented as unavailable. Registered components do
not all report the same placeholder state. A stopped thread is not called
running. A timed-out stop is not reported as clean.

## 23. Resolved Implementation Decisions

The initial implementation uses the following decisions. Later policy may add
choices, but it must not silently change these defaults.

### 23.1 Boot result

`System::boot()` returns `Result<BootReport>`. On success, the value is the
completed boot report. On failure, `Result::status()` contains the failure status
and the complete report remains available through `System::boot_report()`.

The existing `Result<T>` currently stores a default-constructed `T` even on
failure, so callers must not inspect `value()` unless the result has a value.
Improving `Result<T>` storage and access safety is useful core work, but is not a
prerequisite for the static lifecycle architecture.

### 23.2 Repeated boot

The only initial policy is to reject a boot request unless the system is
`Dormant`. A second call while booting, running, stopped, or failed returns
`Status::Already` or the more precise existing status chosen during
implementation.

Controlled in-process reboot remains a future lifecycle policy. It requires an
explicit reset contract for component static state, Solar runtime storage,
channels, facilities, kernel objects, and failure records. It must not be
approximated by calling `boot()` again after `stop()`.

### 23.3 Deinitialization

`deinit()` is implemented as a complete optional lifecycle capability from the
first lifecycle model. Components are not required to provide it. Solar invokes
it in reverse dependency order for entries that initialized successfully and
records `Deinitializing`, `Deinitialized`, or `Failed` truthfully.

### 23.4 Missing lifecycle hooks

Every graph entry participates in lifecycle records, but each hook is optional.
A missing hook is represented as not implemented/not applicable in component
metadata and is treated as a successful no-op by the default policy.

Solar should not return `Status::NotSupported` through ordinary boot flow for a
missing optional hook. That would turn the normal shape of passive facilities
and channels into an error path. A later category or Kconfig policy may require
particular hooks and reject a graph at compile time or boot validation time.

After successful orchestration, a passive component may still be `Running` to
mean available as part of the running system, even if it has no active thread or
`start()` implementation. Hook availability and lifecycle availability are
separate facts.

### 23.5 Channels

Channels are full graph entries and receive lifecycle records. Like every other
component, they may implement any useful subset of lifecycle hooks. A passive
channel with no hooks still progresses through the system lifecycle as an
available managed entry.

### 23.6 Names and descriptors

The first implementation continues using the existing static `Name` convention.
Type identity remains canonical for graph logic. A later reform replaces or
augments `Name` with descriptor traits that provide stable IDs, names,
categories, and protocol metadata without forcing those members into every
component type.

### 23.7 Component errors

Each Solar lifecycle record contains at least `last_status` and the operation
that produced it. This gives every component a bounded Solar-owned `last_error`
equivalent without requiring application components to duplicate framework
state.

Components may later provide an optional error customization point or descriptor
for richer domain-specific reasons. This should be part of a lightweight
component-cooperation concept, not a mandatory mutable `last_error` member.
Simple components remain valid when they only return `Status`.

### 23.8 Lifecycle synchronization

Lifecycle records and projection tables are mutex-protected initially. Query APIs
return copies or views whose validity and locking semantics are explicit; they
must not expose mutable records after releasing the lock.

Service run-loop updates and inspection queries may execute concurrently, so the
implementation must define one lock-order rule and avoid invoking component
hooks while holding the lifecycle mutex.

### 23.9 Service run result

The initial service contract is `Result<void> run(StopToken)`, with `Status::Ok`
meaning normal completion. Any non-OK status means failure. Unexpected successful
exit before a stop request remains a Solar-level `UnexpectedServiceExit`
failure.

The current `Result<void>` carries a `Status` rather than a typed error. Rich
queryable reasons should be added later through either `Result<T, E>`, a separate
typed error result, or a component error customization point. That core API
decision should be made once for Solar rather than invented only for services.

### 23.10 Kconfig and type-policy precedence

Zephyr Kconfig supplies firmware-wide defaults, including service stop timeout
and forced-abort behavior. A service execution policy may override an explicitly
overridable default.

The precedence rule is:

```text
explicit component/execution policy
    overrides Kconfig default
```

Kconfig may also disable a capability globally. A local type policy cannot
re-enable functionality that was excluded from the build or forbidden by a
global safety setting. The specific options and override traits must document
which values are defaults and which are hard gates.

Solar does not provide non-Zephyr fallback configuration. Its public headers
expect Zephyr's generated Kconfig definitions because Solar is a Zephyr module.

## 24. Reference Principles

Keep these principles beside the code during the reform:

1. A component type represents one logical architectural identity.
2. Configuration and identity belong in types; changing operational data belongs
   in static state or static type-owned objects.
3. Solar's `System` is itself a static type and directly owns system coordination.
4. Components use one another through normal includes and direct typed APIs.
5. Dependency declarations describe lifecycle requirements, not object access.
6. Solar owns lifecycle truth, execution infrastructure, reports, and graph
   validation.
7. Components own domain behavior and internal state.
8. Services are unique static capabilities; tasks represent repeatable work.
9. Behavior and execution policy should be separate.
10. Inspection, Health, Supervisor, and Remote consume focused canonical facts.
11. Zephyr remains the operating foundation; Solar should add typed orchestration
    and ergonomics rather than duplicate Zephyr.
12. The first implementation should be truthful and small before it becomes
    clever.

The desired end state can be summarized by this code:

```cpp
#include "app/system.hpp"

using Robot = app::System;

Robot::boot();

app::LeftMotor::set_speed(40);

const auto lifecycle = Robot::lifecycle::components();
const auto remote = Robot::lifecycle::record<app::RemoteService>();
const auto threads = Robot::kernel::service_threads();
```

The application feels like one coherent static firmware system. Components are
directly usable by their domain names. Solar quietly provides the coordination
that only the whole system can provide.
