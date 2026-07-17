# Solar Integrated Architecture

Status: accepted

This specification closes the design pass by proving that Specs 00 through 13
form one implementable Zephyr-native architecture. It also fixes the final
cross-specification clarifications discovered during audit.

Detailed implementation planning lives under
`development-docs/implementation-planning/`.

## 1. Final Architectural Statement

Solar is a static C++23 firmware framework and Zephyr module for one integrated
firmware system.

- The user declares one `Blueprint` and one `System<Blueprint>` type.
- One application binding selects that system for global Solar APIs.
- The binding creates no runtime object.
- Components are static types with optional static lifecycle hooks.
- Device, Facility, Service, and Executor are the component categories.
- Tasks, subscriptions, checks, routes, and subsystem declarations are leaves.
- Components include direct application dependencies normally; Solar does not
  provide a service locator or system context.
- Ordinary component headers never include the application composition root.
- Runtime state is owned by focused type-owned subsystem storage, not one
  monolithic runtime object.
- Kconfig controls build capability, hard ceilings, integration, and defaults.
- C++ types control application membership, architecture, local policy, and
  static identity.
- Zephyr remains canonical for scheduling, drivers, devicetree, workqueues,
  logging integration, and platform capability.
- Solar adds typed C++ ergonomics, compile-time composition, bounded ownership,
  focused records, and system-level coordination.

## 2. Final Binding Model

Solar supports one canonical architecture with two build-wide frontend modes.

### 2.1 Relaxed mode

Relaxed catalog binding is the default:

```text
CONFIG_SOLAR_STRICT_CATALOG_BINDING=n
```

Bound global operations may appear in ordinary inline component methods without
composition-root visibility. During `solar::boot()`, effective catalogs connect
each registered descriptor frontend to the selected system's canonical
facility, storage, and policy.

Relaxed errors are explicit:

- before frontend binding: `NotReady`;
- subsystem disabled: `Disabled`;
- descriptor absent from effective catalog: `NotRegistered`.

Relaxed frontends do not own state, discover call sites, or register types.

### 2.2 Strict mode

Strict catalog binding is selected with:

```text
CONFIG_SOLAR_STRICT_CATALOG_BINDING=y
```

The selected system and catalog membership resolve at compile time. Strict
bound definitions normally live out of line in a `.cpp` that includes the
composition root. Missing membership is a compile-time error and there is no
runtime frontend dispatch cost.

### 2.3 Shared invariants

Both modes use the same public operation names, result types, catalogs,
configuration, canonical state, synchronization, lifecycle, and inspection.
There is no parallel relaxed storage architecture.

## 3. Representative Project Layout

```text
firmware/
  include/
    app/
      system.hpp
    board/
      hardware.hpp
    devices/
      imu.hpp
      motors.hpp
    services/
      navigation.hpp
    execution/
      control_work.hpp
    parameters/
      drive.hpp
    remote/
      robot_remote.hpp
  src/
    main.cpp
    devices/
      imu.cpp
      motors.cpp
    services/
      navigation.cpp
```

The include direction is:

```text
Solar + direct dependency headers
    -> component headers
    -> app/system.hpp
    -> strict source definitions and main.cpp
```

Forbidden:

```text
component header -> app/system.hpp
Solar header -> firmware System type
```

Relaxed source definitions may include only their own component and dependency
headers. Strict bound definitions include `app/system.hpp` from the source file.

## 4. Representative Hardware Aliases

```cpp
// board/hardware.hpp
#pragma once

#include <solar/hardware.hpp>

namespace board
{

using StatusLed = solar::hardware::gpio::Output<
    solar::hardware::dt::alias<"status-led">>;

using ImuBus = solar::hardware::spi::Endpoint<
    solar::hardware::dt::alias<"imu">>;

using ImuReady = solar::hardware::gpio::Interrupt<
    solar::hardware::dt::alias<"imu-ready">,
    solar::hardware::gpio::Edge::Rising>;

using DebugUart = solar::hardware::uart::Async<
    solar::hardware::dt::alias<"debug-uart">>;

} // namespace board
```

These types are Hardware endpoints, not lifecycle components. The application
`Imu` type becomes a Device because it adds application meaning, lifecycle,
health, and contributions.

## 5. Representative Declarations

### 5.1 Parameter

```cpp
struct DriveKp
{
    using Value = float;

    static constexpr solar::parameters::Descriptor descriptor{
        .name = "drive.pid.kp",
        .description = "Drive velocity proportional gain",
        .units = "",
    };

    static constexpr Value default_value = 1.2f;

    using Validation = solar::parameters::Range<
        0.0f,
        10.0f,
        solar::parameters::Reject>;
};
```

### 5.2 Message, event, and metric

```cpp
struct ImuSample
{
    static constexpr solar::bus::Descriptor descriptor{
        .name = "imu.sample",
    };

    std::array<float, 3> acceleration;
    std::array<float, 3> angular_rate;
};

struct ImuConnectionLost
{
    static constexpr solar::events::Descriptor descriptor{
        .name = "imu.connection_lost",
    };

    using Payload = solar::events::NoPayload;
};

struct ImuSamples
{
    static constexpr solar::metrics::Descriptor descriptor{
        .name = "imu.samples",
    };

    using Instrument = solar::metrics::Counter<std::uint64_t>;
};
```

Exact descriptor field helpers remain owned by their subsystem specs. The
important integrated rule is that authored declarations are enriched into
`CatalogEntry` facts rather than modified with system ownership metadata.

## 6. Representative Device

```cpp
// devices/imu.hpp
#pragma once

#include "board/hardware.hpp"
#include "messages/imu.hpp"
#include "observability/imu.hpp"
#include "remote/imu.hpp"

#include <solar/bus.hpp>
#include <solar/events.hpp>
#include <solar/health.hpp>
#include <solar/metrics.hpp>
#include <solar/remote.hpp>
#include <solar/result.hpp>

struct Imu
{
    static constexpr solar::component::Descriptor descriptor{
        .name = "imu",
    };

    using Messages = solar::bus::Messages<ImuSample>;
    using Events = solar::events::Events<ImuConnectionLost>;
    using Metrics = solar::metrics::Metrics<ImuSamples>;
    using RemoteData = solar::remote::ContributeData<ImuTelemetry>;
    using RemoteActions = solar::remote::ContributeActions<CalibrateImu>;

    struct Health
    {
        struct Connection
        {
            static solar::Result<solar::health::Observation> check();
        };

        using Checks = solar::health::Checks<
            Connection,
            solar::health::Progress<500ms>>;
    };

    static solar::Result<void> init();
    static solar::Result<void> start();
    static solar::Result<void> stop();
    static solar::Result<void> deinit();
};
```

The Device names its own semantic contributions. It does not define a combined
`Contributions` alias, inherit a Solar base, accept a context, or include the
application System.

In relaxed mode an ordinary implementation may remain compact:

```cpp
solar::Result<void> Imu::publish(ImuSample sample)
{
    return solar::metrics::inc<ImuSamples>()
        .and_then([&] { return solar::bus::emit(sample); });
}
```

## 7. Representative Service And Execution

```cpp
// services/navigation.hpp
#pragma once

#include "devices/imu.hpp"
#include "devices/motors.hpp"
#include "parameters/drive.hpp"

#include <solar/execution.hpp>
#include <solar/service.hpp>

struct Navigation
{
    static constexpr solar::component::Descriptor descriptor{
        .name = "navigation",
    };

    using Dependencies = solar::Dependencies<Imu, LeftMotor, RightMotor>;

    using Execution = solar::execution::Service<
        solar::execution::StackSize<4096>,
        solar::execution::Priority<4>,
        solar::execution::StopTimeout<100ms>>;

    using Parameters = solar::parameters::Parameters<DriveKp>;

    static solar::Result<void> init();
    static solar::Result<void> start();
    static solar::Result<void> run(solar::kernel::StopToken stop);
    static solar::Result<void> stop();
    static solar::Result<void> deinit();
};
```

The service is a static type. Solar owns at most one service execution for this
component identity. Multiple repeated jobs belong to an Executor, not multiple
instances of the Service.

Shared execution remains explicit:

```cpp
using ControlWorkQueue = solar::execution::WorkQueue<
    "control-work",
    solar::execution::StackSize<3072>,
    solar::execution::Priority<5>>;

using PublishTelemetry = solar::execution::Periodic<
    "publish-telemetry",
    TelemetryBehavior,
    50ms,
    ControlWorkQueue>;
```

No executor is hidden. A Kconfig default may select Zephyr's system workqueue
when a registration omits a target.

## 8. Representative Composition Root

```cpp
// app/system.hpp
#pragma once

#include "devices/imu.hpp"
#include "devices/motors.hpp"
#include "execution/control_work.hpp"
#include "remote/robot_remote.hpp"
#include "services/navigation.hpp"

#include <solar/system.hpp>

namespace app
{

using RobotBlueprint = solar::Blueprint<
    solar::Devices<
        LeftMotor,
        RightMotor,
        Imu>,

    solar::Services<
        Navigation>,

    solar::Executors<
        ControlWorkQueue>,

    solar::Execution<
        PublishTelemetry>,

    solar::Bus<
        ControlSubscriptions,
        TelemetrySubscriptions>,

    solar::parameters::Configuration<
        solar::parameters::InvalidWrite<solar::parameters::Reject>>,

    solar::events::Configuration<
        solar::events::HistoryCapacity<32>,
        solar::events::Overflow<solar::events::DropOldest>>,

    solar::metrics::Configuration<
        solar::metrics::DefaultConcurrency<
            solar::metrics::concurrency::Automatic>>,

    solar::remote::Configuration<
        solar::remote::Engine<solar::remote::DedicatedService>,
        solar::remote::DefaultActionExecution<ControlWorkQueue>>>;

using RobotSystem = solar::System<RobotBlueprint>;

} // namespace app

SOLAR_BIND_SYSTEM(app::RobotSystem);
```

Logging, Health, and Inspection are selected by Kconfig and need not be listed.
Bus, Parameters, Events, Metrics, and Remote are demand-derived from effective
catalog/configuration use. Remote links require the Remote facility and service.
Supervisor requires Health.

## 9. Application Entry

```cpp
#include "app/system.hpp"

int main()
{
    auto boot = solar::boot();
    if (!boot) {
        return -1;
    }

    return 0;
}
```

Zephyr owns application entry and scheduling. Solar performs one explicit
system boot. There is no entry Profile and no system object parameter.

## 10. Effective Catalog Derivation

For the representative system, normalization performs:

1. classify root Blueprint sections;
2. collect component types from Devices, Services, and Executors;
3. validate unique component identity and category;
4. collect `Dependencies<...>` and validate the DAG;
5. invoke each subsystem's generic `contribution_source<Tag, Component>`;
6. collect conventional aliases such as `Messages`, `Parameters`, `Events`,
   `Metrics`, Remote aliases, Tasks, and Health Checks;
7. enrich authored declarations into provenance-preserving `CatalogEntry`
   values;
8. merge explicit root declarations;
9. reject duplicate ownership, conflicting stable IDs, malformed aliases, and
   invalid policies;
10. apply declaration, blueprint, then Kconfig policy precedence;
11. derive built-in inclusion from the three inclusion classes;
12. produce immutable effective component, subsystem, execution, and exposure
   catalogs;
13. derive static storage capacities and runtime owners;
14. expose focused graph/catalog descriptors for compile-time queries,
   Inspection, Remote, and diagnostics.

No runtime scan, linker section registration, or source-body discovery is
required.

## 11. Runtime Ownership

| Domain | Canonical owner | Representative static resources |
| --- | --- | --- |
| system/lifecycle | bound `System` lifecycle state | system state, per-component records, bounded boot/stop reports, lifecycle mutex |
| Kernel | each wrapper/user | native Zephyr object and static backing storage where required |
| service execution | service execution owner | thread object, stack, stop source, execution record |
| executor | executor component or Zephyr system workqueue | queue/thread/stack where owned, registration records |
| Bus | Bus facility and route architecture | route metadata, bounded queues/latest slots, accounting |
| Parameters | Parameters facility | one typed slot per effective parameter, hooks, persistence state |
| Events | Events facility | bounded ingress, history, sequence, accounting |
| Metrics | Metrics facility | one catalog-derived typed storage object per metric |
| Logging | early Logging core | canonical record ring, reserved capacity, history, sink admission/accounting |
| Remote | Remote facility/service/link types | protocol state, sessions, queues, buffers, stream credits, link operations |
| Inspection | Inspection facility | collection metadata and bounded query/page state only |
| Health | Health facility | subject/check/monitor records and bounded evidence references |
| Supervisor | Supervisor service | service thread, policy state, response history, watchdog coordination |
| Hardware | each endpoint wrapper or operation owner | Zephyr specs/handles, static callback and operation state where required |

No subsystem stores a second canonical copy of another subsystem's truth.

## 12. Thread And Stack Ownership

Threads are explicit and statically attributable:

- Zephyr main thread: application entry and boot caller;
- one thread per declared Solar Service using dedicated execution;
- one thread per explicitly owned WorkQueue executor;
- Zephyr system workqueue only when selected by registrations or subsystem
  configuration;
- Remote dedicated service only when configured and derived;
- Supervisor service only when enabled;
- no inherent dedicated thread for Bus, Parameters, Events, Metrics, Logging,
  Inspection, Health, or Hardware;
- no hidden default worker.

Subsystem deferred work names an Executor. Stack ownership is visible in
Execution and focused Kernel records.

## 13. Normal Boot Trace

1. `solar::boot()` resolves the one bound System type.
2. Lifecycle serializes entry and validates Dormant state.
3. Effective catalogs and static ownership are already compile-time facts.
4. Relaxed descriptor frontends are connected to canonical subsystem operation
   tables before component hooks can call them.
5. Lifecycle and execution records initialize.
6. Logging early ingress is already available when selected.
7. System transitions to `Initializing`.
8. Component `init()` hooks run in topological order.
9. System transitions to `Starting`.
10. Component `start()` hooks run in topological order.
11. Service threads and executor registrations are prepared behind the
    activation barrier.
12. Successfully admitted components become Running.
13. System commits to Running.
14. The activation barrier releases service `run()` bodies and task admission.
15. The completed BootReport is published and returned.

User concurrent work cannot observe a partially committed system.

## 14. Failure And Shutdown Traces

### 14.1 Partial boot failure

If a hook, storage adapter, service preparation, or executor admission fails:

1. attribute the focused error to the component/facility and operation;
2. do not release the activation barrier;
3. stop any prepared execution that requires cleanup;
4. invoke stop/deinit rollback in reverse successful order;
5. preserve bounded rollback failures in the BootReport;
6. leave the system in Failed state;
7. reject another boot under the initial reboot policy.

### 14.2 Normal stop

1. serialize stop and transition to Stopping;
2. close task, action, stream, and service admission;
3. request cooperative service/executor stop;
4. apply finite timeout and Kconfig/type policy;
5. stop components in reverse topological order;
6. drain Logging through its bounded shutdown contract;
7. deinitialize components in reverse order;
8. publish the StopReport and final state.

### 14.3 Disconnected Remote

A disconnected or slow Remote link:

- changes only link/session state;
- does not block subsystem producers;
- does not own or invalidate canonical subsystem state;
- applies bounded queue, credit, overflow, and reconnect policy;
- preserves focused loss/accounting records;
- cannot prevent lifecycle stop;
- cannot make Logging depend on Remote.

## 15. Cross-Subsystem Dependency Audit

The accepted direction is:

```text
Zephyr -> Kernel / Hardware
Core catalogs -> System / facilities
System + Kernel -> Lifecycle -> Execution
Execution -> deferred subsystem processing
Subsystem canonical state -> leaf adapters -> Remote / Inspection / Health
Health -> Supervisor policy
```

Resolved cycle risks:

- Logging/Remote: Remote log adapter is a leaf; Logging core never depends on
  Remote.
- Lifecycle/Execution: prepare/activate/stop protocol with separate records.
- Events/Logging and Events/Metrics: one-way adapters own the relationship.
- Health/Supervisor: passive assessment and active response are separate;
  Supervisor self-reporting cannot recursively trigger itself.
- Hardware/Device: hardware endpoints remain independent; application Devices
  consume them.
- component/System includes: only composition root sees all components.

The full dependency map is in
`implementation-planning/02-dependency-map.md`.

## 16. ISR Surface Audit

Canonical ISR naming is `try_<verb>_isr`, with source-qualified variants such
as `try_observe_isr_from`.

| Subsystem | ISR-capable examples | Thread-only examples |
| --- | --- | --- |
| Kernel | semaphore give, queue try put/get where native-safe, event operations, interrupt state | waiting, mutex locking, join, workqueue drain |
| Bus | `try_emit_isr` for fully compatible routes | blocking/deferred configuration and route inspection |
| Parameters | only declaration policies explicitly supporting atomic ISR access | mutex-backed get/set, persistence, change hooks unless deferred through ISR ingress |
| Events | `try_observe_isr`, compact bounded ingress | formatting, history paging, processors |
| Metrics | `try_inc_isr`, `try_add_isr`, `try_set_isr`, `try_observe_isr` where policy permits | mutex/external reducers and broad queries |
| Logging | `try_<level>_isr` bounded argument capture | formatting, sinks, flush |
| Execution | `try_submit_isr` for compatible registrations | cancellation waits, drain, stop coordination |
| Remote | bounded push/receipt APIs explicitly marked ISR-safe | decode, sessions, Actions, streams, transport coordination |
| Health | progress marker and compact `try_report_isr_from` | assessment hooks, response policy |
| Hardware | driver-specific callback/ISR methods supported by Zephyr | ordinary blocking transfers, configuration, readiness waits |

No ISR API allocates, waits, locks a mutex, formats text, invokes sinks, or
runs arbitrary application recovery.

## 17. Kconfig Architecture

Kconfig is hierarchical and Zephyr-native. Exact symbols are implemented by
their owning stages, but the categories are fixed:

### 17.1 Foundation

- Solar module capability;
- strict versus relaxed catalog binding;
- descriptor strings and diagnostic detail defaults;
- dynamic allocation capability, default disabled;
- lifecycle report ceilings and stop/reboot defaults.

### 17.2 Kernel and execution

- focused diagnostics and native runtime statistics;
- service stop timeout and forced abort default;
- system workqueue default target and required Zephyr workqueue options;
- stack/runtime monitor capability.

### 17.3 Subsystems

- Bus, Parameters, Events, Metrics, Logging, Remote, Inspection, Health, and
  Supervisor capabilities;
- hard byte/count ceilings;
- optional accounting, timestamps, histories, strings, and adapters;
- Logging level, Zephyr frontend, reserved storage, and panic integration;
- Remote frame/session/stream ceilings and protocol capabilities.

### 17.4 Hardware

- generator capability;
- wrapper families enabled by build/platform capability;
- driver API dependencies;
- no Kconfig selection of application C++ endpoint types.

Policy precedence remains:

```text
declaration-specific policy
    > blueprint subsystem configuration
    > Kconfig default
```

Kconfig hard ceilings and disabled capabilities cannot be overridden by C++
policy.

### 17.5 Required Zephyr capability inventory

The workspace Zephyr 4.4 source establishes this initial dependency inventory.
Owning implementation stages must confirm exact `depends on`, `select`, and
unavailable behavior rather than selecting every optional feature globally.

| Solar capability | Zephyr symbols used or conditionally required |
| --- | --- |
| C++23 core | `CONFIG_CPP`, `CONFIG_STD_CPP23`, `CONFIG_REQUIRES_FULL_LIBCPP` |
| thread naming and basic records | `CONFIG_THREAD_NAME`; thread APIs from the kernel baseline |
| stack diagnostics | `CONFIG_THREAD_STACK_INFO`, optionally `CONFIG_THREAD_MONITOR` |
| runtime usage diagnostics | `CONFIG_THREAD_RUNTIME_STATS`, `CONFIG_SCHED_THREAD_USAGE` where supported |
| poll wrappers and poll-driven services | `CONFIG_POLL` |
| event wrappers | `CONFIG_EVENTS` |
| workqueue timeout/diagnostics | `CONFIG_WORKQUEUE_WORK_TIMEOUT` and associated timeout defaults when that feature is selected |
| Zephyr log capture | `CONFIG_LOG`, `CONFIG_LOG_FRONTEND`, optionally `CONFIG_LOG_PRINTK` |
| parameter persistence | `CONFIG_SETTINGS` when a settings adapter is selected |
| GPIO endpoints | `CONFIG_GPIO` |
| SPI endpoints | `CONFIG_SPI` |
| I2C endpoints | `CONFIG_I2C` |
| UART endpoints | Zephyr serial capability and `CONFIG_UART_ASYNC_API` for async links |
| ADC endpoints | `CONFIG_ADC` |
| PWM endpoints | `CONFIG_PWM` |
| counter endpoints | `CONFIG_COUNTER` |
| watchdog provider | `CONFIG_WATCHDOG` |
| optional RTIO adapters | `CONFIG_RTIO` only when an accepted endpoint/operation selects RTIO |
| controlled platform reboot, later policy | `CONFIG_REBOOT` only when that policy is implemented |

Driver-family capability remains board-dependent. Solar must not force-enable a
driver or diagnostic feature merely because its wrapper header exists.

## 18. Zephyr-Native Audit

The integrated design works with Zephyr rather than replacing it:

- `k_thread`, static stacks, and Zephyr scheduling remain native;
- `k_work`, delayable work, owned workqueues, and the system workqueue are
  first-class Kernel/Execution mechanisms;
- poll, queues, semaphores, mutexes, timers, slabs, and diagnostics map directly
  to Zephyr facilities;
- devicetree remains canonical for hardware existence and configuration;
- Zephyr driver APIs own normal DMA use;
- generated hardware ergonomics read Zephyr's `edt.pickle` through Zephyr's
  Python libraries;
- Zephyr logging is captured through a supported frontend rather than replaced
  at the kernel boundary;
- Kconfig owns module capabilities and platform defaults;
- native simulation is the primary system test environment;
- native handles remain available where Solar cannot responsibly abstract a
  platform-specific feature.

Solar's deliberate custom Logging stream is the accepted exception because
Remote-first structured diagnostics, bounded history, and unified event/log
correlation require a different canonical record architecture.

## 19. Compile-Time And Resource Risks

### 19.1 Template cost

Risks include large effective catalogs, repeated normalization, verbose
constraint failures, and broad aggregate headers. Implementation must:

- normalize once per System and alias results;
- use focused concepts and diagnostic tokens;
- avoid recursive algorithms where folds/index sequences are clearer;
- keep public subsystem headers focused;
- add representative compile-time/build-time measurements;
- test with and without LTO;
- avoid including all of Solar through `solar/solar.hpp` in normal component
  headers.

### 19.2 Relaxed dispatch

Relaxed operations may add readiness check, pointer load, and indirect dispatch.
This is accepted initially. Hot Metrics and ISR paths are benchmarked. Strict
mode removes binding dispatch, and typed cached handles may be considered only
after measurement demonstrates need.

### 19.3 Static storage

All core storage is bounded and catalog/Kconfig derived. Optional facilities
must disappear when their inclusion rule excludes them. The implementation
records static storage and binary deltas rather than claiming zero cost without
evidence.

### 19.4 Threads and stacks

No facility gains a thread merely for convenience. Service and owned executor
stacks are explicit. System workqueue use is visible. Supervisor and Remote
threads exist only when their configured service form is included.

## 20. Platform Practicality

### 20.1 Native simulation

Native simulation supports lifecycle, execution, synchronization, protocol,
most concurrency, generated devicetree fixtures, and fake hardware provider
tests. It is the primary CI platform.

### 20.2 Teensy 4.0

Teensy 4.0 is the primary firmware compile target and hardware validation
platform. Physical tests are required for driver and interrupt behavior that
native simulation cannot prove. Unsupported devicetree/driver capabilities are
reported as unavailable rather than emulated by Solar.

### 20.3 Toolchain

Solar requires the workspace's C++23-capable Zephyr toolchain and full standard
library support. Multi-release Zephyr compatibility is not an initial promise;
the pinned Zephyr 4.4 workspace is canonical for the reform.

## 21. Migration Decision

The migration is intentionally hard:

- no compatibility API;
- no deprecated aliases;
- no parallel old runtime;
- no in-tree archive;
- old tests are deleted or rewritten by their replacement stage;
- firmware migrates at named integration checkpoints;
- intermediate active stages may be red;
- every stage closes green for its owned scope.

Current implementation disposition is recorded in
`implementation-planning/01-repository-inventory.md`.

## 22. Implementation Program

The required workflow and gates are defined in:

- `implementation-planning/03-stage-workflow-and-verification.md`;
- `implementation-planning/landed-summary-template.md`.

The staged roadmap is defined in:

- `implementation-planning/04-implementation-roadmap.md`.

The first stage resets Solar to a minimal buildable C++23 Zephyr module and
removes code already superseded by the accepted architecture. It does not leave
the repository intentionally broken at its close.

## 23. Audit Corrections Applied

The integrated audit made these clarifications normative across earlier specs:

1. Board and Peripheral are not component categories; raw hardware belongs to
   Hardware and application abstractions deliberately become Devices.
2. Service threads and executor jobs remain behind a final activation barrier
   until the system commits to Running.
3. Logging core is early Kconfig-selected infrastructure; transport and Remote
   sinks are leaf adapters, eliminating a lifecycle cycle.
4. Built-ins use demand-derived, Kconfig-selected, or required-derived
   inclusion.
5. Component dependencies use `solar::Dependencies<...>` consistently.
6. Logging uses `solar::log` consistently.
7. ISR APIs use `try_<verb>_isr` consistently.
8. Broad subsystem snapshots and stale Inspection examples are removed.
9. Relaxed catalog binding is the ergonomic default; strict binding remains a
   zero-dispatch compile-time option.

## 24. Final Design Gate

The design pass is ready to close when the user accepts this integrated
architecture and the linked planning package.

The architecture now has:

- one static composition model;
- one identity/contribution system;
- one lifecycle and execution boundary;
- explicit canonical ownership for every runtime domain;
- bounded concurrency and ISR contracts;
- Zephyr-native kernel and hardware layers;
- no unresolved cross-subsystem ownership cycle;
- a hard-migration inventory;
- a dependency-derived implementation roadmap;
- defined green checkpoints and documentation handoff.

No blocking design question remains before Stage 00 implementation.
