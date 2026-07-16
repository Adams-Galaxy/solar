# System Blueprint And Global Binding

Date: 2026-07-13

Status: accepted design

Owning phase: Phase 1

Depends on:

- `00-design-conventions.md`
- `00a-modern-cpp-result-and-status.md`

## 1. Purpose

This specification defines how one Solar firmware image declares its complete
compile-time architecture and how global Solar APIs discover that architecture.

It establishes:

- one non-positional `Blueprint<...>` declaration;
- one static `System<Blueprint>` coordinating type;
- extensible tagged blueprint sections;
- normalization into one validated effective blueprint;
- demand-driven inclusion of Solar built-in facilities;
- one application binding selected through a trait specialization;
- global APIs such as `solar::boot()` without a system object;
- explicit alternate application bindings for tests;
- strict include direction that prevents circular project dependencies;
- separate catalog and subsystem-configuration sections.

The central promise is:

> Solar knows one complete firmware architecture at compile time, while
> application components remain ordinary static C++ types that never receive a
> system object, context object, or dependency locator.

## 2. Non-Goals

This specification does not define:

- final component descriptor fields or stable external identifiers;
- the final contribution collection protocol;
- detailed lifecycle hook semantics;
- individual parameter, bus, event, metric, logging, or Remote APIs;
- executor scheduling and queue behavior;
- dynamic runtime registration;
- multiple independently running Solar systems in one firmware image;
- dependency access through `System`;
- automatic project-header injection by CMake or Kconfig;
- a header-only requirement for application component implementations.

Those subjects belong to later subsystem specifications. This document fixes
the composition and binding boundaries they must use.

## 3. Architectural Decisions

### 3.1 One blueprint, one system type

`System` accepts one blueprint type:

```cpp
using RobotSystem = solar::System<RobotBlueprint>;
```

It does not accept an expanding positional list of device, service, task,
channel, runtime, and policy arguments.

The blueprint is the declarative input. `System` validates and coordinates the
effective result. Neither is instantiated by application code.

### 3.2 One default application binding

One trait specialization selects the system used by no-argument global Solar
APIs:

```cpp
SOLAR_BIND_SYSTEM(app::RobotSystem);
```

The binding selects a type. It creates no runtime object, storage aggregate,
registry, initialization side effect, or dependency access mechanism.

### 3.3 Static does not mean monolithic storage

`System` is the real static coordinating type. Runtime state remains distributed
among its responsible types:

- components own component-local state;
- facilities own subsystem state;
- executors own execution machinery;
- lifecycle owns lifecycle records and reports;
- individual subsystem stores own their bounded data.

The implementation must not hide an old object-oriented system container behind
the static API.

### 3.4 Components do not include the composition root

Ordinary component headers include only:

- Solar headers needed by their declarations;
- application headers for direct typed dependencies.

They never include the application composition root. The root includes the
component declarations, not the reverse.

### 3.5 Bound behavior is compiled after binding visibility

Application method definitions that instantiate bound global Solar APIs are
normally out of line in a source file that includes the completed composition
root.

This is ordinary C++ include discipline. It does not pass a system type or
object into the component.

### 3.6 Catalog membership and configuration are separate sections

A subsystem catalog contains declarations. A subsystem configuration section
contains recognized policy overrides. They are sibling blueprint sections and
must not be mixed in one template argument pool.

## 4. Public Declaration Shape

### 4.1 Realistic application

```cpp
// app/system.hpp
#pragma once

#include "devices/imu.hpp"
#include "devices/motors.hpp"
#include "execution/control_worker.hpp"
#include "parameters/drive_parameters.hpp"
#include "services/navigation.hpp"
#include "services/remote_transport.hpp"

#include <solar/system.hpp>

namespace app
{

using RobotBlueprint = solar::Blueprint<
    solar::Devices<
        LeftMotor,
        RightMotor,
        Imu>,

    solar::Facilities<
        CalibrationStore>,

    solar::Services<
        Navigation,
        RemoteTransport>,

    solar::Executors<
        ControlWorker>,

    solar::Execution<
        ReadControls,
        UpdateOdometry>,

    solar::Bus<
        EmergencyStopSubscriptions,
        ControlSubscriptions>,

    solar::Parameters<
        DriveKp,
        DriveKi,
        DriveKd>,

    solar::parameters::Configuration<
        solar::parameters::Persistence<SettingsPartition>,
        solar::parameters::InvalidWrite<solar::parameters::Reject>>,

    solar::Events<
        ControlDeadlineMissed,
        RemoteFrameDropped>,

    solar::events::Configuration<
        solar::events::HistoryCapacity<32>,
        solar::events::Overflow<solar::events::DropOldest>>,

    solar::Metrics<ControlLoopTime, RemoteFramesDropped>,

    solar::metrics::Configuration<
        solar::metrics::DefaultConcurrency<
            solar::metrics::concurrency::Automatic>>,

    solar::remote::Configuration<
        solar::remote::Engine<solar::remote::DedicatedService>,
        solar::remote::DefaultActionExecution<ControlWorker>>>;

using RobotSystem = solar::System<RobotBlueprint>;

} // namespace app

SOLAR_BIND_SYSTEM(app::RobotSystem);
```

Names inside subsystem sections remain illustrative until their owning specs
are accepted. The outer blueprint and binding relationships are accepted here.

### 4.2 Application entry

```cpp
#include "app/system.hpp"

int main()
{
    auto report = solar::boot();
    if (!report)
    {
        return -1;
    }

    return 0;
}
```

The explicitly named system remains available:

```cpp
static_assert(app::RobotSystem::valid);

auto report = app::RobotSystem::boot();
auto components = app::RobotSystem::catalog::components();
```

Global and explicit access address the same static system state.

## 5. Blueprint Section Model

### 5.1 Sections are tagged, not positional

`Blueprint<...>` accepts recognized section types in any order. A section is
classified by a Solar-owned trait or equivalent constrained protocol.

Conceptually:

```cpp
template<typename Section>
struct section_traits;

template<typename... Types>
struct section_traits<solar::Devices<Types...>>
{
    using domain = solar::domains::components;
    using key = solar::section_keys::devices;
    static constexpr auto role = solar::section_role::catalog;
};
```

The exact internal names may change. The semantic requirements are:

- every section has an unambiguous key;
- every section declares its role;
- section order has no meaning;
- duplicate singular keys are rejected;
- unknown section types produce a focused diagnostic;
- extension does not require adding a new positional `System` parameter.

### 5.2 Section roles

Initial section roles are:

- component catalog;
- subsystem catalog;
- subsystem configuration;
- execution registration;
- explicit built-in selection or override.

Later specifications may define additional roles through the same extension
protocol.

### 5.3 Cardinality

The user blueprint permits:

| Section | User cardinality | Meaning |
| --- | --- | --- |
| `Devices<...>` | zero or one section | device components |
| `Facilities<...>` | zero or one section | application or explicitly selected facilities |
| `Services<...>` | zero or one section | sustained active components |
| `Executors<...>` | zero or one section | execution infrastructure components |
| `Execution<...>` | zero or one section | leaf execution registrations |
| subsystem catalog | zero or one per subsystem key | declarations or contribution roots |
| subsystem configuration | zero or one per subsystem key | policy overrides |

An omitted collection section is equivalent to an empty section. Users should
not need to spell empty sections.

### 5.4 Hardware representation

Boards, devicetree nodes, and raw hardware endpoints are described by Zephyr
and Solar Hardware. They are not blueprint sections or lifecycle components.
Application types built over those endpoints may be declared in
`Devices<...>` when they need lifecycle, dependency, health, or contribution
participation.

### 5.5 Retained component categories

The effective component graph recognizes:

- device;
- facility;
- service;
- executor.

These categories describe role. They do not require inheritance or object
instances.

### 5.6 Tasks are not components by default

Task behavior enters through execution registrations. A task is a leaf bound to
an executor, trigger, policy, and callable behavior. It does not automatically
receive component identity or lifecycle.

An executor is a component because it owns execution infrastructure and may
participate in lifecycle. A task registration is not.

### 5.7 Channels are removed

`Channels<...>` is not part of the target blueprint. Typed bus subscriptions,
topics, or message declarations will be defined by the bus specification.

Migration from current channel APIs must follow the Phase 4 classification:
fan-out behavior moves to the bus, while point-to-point storage moves to a typed
kernel queue, direct call, or component-owned state. Graph-owned channel objects
are not preserved.

## 6. Subsystem Catalog And Configuration Sections

### 6.1 Separate sibling sections

Catalog entries and configuration are declared separately:

```cpp
solar::Parameters<DriveKp, DriveKi, DriveKd>,

solar::parameters::Configuration<
    solar::parameters::Persistence<SettingsPartition>,
    solar::parameters::InvalidWrite<solar::parameters::Reject>>
```

The catalog section answers:

> What declarations belong to this firmware system?

The configuration section answers:

> Which subsystem-wide policy overrides apply to them?

### 6.2 Configuration is optional

If configuration is absent, the subsystem uses declaration-specific policy and
Kconfig defaults:

```cpp
solar::Parameters<DriveKp, DriveKi>
```

Policy precedence is:

```text
declaration-specific policy
    > blueprint subsystem configuration
    > Kconfig default
```

### 6.3 Configuration is constrained

A subsystem `Configuration<...>` may contain only policy types recognized by
that subsystem. It is a structuring section, not an untyped property bag.

Validation must reject:

- policies belonging to another subsystem;
- duplicate policies for one exclusive policy axis;
- contradictory policies;
- unsupported combinations;
- local attempts to enable a capability disabled by Kconfig.

### 6.4 Why there is no global configuration pool

This design does not require:

```cpp
solar::Configuration<
    solar::parameters::Configuration<...>,
    solar::events::Configuration<...>>
```

`Blueprint` already provides the heterogeneous outer structure. Direct sibling
sections are flatter, preserve subsystem ownership, and avoid another generic
wrapper.

### 6.5 Why configuration is not mixed with entries

This form is rejected:

```cpp
solar::Parameters<
    DriveKp,
    DriveKi,
    solar::parameters::Configuration<...>>
```

It makes one argument list represent both catalog membership and policy,
weakens diagnostics, and forces catalog algorithms to separate unrelated
roles.

## 7. Blueprint Normalization

### 7.1 User and effective blueprints

The user blueprint is the declaration written by the application. Solar
normalizes it into an effective blueprint used for validation and runtime
coordination.

Conceptually:

```cpp
using Effective = solar::effective_blueprint_t<RobotBlueprint>;
```

This alias may remain internal, but focused inspection must expose the effects
of normalization.

### 7.2 Normalization pipeline

Normalization proceeds in a deterministic compile-time sequence:

1. Classify every user section.
2. Reject unknown or duplicate singular sections.
3. Supply empty/default forms for omitted optional sections.
4. Read Kconfig capability availability and defaults.
5. Collect application components and explicit subsystem declarations.
6. Derive required Solar facilities and default infrastructure.
7. Check that every derived requirement is enabled.
8. Apply subsystem configuration and narrow policy precedence.
9. Collect contribution catalogs while preserving origin.
10. Validate identities, registrations, dependencies, and feature combinations.
11. Build the component dependency graph.
12. Produce the immutable effective blueprint and catalog types.

Later specifications may refine ordering where their validation depends on a
new catalog, but they must preserve deterministic finite normalization.

### 7.3 Finite derived inclusion

Solar-provided facilities and infrastructure may declare additional Solar
requirements. Normalization computes the finite closure of those requirements.

It must detect and diagnose:

- impossible capability combinations;
- contradictory derived policies;
- dependency cycles;
- duplicate effective components;
- failure to reach a stable finite result.

Application component dependencies are never silently added. If `Navigation`
requires `Imu`, the application must register `Imu` in the component graph.

## 8. Built-In Facility Inclusion

### 8.1 Enabled is not present

Kconfig enablement means Solar compiled support for a capability. It does not by
itself create a runtime facility.

A built-in facility is present only when:

```text
the capability is enabled
AND
(the effective blueprint requires it OR an explicit force policy selects it)
```

### 8.2 Sources of requirement

A built-in may be required by:

- a non-empty subsystem catalog;
- another present subsystem with a required relationship;
- an explicit application facility selection;
- an explicit Kconfig or C++ always-present policy.

Calls appearing in arbitrary function bodies are not a reliable source of
blueprint demand because standard C++ does not provide whole-program reflection
over call sites. Required presence must follow declared composition.

### 8.3 Zero cost when unused

An enabled but unrequired facility contributes no:

- runtime storage;
- lifecycle records;
- mutexes or queues;
- worker registrations;
- initialization work;
- inspection records.

### 8.4 Disabled but required

If the effective blueprint requires a disabled capability, system validation
fails at compile time with a subsystem-specific diagnostic.

Example diagnostic intent:

```text
RobotBlueprint declares parameters, but CONFIG_SOLAR_PARAMETERS is disabled
```

Optional cross-subsystem integrations disappear at compile time when their
target capability is unavailable. Required integrations fail validation.

## 9. Application Binding

### 9.1 Trait model

The conceptual binding customization point is:

```cpp
namespace solar
{

struct DefaultApplication;

template<typename Application = DefaultApplication>
struct system_binding
{
    static constexpr bool bound = false;
};

} // namespace solar
```

The exact implementation may use helper bases or concepts to improve
diagnostics.

### 9.2 Binding macro

The normal macro specializes the default application binding:

```cpp
SOLAR_BIND_SYSTEM(app::RobotSystem);
```

Conceptually it declares:

```cpp
template<>
struct solar::system_binding<solar::DefaultApplication>
{
    static constexpr bool bound = true;
    using System = app::RobotSystem;
};
```

The macro exists to avoid verbose and error-prone specialization syntax. It
must not:

- define a system object;
- allocate storage;
- run initialization;
- emit a runtime constructor;
- create a service locator;
- hide the selected type from diagnostics.

### 9.3 Tagged alternate bindings

Tests and specialized tools may bind another application tag:

```cpp
struct NavigationTest;

SOLAR_BIND_SYSTEM_FOR(NavigationTest, test::NavigationSystem);

auto report = solar::boot<NavigationTest>();
auto value = solar::parameters::Of<NavigationTest>::get<DriveKp>();
```

The final argument ordering of subsystem templates belongs to their owning
specifications. The accepted requirement is that explicit application tags are
public and do not alter the default production binding.

### 9.4 Application binding is canonical

Subsystems derive their active catalog and facility from the application
binding. Production code does not independently bind parameters, events,
metrics, bus, logging, or Remote.

Independent subsystem bindings risk describing mutually inconsistent firmware
systems. A later subsystem may expose an isolated test harness, but that is not
a second production composition mechanism.

## 10. Binding Visibility And Include Direction

### 10.1 Required project layout

A normal project uses one composition root and ordinary component files:

```text
firmware/
  include/
    app/system.hpp
    devices/motors.hpp
    parameters/drive_parameters.hpp
    services/navigation.hpp
  src/
    main.cpp
    devices/motors.cpp
    services/navigation.cpp
```

No `system_fwd.hpp` is required.

### 10.2 Component header

```cpp
// services/navigation.hpp
#pragma once

#include "devices/imu.hpp"
#include "devices/motors.hpp"

#include <solar/service.hpp>
#include <solar/core/result.hpp>

struct Navigation
{
    using Dependencies = solar::Dependencies<Imu, LeftMotor, RightMotor>;

    static solar::Result<void> init();
    static solar::Result<void> start();
    static void run(solar::StopToken stop);
    static solar::Result<void> stop();
};
```

The component includes its direct typed dependencies because it calls them. It
does not include `app/system.hpp` and does not obtain those types from Solar.

### 10.3 Composition root

```cpp
// app/system.hpp
#pragma once

#include "devices/imu.hpp"
#include "devices/motors.hpp"
#include "parameters/drive_parameters.hpp"
#include "services/navigation.hpp"

#include <solar/system.hpp>

namespace app
{

using RobotBlueprint = solar::Blueprint<
    solar::Devices<Imu, LeftMotor, RightMotor>,
    solar::Services<Navigation>,
    solar::Parameters<DriveKp>>;

using RobotSystem = solar::System<RobotBlueprint>;

} // namespace app

SOLAR_BIND_SYSTEM(app::RobotSystem);
```

### 10.4 Component implementation

```cpp
// services/navigation.cpp
#include "app/system.hpp"

solar::Result<void> Navigation::start()
{
    auto kp = solar::parameters::get<DriveKp>();
    LeftMotor::set_speed(0.0f);
    RightMotor::set_speed(0.0f);
    return {};
}
```

Including the root from the implementation source does not create a circular
header dependency. The root first includes the component declaration, declares
and binds the system, and then the source defines the method.

### 10.5 Include-direction invariant

```text
component header
    -> Solar declaration headers
    -> direct application dependency headers

composition root
    -> all registered component/catalog declaration headers
    -> Solar system and binding headers

component source
    -> composition root
    -> out-of-line bound method definitions

application entry
    -> composition root
```

The forbidden direction is:

```text
component header -> composition root
```

### 10.6 Binding visibility by mode

Strict bound APIs use the selected system type to validate subsystem
availability and catalog membership. Standard C++ compilation cannot discover a
trait specialization declared later in another translation unit.

Therefore a translation unit that instantiates a strictly bound API must see
the relevant binding specialization first. The composition-root include in
component source files satisfies this without contaminating component headers.

The default relaxed mode does not require binding visibility at each call site.
Its generic typed frontend is connected to the validated system during boot.
Only the composition root and application entry need to see the application
binding. Component headers may contain ordinary inline methods that use relaxed
global APIs while including only Solar and their direct declaration headers.

## 11. Global API Boundaries

### 11.1 APIs requiring a binding

The following classes of operation require an active system binding:

- boot and stop coordination;
- lifecycle records and reports;
- component graph and catalog queries;
- execution records for services, executors, and jobs;
- canonical subsystem state access;
- calls requiring registration in the active catalog;
- system-level feature availability queries.

Illustrative surface:

```cpp
solar::boot();
solar::stop();

solar::boot_report();
solar::stop_report();

solar::lifecycle::components();
solar::lifecycle::record<Navigation>();

solar::graph::components();
solar::graph::dependencies<Navigation>();

solar::catalog::parameters();
solar::catalog::events();

solar::execution::services();
solar::execution::executors();
solar::execution::jobs();
```

The final report and query return types belong to later specs.

### 11.2 Descriptor-only operations

Operations concerning only a descriptor type's local immutable definition may
remain unbound:

```cpp
static_assert(DriveKp::default_value == 1.0f);
static_assert(solar::descriptor_for<DriveKp>.name == "drive.kp");
```

They do not claim that the descriptor is registered in any firmware system.

### 11.3 Catalog registration modes

Intentional global subsystem operations require the descriptor to belong to the
bound effective catalog:

```cpp
solar::parameters::set<DriveKp>(1.4f);
solar::events::observe<FrameDropped>({...});
solar::metrics::inc<DroppedFrameCount>();
solar::bus::emit<ButtonPressed>({...});
```

The effective catalog is authoritative in both supported modes:

- with `CONFIG_SOLAR_STRICT_CATALOG_BINDING=y`, an unregistered descriptor at a
  bound call site is a compile-time error;
- with `CONFIG_SOLAR_STRICT_CATALOG_BINDING=n`, the default, an unregistered
  descriptor compiles but returns `NotRegistered` after frontend binding.

Relaxed calls made before frontend binding return `NotReady`. Calls to a
Kconfig-disabled subsystem return `Disabled`. A global operation never silently
creates standalone state or an implicit registry entry in either mode.

Disabled diagnostic subsystem behavior may receive carefully documented no-op
forms in its owning specification. Logging retains its explicit early-ingress
rules independently of catalog frontend binding.

### 11.4 Execution, not kernel, owns Solar execution inspection

`solar::kernel` exposes Zephyr-derived primitives and low-level scheduler facts.
`solar::execution` exposes Solar services, executors, jobs, and deferred-work
records.

```cpp
solar::kernel::this_thread::sleep_for(20ms);

solar::execution::record<Navigation>();
solar::execution::executors();
```

Application execution metadata must not be placed into the kernel namespace.

## 12. Compile-Time Validation And Diagnostics

### 12.1 Validation timing

Structural blueprint validation occurs when `System<Blueprint>` is formed or
when the binding requires its validated effective blueprint. Bound subsystem
calls perform their own focused membership and availability checks.

The implementation should avoid one enormous assertion that hides the actual
failure.

### 12.2 Required diagnostics

Solar must diagnose at least:

- unknown blueprint section;
- duplicate singular section;
- duplicate effective component type;
- component in incompatible categories;
- unregistered component dependency;
- disabled but required subsystem;
- invalid subsystem configuration entry;
- duplicate or contradictory policy;
- missing default application binding;
- more than one binding for an application tag;
- invalid bound type that is not a Solar `System`;
- unregistered descriptor used by a strict global API;
- unavailable subsystem used intentionally;
- cyclic component dependency graph.

### 12.3 Diagnostic intent

Messages should name the failing concept and relevant type when practical:

```text
Solar system binding is missing for DefaultApplication
```

```text
DriveKp is not registered in the parameter catalog of app::RobotSystem
```

```text
Navigation requires Imu, but Imu is not registered as a component
```

Compile-fail tests must check diagnostic categories without depending on full
compiler-specific type rendering.

## 13. Runtime Ownership

Blueprints, section traits, catalogs, bindings, and effective-blueprint data are
compile-time constructs and own no mutable runtime state.

The binding does not centralize state. It permits namespace APIs to select the
correct type-owned subsystem state.

Conceptually:

```cpp
template<typename Application>
using bound_system_t = typename system_binding<Application>::System;

template<typename Application>
using parameter_facility_t =
    typename bound_system_t<Application>::facilities::parameters;
```

Actual runtime storage belongs to `parameter_facility_t`, not to
`system_binding` and not to a universal system storage object.

All runtime state must remain bounded and allocation-free unless an owning
subsystem specification explicitly justifies another policy.

## 14. Lifecycle And Initialization

Binding performs no lifecycle action. Static initialization must not boot the
system or register runtime objects.

Lifecycle begins only through an explicit call:

```cpp
auto result = solar::boot();
```

The effective component graph determines lifecycle participation and order.
Built-in facilities appear as normal effective components when required.

Detailed hook detection, missing-hook behavior, reports, reboot rejection, and
stop semantics belong to Phase 3. This specification requires only that those
operations resolve through the same application binding and static system type.

## 15. Concurrency And ISR Behavior

Blueprint normalization and binding have no runtime concurrency behavior.

Global subsystem calls inherit concurrency and ISR rules from their owning
facility. The binding must not add a universal lock or serialize unrelated
subsystems.

No global operation is assumed ISR-safe merely because it is statically
addressed. Subsystem specifications must identify ISR-safe variants explicitly.

## 16. Error And Result Behavior

Compile-time architecture mistakes are compilation errors, not runtime
`Status` values.

Runtime boot and stop operations use the C++23 result model established in
`00a-modern-cpp-result-and-status.md`:

```cpp
solar::Result<solar::BootReport, solar::BootError> solar::boot();
```

The exact boot signature remains owned by Phase 3. A persistent report remains
queryable separately:

```cpp
solar::boot_report();
```

Subsystem operations return their own typed results after binding and
registration validation. The binding itself has no runtime failure mode.

## 17. Kconfig And C++ Policy Boundary

Kconfig controls:

- whether a Solar capability is compiled and available;
- Zephyr integration and platform support;
- default capacities and scalar policies;
- optional explicit always-present facility policies;
- diagnostics compiled into the firmware.

C++ blueprint types control:

- application component membership;
- subsystem declaration membership;
- direct dependencies;
- execution registrations;
- local typed policy overrides;
- explicit demand for supported built-in facilities.

Kconfig must not name application C++ types or select the application binding.
A C++ policy cannot locally re-enable a Kconfig-disabled capability.

## 18. Catalog, Contribution, And Inspection Interaction

The blueprint supplies direct declarations and component roots. Contributions
from components are collected during normalization and preserve their origin.

Catalogs are immutable compile-time results. The binding selects which system's
catalogs a global API addresses.

Inspection may expose:

- user-declared sections;
- effective component membership;
- automatically included built-ins and their inclusion reason;
- active subsystem policies and their source;
- focused runtime records owned by later subsystems.

Inspection must not expose the binding as though it were a runtime object.

Remote may consume selected catalogs after binding, but it does not own the
system binding or redefine system membership.

## 19. Identity And Metadata Boundaries

This phase uses C++ type identity to classify sections and register members.

It does not authorize compiler type-name hashes as stable IDs. Local numeric
IDs, stable external IDs, human-readable names, ownership wrappers, and rename
rules belong to Phase 2.

Normalization must preserve sufficient type and origin information for Phase 2
to implement those rules without changing the blueprint or binding shape.

## 20. Migration From Current Solar

Current positional system declarations historically included separate board
and peripheral lists:

```cpp
using System = solar::System<
    Board,
    Peripherals,
    Devices,
    Facilities,
    Services,
    Tasks,
    Channels,
    Runtime>;
```

migrate to:

```cpp
using Blueprint = solar::Blueprint<
    solar::Devices<...>,
    solar::Facilities<...>,
    solar::Services<...>,
    solar::Executors<...>,
    solar::Execution<...>,
    subsystem_catalogs...,
    subsystem_configurations...>;

using System = solar::System<Blueprint>;
SOLAR_BIND_SYSTEM(System);
```

Required migration actions include:

1. Introduce generic section classification and normalization.
2. Change `System` to accept one blueprint.
3. Move board and peripheral wrappers to Solar Hardware or deliberate
   application `Device` types.
4. Remove `Channels` and map use cases to the typed bus design.
5. Replace `Tasks` component treatment with execution registrations.
6. Derive or select built-in facilities according to their subsystem
   inclusion class.
7. Introduce the application binding trait and macro.
8. Add global boot, lifecycle, graph, catalog, and execution access surfaces.
9. Move application bound method definitions to source files where necessary.
10. Remove system object parameters from entry and lifecycle integration.
11. Preserve explicit `System` access for tests and metaprogramming.

Migration compatibility aliases may be temporary, but new architecture code
must not retain positional template growth as its implementation model.

## 21. Testing Obligations

The implementation pass must include:

### 21.1 Compile-pass tests

- minimal empty blueprint;
- blueprint containing no application devices;
- realistic full firmware declaration;
- section-order independence;
- omitted optional sections;
- enabled but unused built-in exclusion;
- enabled and required built-in inclusion;
- default binding global boot compilation;
- alternate tagged test binding;
- explicit named-system access;
- component source using bound APIs through the root include;
- dependent bound frontend with a binding declared later in the translation
  unit;
- defaulted function-template inline method called with ordinary syntax;
- standalone inclusion of a component containing an uninstantiated lazy inline
  bound method;
- lifecycle hook detection without early bound-body instantiation;
- descriptor-only operation without a binding.

### 21.2 Compile-fail tests

- unknown section;
- duplicate section;
- repeated component;
- missing dependency;
- disabled required subsystem;
- invalid subsystem policy;
- missing binding;
- duplicate binding for one tag;
- invalid bound system type;
- strict operation on unregistered descriptor;
- eager non-template inline bound use in a translation unit with no binding;
- lazy inline bound use called in a translation unit with no binding;
- component dependency cycle.

### 21.3 Runtime tests

- global and explicit system APIs address the same state;
- binding has no static initialization side effects;
- relaxed calls before binding return `NotReady`;
- relaxed calls to an absent catalog entry return `NotRegistered`;
- relaxed calls to a disabled subsystem return `Disabled`;
- relaxed frontend binding reaches the same canonical state as strict access;
- unused enabled facilities allocate no runtime owners;
- required built-ins appear in lifecycle and inspection;
- alternate test bindings do not disturb the default binding.

### 21.4 Build and size tests

- Zephyr builds with the required C++23 configuration;
- disabling and not requiring a subsystem removes its storage and execution
  machinery;
- no hidden heap requirement is introduced;
- representative firmware does not produce duplicate static state across
  translation units;
- dependent frontends and lazy inline methods compile under supported GCC and
  Clang toolchains;
- relaxed ordinary inline methods compile without composition-root visibility;
- strict and relaxed builds expose the same operation names and result types;
- representative multi-translation-unit builds pass with and without LTO;
- a complete Zephyr C++23 application using the pattern builds and links.

## 22. Bound Frontend Modes

Solar supports one catalog and one canonical state architecture with two
frontend-binding strategies. Kconfig selects the strategy build-wide so every
translation unit observes one consistent inline API definition.

### 22.1 Relaxed mode is the default

With `CONFIG_SOLAR_STRICT_CATALOG_BINDING=n`, global typed operations compile
without seeing the application binding at the call site:

```cpp
struct Navigation
{
    static inline solar::Result<void> update()
    {
        return solar::parameters::get<DriveKp>()
            .transform([](float kp) { apply_gain(kp); });
    }
};
```

The header includes only the descriptor declaration and relevant Solar header.
It never includes the composition root and does not need to turn an ordinary
method into a template.

Before lifecycle hooks run, `solar::boot()` walks each effective catalog and
connects its descriptor frontends to the bound system's type-owned facility,
storage, and policy. Conceptually:

```text
parameters::frontend<DriveKp>
    -> RobotSystem parameter operation table and canonical storage
```

This is runtime frontend binding, not runtime registration. The blueprint still
decides membership at compile time. Frontends own no parameter, metric, event,
bus, or Remote state and cannot add a descriptor to a catalog.

### 22.2 Relaxed error timing

Relaxed mode reports focused runtime errors:

| Condition | Result |
| --- | --- |
| operation before frontend binding | `NotReady` |
| subsystem excluded by Kconfig | `Disabled` |
| subsystem available but descriptor absent | `NotRegistered` |
| descriptor bound but facility unavailable | subsystem-specific unavailable error |

Mutating and state-querying global APIs must return a `Result` or another
explicit admission result capable of preserving these errors. Results remain
`[[nodiscard]]`. Logging follows its separate early-ingress and compile-time
filtering contract.

### 22.3 Relaxed overhead

The ordinary relaxed path may perform a readiness check, one frontend pointer
load, and an indirect call. It must not acquire a universal lock or copy payload
data merely to resolve the binding. That cost is expected to be negligible for
queue, mutex, formatting, protocol, and transport operations, but must be
benchmarked for hot atomic metric and ISR paths.

The initial implementation accepts this overhead. A later typed cached handle
may amortize lookup for measured hot paths, but no handle is required by the
initial public contract.

### 22.4 Strict mode

With `CONFIG_SOLAR_STRICT_CATALOG_BINDING=y`, the frontend resolves the selected
system and catalog membership at compile time. An unregistered descriptor is a
focused compile-time error and there is no runtime binding dispatch overhead.

Strict bound APIs make application lookup dependent on a function-template
parameter:

```cpp
template<
    typename Descriptor,
    typename Application = solar::DefaultApplication>
auto get()
{
    return solar::parameters::Frontend<Application>
        ::template get<Descriptor>();
}
```

A strict call site must see the binding. The normal component shape is therefore
an ordinary declaration with an out-of-line definition:

```cpp
struct Navigation
{
    static solar::Result<void> start();
};

// navigation.cpp
#include "app/system.hpp"

solar::Result<void> Navigation::start()
{
    return solar::parameters::get<DriveKp>()
        .transform([](float kp) { apply_gain(kp); });
}
```

### 22.5 Strict lazy inline form

Generic or deliberately header-only strict code may propagate an application
tag through a defaulted function template:

```cpp
struct Navigation
{
    template<typename Application = solar::DefaultApplication>
    static solar::Result<void> start()
    {
        return solar::parameters::Of<Application>::get<DriveKp>();
    }
};
```

Ordinary call syntax remains `Navigation::start()`. Taking its address may
require `&Navigation::start<>`. This form remains optional rather than the
common component pattern.

### 22.6 Shared invariants

Both modes use exactly the same:

- blueprint and contribution catalogs;
- normalized configuration and policy;
- effective components and built-in inclusion;
- canonical facility storage;
- lifecycle and synchronization;
- public operation names and result types.

There is no relaxed state beside strict state. Switching mode changes lookup
and validation timing, not ownership or semantics.

### 22.7 What frontend binding cannot discover

Solar cannot discover arbitrary descriptor calls by inspecting compiled
function bodies. A descriptor must still enter the effective catalog through a
component contribution or explicit root section. Relaxed use of an unreported
descriptor remains unbound and returns `NotRegistered`.

No duplicate `Uses<...>` declaration, source analysis, linker registration, or
implicit state creation is introduced.

### 22.8 Verification evidence required

Implementation must verify both modes under supported GCC and Clang builds,
multiple translation units, LTO and non-LTO, native Zephyr runtime tests, and
representative ISR and high-frequency metric benchmarks. Required Zephyr
language configuration is:

```text
CONFIG_CPP=y
CONFIG_STD_CPP23=y
CONFIG_REQUIRES_FULL_LIBCPP=y
```

## 23. Rejected Alternatives

### 23.1 Positional `System` growth

Rejected because every subsystem addition changes the central template shape,
defaults become brittle, and independent extension is difficult.

### 23.2 Arbitrary untagged blueprint contents

Rejected because category and policy meaning become implicit and diagnostics
degrade into template-shape failures.

### 23.3 System or context object passing

Rejected because Solar describes one integrated firmware system. Components
use normal includes and direct static APIs.

### 23.4 Required `system_fwd.hpp`

Rejected as the normal project model. It adds another project-wide composition
artifact and does not remove the underlying C++ binding-visibility rule.

### 23.5 Component headers including the root

Rejected because it reverses ownership, creates include cycles, couples reusable
component declarations to one application, and makes binding order fragile.

### 23.6 Kconfig naming the application C++ type

Rejected because Kconfig is not a C++ type registry and should not encode
application namespace spelling.

### 23.7 Forced compiler inclusion of a generated binding header

Rejected as the primary model because it hides include dependencies and adds
build-system magic. It may be reconsidered only if a compelling generated-code
workflow requires it.

### 23.8 Runtime or linker registration for the active system

Rejected because it weakens compile-time validation, introduces initialization
or linker-order concerns, and obscures ownership.

Relaxed runtime frontend binding is not registration: boot connects only the
descriptors already present in immutable compile-time catalogs, owns no
canonical subsystem state, and cannot discover or add call-site types.

### 23.9 Independent production subsystem bindings

Rejected because parameters, events, metrics, bus, logging, and Remote could
silently address inconsistent system descriptions. They derive from one
application binding.

### 23.10 Catalog and configuration mixed in one pack

Rejected because membership and policy are separate semantic roles.

### 23.11 One extra global configuration wrapper

Not prohibited forever, but rejected for the current design because subsystem
configuration sections are already recognizable siblings within `Blueprint`.

### 23.12 Automatically adding application dependencies

Rejected because a dependency declaration validates architecture; it does not
silently change what the application declared.

## 24. Accepted Decisions

1. `System` accepts one `Blueprint` type.
2. Blueprint sections are tagged and order-independent.
3. Singular section duplication is a compile-time error.
4. Boards and raw hardware endpoints belong to the Hardware layer rather than
   the component graph.
5. Device, facility, service, and executor are the component categories.
6. Tasks are leaf execution registrations by default.
7. Channels are removed; Phase 4 maps each use to the typed bus, a typed queue,
   a direct call, or component-owned state.
8. Catalog and subsystem configuration are separate sibling sections.
9. Built-ins use an explicit demand-derived, Kconfig-selected, or
   required-derived inclusion rule.
10. Automatic built-in inclusion is visible in the effective blueprint.
11. One application trait binding is canonical for production global APIs.
12. `SOLAR_BIND_SYSTEM` only declares a type specialization.
13. Public alternate application tags support isolated tests.
14. Subsystems derive their binding from the application binding.
15. Strict operations validate catalog membership at compile time; relaxed
    operations validate through boot-bound frontends and focused runtime errors.
16. Descriptor-local immutable operations may remain unbound.
17. `solar::execution`, not `solar::kernel`, owns Solar execution inspection.
18. One application composition root is required; `system_fwd.hpp` is not.
19. Ordinary component headers never include the composition root.
20. Strict bound implementations normally include the root from their source
    file and define methods out of line; relaxed implementations may remain
    ordinary inline methods.
21. The binding creates no object, storage, registry, or static initialization.
22. Explicit named-system access remains supported.
23. Relaxed catalog binding is the Kconfig default and uses non-owning
    frontends connected during boot.
24. Relaxed unbound calls return `NotReady`, `Disabled`, or `NotRegistered` and
    never create state or registration.
25. Strict subsystem APIs resolve through dependent application frontends with
    no runtime binding dispatch.
26. Ordinary out-of-line non-template methods are the normal strict component
    implementation.
27. Defaulted function-template methods remain an optional strict lazy-inline
    form and retain ordinary call syntax.
28. Strict and relaxed modes share catalogs, policy, canonical state,
    synchronization, public API names, and result types.
29. Frontend binding does not use linker registration, generated call-site
    discovery, or manual duplicate-use registration.

## 25. Open Questions

The architecture has no blocking open questions for Phase 2.

The following naming details remain intentionally deferred:

- exact public names for alternate-binding macros and explicit application-tag
  argument ordering;
- exact internal section-trait and normalization helper names.

These questions may refine ergonomics but must not overturn the accepted
ownership, binding, validation, or include-direction rules.
