# Solar Design Conventions

Date: 2026-07-13

Status: accepted

Companion specification:
`development-docs/design-specs/00a-modern-cpp-result-and-status.md`

## 1. Purpose

This specification defines the vocabulary, semantic boundaries, design rules,
and document conventions used by every later Solar subsystem specification.

Solar is a modern, static, type-driven firmware framework for Zephyr. It
describes and coordinates one integrated firmware image. Its APIs should feel
global where the underlying firmware capability is global, while ownership and
compile-time composition remain explicit and inspectable.

This specification is normative. Later specifications may refine a term inside
their own domain, but must not assign it a conflicting meaning.

## 2. Non-Goals

This specification does not settle:

- the final `Blueprint<...>` template syntax;
- the implementation of the active-system binding;
- individual bus, parameter, event, metric, logging, execution, or Remote APIs;
- exact component descriptor fields;
- concrete persistence or protocol formats;
- implementation sequencing.

Those decisions belong to later specifications. This document establishes the
language in which they will be made.

## 3. Architectural Statement

Solar operates one statically described firmware system.

The application declares what exists. Components own domain behavior and local
state. Solar validates composition, coordinates lifecycle and execution,
derives catalogs, and exposes global subsystem APIs. Zephyr owns the kernel,
hardware model, drivers, and build-time platform capabilities.

Solar is not:

- an object container;
- a dependency injection framework;
- a runtime service locator;
- a replacement operating system;
- a dynamic plugin registry;
- a universal state store.

The intended relationship is:

```text
application types describe the firmware
        |
        v
blueprint declares composition and policy
        |
        v
System validates and coordinates
        |
        v
bound namespace APIs expose global capabilities
        |
        v
components call direct dependencies and Solar subsystems
```

## 4. Normative Language

The words **must**, **must not**, **should**, **should not**, and **may** are used
normatively:

- **must**: required by the accepted architecture;
- **must not**: prohibited by the accepted architecture;
- **should**: expected unless a later specification documents a strong reason;
- **should not**: discouraged unless a later specification justifies it;
- **may**: permitted but not required.

## 5. Core Vocabulary

### 5.1 Firmware system

A **firmware system** is the single integrated application described by one
Solar blueprint and selected by one application binding.

It includes:

- application components;
- lifecycle and dependency relationships;
- execution infrastructure;
- subsystem declarations and policies;
- compile-time catalogs;
- enabled built-in Solar facilities.

There is one active firmware system per firmware image.

The word `System` refers to the static coordinating type, never to a
user-created runtime object.

### 5.2 Blueprint

A **blueprint** is the complete compile-time declaration of the firmware
system. It describes composition and policy but does not itself own runtime
state.

Illustrative syntax:

```cpp
using RobotBlueprint = solar::Blueprint<
    solar::Devices<LeftMotor, RightMotor, Imu>,
    solar::Services<Navigation, RemoteTransport>,
    solar::Parameters<DriveKp, DriveKi>>;

using RobotSystem = solar::System<RobotBlueprint>;
```

The final group syntax is reserved for Phase 1.

### 5.3 Effective blueprint

The **effective blueprint** is the validated composition produced from:

1. the user blueprint;
2. built-in Solar facilities selected by their inclusion class;
3. declarations derived from explicit use of supported defaults;
4. Kconfig-selected capabilities and defaults.

Automatic inclusion must remain visible through graph and catalog inspection.
Built-in must not mean hidden. Each built-in declares one inclusion class:
demand-derived, Kconfig-selected, or required-derived. Kconfig-selected
facilities are present whenever selected; demand-derived facilities additionally
require effective use; required-derived facilities are pulled in by another
selected component. An explicit force policy may also require presence where a
subsystem permits it.

The effective blueprint is still a compile-time description. It is not a
runtime registry.

### 5.4 Binding

A **binding** selects the one `System` type used by global Solar APIs.

Illustrative syntax:

```cpp
using RobotSystem = solar::System<RobotBlueprint>;
SOLAR_BIND_SYSTEM(RobotSystem);
```

A binding:

- selects a type;
- creates no runtime object;
- owns no state;
- performs no dependency lookup;
- does not replace normal C++ includes;
- must produce clear missing or conflicting binding diagnostics.

The exact mechanism is reserved for Phase 1.

### 5.5 Component

A **component** is a unique logical participant in the firmware architecture,
represented by a concrete C++ type.

A component may:

- expose a static domain API;
- own static type-owned runtime state;
- declare required component dependencies;
- participate in lifecycle;
- contribute subsystem declarations;
- own hardware or sustained behavior;
- expose focused inspection facts.

```cpp
struct LeftMotor
{
    static solar::Status init();
    static solar::Status set_speed(float speed);
};
```

Component identity is its concrete type. A component name or numeric ID is
metadata, not C++ identity.

A concrete component type may occur at most once in one effective component
graph unless a later specification introduces a distinct explicit identity
wrapper. Multiple object instances are not inferred from repeated type use.

### 5.6 Component category

A **component category** describes architectural role, not identity or an
inheritance hierarchy.

Initial vocabulary includes:

- device;
- facility;
- service;
- executor.

Boards, devicetree nodes, and raw hardware endpoints are not component
categories. They belong to the Zephyr and Solar Hardware layer. An
application-level driver or hardware abstraction participates as a `device`
only when the application deliberately declares that type in `Devices<...>`.

A category may influence lifecycle metadata, validation, diagnostics, and
carefully defined default policy. It must not require a common object base.

Task behaviors and execution registrations are not components by default.

### 5.7 Facility

A **facility** is a passive capability component that owns or coordinates
canonical runtime state but does not inherently require a permanent execution
loop.

A facility may:

- participate in lifecycle;
- contribute declarations;
- own bounded storage and synchronization;
- use work items or shared executors;
- expose focused diagnostics;
- be defined by Solar or by an application.

Examples include parameter storage, metric storage, event capture/history, and
protocol-engine state.

Using occasional deferred work does not make a facility a service.

Facility types are architectural owners. They should not normally appear in the
ordinary public call syntax:

```cpp
solar::metrics::inc<FramesDropped>();       // preferred
MetricsFacility::inc<FramesDropped>();      // internal/explicit use only
```

### 5.8 Built-in facility

A **built-in facility** is a Solar-provided facility made available by Kconfig
and selected for inclusion by derived use or an explicit force policy.

Present built-in facilities must be included visibly in the effective
blueprint and must receive the same lifecycle, dependency, contribution, and
inspection treatment as user facilities. Merely enabling a capability must not
create unused runtime storage, lifecycle records, synchronization, or execution
cost.

Users may explicitly configure or include a built-in facility where the later
subsystem specification permits it, but common applications should not need to
manually repeat standard Solar facilities in their blueprint.

### 5.9 Service

A **service** is a unique active component with one sustained execution loop and
explicit cooperative shutdown.

```cpp
struct RemoteTransportService
{
    static solar::Status init();
    static solar::Result<void> run(solar::StopToken stop);
    static solar::Status stop();
};
```

A service:

- is represented by one registered type;
- has at most one active execution instance in the firmware system;
- uses Solar-owned or explicitly selected execution infrastructure;
- has lifecycle and execution diagnostics;
- must not be used merely because code is shared or important.

Short-lived or repeatable jobs are tasks, not services.

### 5.10 Task behavior

A **task behavior** is reusable bounded behavior independent of where and when
it executes.

```cpp
struct UpdateOdometry
{
    static solar::Status execute();
};
```

A task behavior:

- is not a component by default;
- does not imply a thread;
- does not independently receive component lifecycle;
- may be used by more than one execution registration.

### 5.11 Execution registration

An **execution registration** binds task behavior to an identity, executor,
trigger/schedule, and policy.

Illustrative syntax:

```cpp
using OdometryJob = solar::execution::Periodic<
    solar::Name<"update-odometry">,
    UpdateOdometry,
    10ms,
    MainWorker>;
```

An execution registration is a leaf registration. It may have policy-owned
runtime state and focused diagnostics, but it is hosted by an executor rather
than treated as an independent component branch.

### 5.12 Executor

An **executor** is a component that owns machinery for deferred or scheduled
work.

It may own:

- a queue or mailbox;
- a thread and stack;
- synchronization and scheduling state;
- capacity and overflow state;
- lifecycle and execution diagnostics.

Many task registrations, bus subscribers, event processors, parameter writers,
or Remote handlers may share one executor.

An executor is not the behavior it executes.

The canonical subsystem namespace is `solar::execution`.

Solar should provide an ergonomic shared worker for the common path. Later
specifications must decide whether its inclusion is explicit, Kconfig-selected,
or derived when a declaration chooses the default worker. Derived and visible
inclusion is the current preferred direction.

### 5.13 Leaf registration

A **leaf registration** is a typed declaration hosted by a component or
subsystem but not independently treated as a component.

Examples include:

- task execution registrations;
- bus subscriptions;
- event-to-metric adapters;
- event-to-log policies;
- Remote exposures;
- deferred parameter persistence jobs.

A leaf registration:

- has compile-time identity and policy;
- may own small bounded policy state;
- belongs to a catalog;
- is inspectable through its host subsystem;
- does not receive full component lifecycle by default.

### 5.14 Dependency

A **dependency** states that one component requires another component to be
operational.

```cpp
struct Navigation
{
    using Dependencies = solar::Dependencies<Imu, LeftMotor, RightMotor>;
};
```

Dependencies provide:

- graph validation;
- initialization and start ordering;
- reverse stop and deinitialization ordering;
- architecture inspection.

Dependencies do not provide access. Application code includes and calls the
types it intentionally depends on.

```cpp
#include "devices/imu.hpp"
#include "devices/motors.hpp"

auto sample = Imu::sample();
LeftMotor::set_speed(command.left);
```

**Dependencies order; normal C++ names and calls access.**

### 5.15 Ownership

**Ownership** answers which type or subsystem is responsible for the meaning
and canonical state of something.

Specifications must distinguish three kinds of ownership:

- **semantic ownership**: who defines and is responsible for a declaration;
- **runtime storage ownership**: who stores its mutable canonical state;
- **execution ownership**: which service or executor performs associated work.

Example:

```text
RemoteTransport semantically owns FrameDropped
event facility owns retained FrameDropped occurrences
EventProcessor executor owns deferred processing work
```

Every mutable fact must have exactly one canonical runtime owner.

### 5.16 Registration

**Registration** means compile-time inclusion in a blueprint or derived
catalog.

Registration means Solar can validate and enumerate a declaration. It does not
necessarily mean:

- runtime construction;
- component lifecycle;
- storage inside `System`;
- dependency;
- subscription;
- external exposure.

Use `registered`, not `created`, for compile-time membership.

### 5.17 Contribution

A **contribution** declares subsystem vocabulary semantically owned or provided
by a component.

```cpp
struct DriveController
{
    using Metrics = solar::metrics::Contribute<
        ControlDuration,
        SaturationCount>;

    using Events = solar::events::Contribute<
        ControlDeadlineMissed>;
};
```

Collection must preserve the origin component.

A contribution does not imply:

- component dependency;
- bus subscription;
- Remote exposure;
- lifecycle ordering;
- event delivery.

**Contributions declare ownership; catalogs collect it.**

### 5.18 Catalog

A **catalog** is an immutable compile-time collection of validated declarations
and ownership metadata.

Examples include:

- component catalog;
- event catalog;
- metric catalog;
- parameter catalog;
- subscription catalog;
- execution catalog;
- Remote schema/Data/Action/Topic/Stream catalogs.

A catalog supports compile-time validation, descriptor generation, filtering,
transformation, bounded inspection, and schema generation.

A catalog does not own mutable runtime values or occurrences.

The word **registry** is reserved for a genuinely runtime-mutable collection.
Solar should prefer catalogs and avoid runtime registries unless dynamic
membership is an explicit requirement.

### 5.19 Descriptor

A **descriptor** is immutable metadata describing one typed declaration.

A descriptor may include:

- local ID;
- stable external ID;
- name and description;
- semantic owner;
- component category or declaration kind;
- units or value schema;
- access or exposure policy.

A descriptor must not contain mutable values, counters, lifecycle state, or
kernel handles. Those belong to records or subsystem state.

### 5.20 Record

A **record** is a bounded runtime fact associated with a declaration.

Examples include:

- `LifecycleRecord`;
- `ServiceExecutionRecord`;
- `EventRecord`;
- `LogRecord`;
- parameter runtime state;
- task/executor execution records.

Unlike descriptors, records change over time.

The word **snapshot** is reserved for an explicitly point-in-time copy of one
focused runtime area. It must not be used as a vague all-system query.

### 5.21 Policy

A **policy** is a compile-time or Kconfig-selected rule governing one narrow
behavior without changing semantic identity.

Examples include:

- bus delivery policy;
- event retention policy;
- parameter persistence policy;
- task execution policy;
- Remote overflow policy.

Policies must not become unstructured configuration bags.

Policy scopes are:

- **declaration policy**: explicitly attached to one declaration;
- **blueprint subsystem policy**: default for one subsystem in one firmware;
- **Kconfig default**: build/platform default.

Precedence is fixed:

```text
explicit declaration policy
    > blueprint subsystem policy
    > Kconfig default
```

A subsystem may omit an inapplicable level but must not reverse precedence.

### 5.22 Configuration

Specifications must distinguish:

- **Kconfig configuration**: build capability, Zephyr integration, resource
  defaults, and feature selection;
- **type configuration**: C++ composition, descriptors, typed policies, and
  execution strategy;
- **runtime parameters**: registered values intentionally adjustable while the
  firmware runs.

Example:

```text
CONFIG_SOLAR_REMOTE_MAX_FRAME_SIZE=512  Kconfig resource default
CobsCrc32                               typed protocol policy
parameters::set<DriveKp>(1.4f)          runtime parameter
```

### 5.23 Namespace API

A **namespace API** is the normal global public surface of a Solar subsystem.

```cpp
solar::bus::emit<ButtonPressed>(payload);
solar::parameters::get<DriveKp>();
solar::events::observe<FrameDropped>(payload);
solar::metrics::inc<DroppedFrames>();
solar::log::warn<RemoteService>("TX queue full");
```

A namespace API may delegate to descriptor-owned storage, a bound system
catalog, or a built-in facility. The namespace is a public access surface, not
the runtime owner itself.

Ordinary public calls should not require users to name an internal facility
type.

### 5.24 Subscription

A **subscription** declares that a handler receives an application bus event
under a delivery policy.

Subscription means behavioral dispatch. The term must not be reused for:

- observability event sinks;
- Remote client subscriptions;
- metric exporters;
- log sinks.

Those mechanisms may reuse execution primitives but retain distinct semantics.

### 5.25 Exposure

**Exposure** is the explicit decision to make an internal declaration or fact
available through an external interface.

Examples include:

- exposing a parameter for Remote read/write;
- exposing an internal bus event as a Remote topic;
- exposing metrics as telemetry;
- exposing an RPC endpoint.

Registration or contribution must not automatically imply exposure.

**Declared internally does not mean externally accessible.**

### 5.26 Sink

A **sink** consumes records for storage, forwarding, or rendering. It does not
become the canonical owner of the source subsystem's fact merely by consuming
it.

### 5.27 Observer

An **observer** is infrastructure that receives structured observability event
records. It may retain, count, aggregate, render, or forward them. It must not
casually invoke application domain behavior.

### 5.28 Exporter

An **exporter** periodically or reactively exposes canonical subsystem facts to
another boundary. Metrics and parameter storage remain canonical in their
owning facilities while an exporter reads them.

### 5.29 Adapter

An **adapter** explicitly translates declarations or occurrences between
subsystems.

Examples include:

- expose a bus event as a Remote topic;
- count an observability event as a metric;
- render an event as a log;
- persist a parameter through Zephyr settings.

Cross-subsystem behavior should use explicit adapters when it is not an
intrinsic dependency. This prevents optional subsystems from becoming one
inseparable cluster.

## 6. Semantic Boundaries

### 6.1 Bus events

Bus events coordinate application behavior.

```cpp
solar::bus::emit<ButtonPressed>(payload);
```

They have application subscribers and delivery policy. Their purpose is to
cause reactions while allowing producers not to know consumers.

### 6.2 Observability events

Observability events record structured operational facts.

```cpp
solar::events::observe<FrameDropped>(payload);
```

They feed infrastructure observers, retention, metrics, logs, diagnostics, and
external tooling. They must not become a second application control bus.

### 6.3 Logs

Logs provide human-oriented diagnostic explanation.

```cpp
solar::log::warn<RemoteService>("TX queue full");
```

They need no dedicated structured event type and are optimized for developer
comprehension rather than stable machine semantics.

### 6.4 Parameters

Parameters are globally identifiable, validated, optionally persistent runtime
configuration values.

The canonical C++ namespace is `solar::parameters`.

“System Variables” may be used as product or Remote UI terminology, but must not
replace `parameters` in the architecture or canonical API.

Parameters must not become storage for arbitrary private component state.

### 6.5 Metrics

Metrics are typed numeric instruments representing accumulated, sampled, or
derived numeric facts. They are passive canonical state, not event delivery and
not a telemetry transport.

### 6.6 Remote

Remote is an external protocol and transport boundary. It consumes selected
catalogs and canonical facts but must not own parameter values, metric state,
event truth, log truth, lifecycle truth, or application bus behavior.

## 7. Feature Inclusion And Availability

### 7.1 Feature states

Specifications must distinguish:

- **enabled**: selected by Kconfig or supported configuration;
- **present**: included in the effective blueprint;
- **available**: valid to call in the bound firmware after composition
  validation.

The exact trait names are reserved for Phase 1. Conceptually:

```cpp
solar::feature_enabled<solar::features::Logging>
solar::feature_present<solar::features::Logging, RobotSystem>
solar::feature_available<solar::features::Logging>
```

### 7.2 Disabled subsystems

A disabled built-in subsystem should incur no runtime storage, lifecycle, or
execution cost.

Behavior when code intentionally calls a disabled subsystem must be specified
per subsystem. The shared defaults are:

- fire-and-forget diagnostic logging may compile to a no-op;
- fallible `try_*` diagnostic calls return `Status::NotSupported` or a typed
  equivalent;
- calls requiring absent canonical state should fail at compile time;
- optional internal integrations should disappear through compile-time feature
  checks;
- invalid required feature combinations should fail blueprint validation.

Later subsystem specs may strengthen these rules but must state the behavior
explicitly.

### 7.3 Cross-subsystem relationships

Every cross-subsystem relationship must be classified as:

- **required**: the sibling subsystem must be available;
- **optional**: behavior compiles away when unavailable;
- **adapter-driven**: behavior exists only when an explicit adapter is
  registered.

Examples such as event-to-log rendering and event-to-metric mapping should be
adapter-driven, not unconditional dependencies of event capture.

## 8. Identity Model

Solar uses four distinct identity layers.

### 8.1 C++ type identity

The concrete type is authoritative for compile-time registration, lookup,
dependency, and uniqueness.

```cpp
DriveKp
```

### 8.2 Local numeric ID

A local ID is a compact build-local identity assigned from a validated catalog.
It is suitable for bounded arrays and runtime records.

It may change when the blueprint changes and must not silently become a
persistence or wire contract.

### 8.3 Stable external ID

A stable external ID is explicit or controlled by a versioned manifest. It is
required when identity must survive builds, schema changes, persistence, or
external communication.

Stable IDs are required for at least:

- persistent parameters;
- observability event schemas;
- Remote Schemas, Data, and Actions;
- Remote Topics and Streams;
- externally visible message types;
- persistent structured diagnostic schemas.

Later specs may require stable IDs for additional declarations.

Compiler-derived type-name hashes must not be wire or persistence contracts.

### 8.4 Human-readable name

A human-readable name supports inspection, manifests, logs, and tooling. It may
have a documented external stability contract but is never a substitute for
C++ type identity.

### 8.5 Ownership identity

Collected declarations must retain their semantic origin. Catalog entries
should be representable conceptually as:

```cpp
solar::OwnedBy<RemoteTransport, FramesDropped>
```

The final representation belongs to Phase 2.

## 9. Runtime State Rules

- Every mutable fact must have one canonical runtime owner.
- Runtime storage should be type-owned and distributed by responsibility.
- `System` must not become a monolithic storage object.
- Global namespace APIs do not imply global unstructured state.
- Internal ordinary objects are encouraged where they model state naturally.
- Core storage must be bounded and allocation-free by default.
- Dynamic allocation requires explicit justification in the owning spec.
- Synchronization and ISR access rules must be stated per mutable state owner.
- Remote, Inspection, sinks, and exporters consume facts; they do not replace
  canonical storage.

## 10. Error And Result Vocabulary

Solar uses three layers:

1. `solar::Status` for broad framework/lifecycle classification;
2. typed subsystem errors for queryable domain reasons;
3. `solar::Result<T, E>` as the fallible value carrier.

Solar targets C++23 and defines `Result` over `std::expected`:

```cpp
namespace solar
{
template<typename T, typename E = Status>
using Result = std::expected<T, E>;

template<typename E>
using Failure = std::unexpected<E>;
}
```

Typed errors should remain rich until an explicit boundary maps them to
`Status`. Specifications must not flatten all subsystem failures merely for
convenience.

Full rules, monadic use, migration, and capability requirements are defined in
`00a-modern-cpp-result-and-status.md`.

## 11. Modern C++ Baseline

Solar requires C++23 and intends to use the standard deliberately, including:

- `std::expected`;
- `and_then`, `transform`, `or_else`, and `transform_error`;
- concepts and constrained templates;
- `consteval` and `constexpr` validation;
- ranges where they improve clarity without hidden allocation;
- standard vocabulary types such as `span`, `string_view`, and `chrono`;
- attributes and compile-time facilities that improve diagnostics and safety.

Solar should track supported Zephyr and Zephyr SDK releases rather than remain
permanently constrained by older toolchains. A future standard transition,
including C++26 when mature in the Zephyr ecosystem, is expected policy rather
than an exceptional rewrite.

Modern language use must still satisfy embedded constraints. Newer syntax is
not permission for hidden allocation, exceptions in normal control flow,
unbounded work, or opaque template diagnostics.

## 12. Public API Rules

- The common path should be concise.
- Public APIs should use subsystem namespaces rather than expose internal
  facility types.
- Familiar direct component calls remain normal for intentional dependencies.
- Global subsystem APIs must not require a `System` object or context object.
- The active system type remains explicitly available for tests and
  metaprogramming.
- Fallible operations should return `Result` or a deliberately documented
  status-only result.
- Fire-and-forget APIs should have a fallible `try_*` counterpart when failure
  can matter.
- APIs callable from ISR context must be explicitly identified and constrained.
- Capacity and overflow behavior must be visible in types, Kconfig, descriptors,
  or documentation rather than hidden.
- Disabled-feature behavior must be deterministic and documented.

## 13. Kconfig And Type Policy Rules

Kconfig should select:

- Zephyr capabilities;
- subsystem inclusion defaults;
- memory and capacity defaults;
- diagnostics support;
- platform/build behavior;
- default scalar policies.

C++ types should select:

- application composition;
- identity and ownership;
- typed behavior;
- execution and delivery strategy;
- explicit local policy overrides;
- schema and contribution declarations.

Runtime parameters should hold values intentionally adjustable after boot.

Policy precedence is always:

```text
declaration > blueprint subsystem policy > Kconfig default
```

## 14. Inspection Rules

- Compile-time metadata belongs in immutable descriptors and catalogs.
- Runtime facts belong in focused records owned by their subsystem.
- Queries should return bounded copies or explicit immutable views.
- Inspection may filter, page, and format but must not own or rewrite facts.
- Unavailable, disabled, unsupported, stale, and failed are distinct states.
- A universal all-system runtime snapshot is prohibited.

## 15. Design Specification Conventions

Each later specification must include, where applicable:

1. purpose and non-goals;
2. terminology and semantic boundaries;
3. normal public API examples;
4. compile-time declarations and concepts;
5. runtime ownership and bounded storage;
6. lifecycle and initialization;
7. concurrency and ISR behavior;
8. result and error behavior;
9. capacity, overflow, and backpressure;
10. Kconfig/type-policy boundary;
11. blueprint, binding, contribution, and catalog interaction;
12. Inspection and Remote exposure boundaries;
13. identity and metadata;
14. validation and expected diagnostics;
15. migration from current Solar;
16. implementation test obligations;
17. deferred capabilities;
18. rejected alternatives;
19. open questions and final decisions.

### 15.1 Decision states

- **proposed**: actively under discussion;
- **accepted**: normative for later specifications;
- **experimental**: requires a bounded feasibility prototype;
- **deferred**: intentionally excluded while preserving a stated extension
  constraint;
- **rejected**: considered and declined with rationale;
- **superseded**: replaced by a named later decision.

### 15.2 Deferred items

A deferred item must state:

- what is deferred;
- why it is deferred;
- which extension constraint is preserved;
- what current behavior is not promised.

Deferred must not mean vaguely supported.

### 15.3 Examples

Examples must be realistic enough to reveal:

- include direction;
- declaration verbosity;
- ownership;
- disabled-feature behavior;
- error handling;
- lifecycle interaction;
- policy precedence.

Examples are normative for API shape once their containing spec is accepted.

## 16. Rejected Vocabulary And Models

### 16.1 System context or access facade

Rejected. Components use direct includes and global subsystem APIs. Solar does
not pass a system object, context, `Use<System>`, or service locator.

### 16.2 Object-container System

Rejected. Component types own their state; Solar owns only coordination and
subsystem-specific runtime infrastructure.

### 16.3 Runtime registry as the primary architecture

Rejected. Static catalogs provide membership and validation. Runtime registries
are reserved for a future requirement that genuinely needs dynamic membership.

### 16.4 Name-based dependency

Rejected. Component dependencies use concrete types. Names remain metadata.

### 16.5 Automatic external exposure

Rejected. Contribution and registration do not imply Remote access.

### 16.6 One execution stack per task

Rejected as a default. Executors are explicit shareable components; dedicated
execution remains an explicit policy.

### 16.7 Custom Result discriminated union

Rejected for the new baseline. `std::expected` is the canonical representation;
Solar provides aliases, concepts, and free helpers rather than wrapping it in a
competing value type.

## 17. Compact Architectural Rules

1. Types identify; descriptors describe; records change.
2. Blueprints declare; systems validate and coordinate.
3. Bindings select; they do not own.
4. Dependencies order; direct C++ calls access.
5. Contributions declare ownership; catalogs collect it.
6. Registration does not imply lifecycle, subscription, or exposure.
7. Namespace APIs are public surfaces, not runtime owners.
8. Facilities own passive capability; services own sustained execution.
9. Task behaviors describe work; executors own where it runs.
10. Leaf registrations are hosted and inspected without becoming components.
11. Bus events coordinate behavior; observability events record facts; logs
    explain.
12. Parameters are identifiable runtime configuration, not arbitrary global
    state.
13. Policies govern one narrow behavior and follow fixed precedence.
14. Optional cross-subsystem behavior is explicit or compiled away.
15. Every mutable fact has one canonical owner.
16. Runtime storage is bounded and allocation-free by default.
17. External identity is explicit and versioned.
18. Inspection consumes facts and never becomes their owner.
19. Solar uses modern standard C++ deliberately and tracks the supported Zephyr
    toolchain ecosystem.

## 18. Decisions Locked By This Specification

- Facilities remain first-class lifecycle components and may be user-defined.
- Built-in facilities are normally selected through Kconfig/derived use and are
  included visibly in the effective blueprint.
- Disabled subsystems have no runtime ownership cost.
- Required, optional, and adapter-driven cross-subsystem relationships are
  distinct.
- Executors are components.
- Task behaviors and execution registrations are not components by default.
- `solar::execution` is the canonical execution subsystem namespace.
- Solar should provide an ergonomic shared worker for common work.
- Persistent and wire identities use explicit stable IDs.
- `solar::parameters` is the canonical parameter namespace.
- Policy precedence is declaration, blueprint, then Kconfig.
- Solar's required language baseline is C++23.
- `solar::Result<T, E>` is based on `std::expected<T, E>`.
- Solar may rely on the complete C++23 `std::expected` monadic interface.
- Typed subsystem errors remain rich until explicitly mapped to `Status`.
- Contributions, dependencies, subscriptions, exposures, and registrations are
  separate relationships.
- Every mutable runtime fact has one canonical owner.

## 19. Open Questions

No Phase 0 question remains open.

The concrete mechanics intentionally passed to later phases are:

- blueprint syntax and automatic built-in composition;
- binding implementation and include-direction proof;
- generic contribution representation;
- descriptor fields and manifest tooling;
- exact feature-availability traits;
- shared worker inclusion rules;
- subsystem-specific disabled behavior;
- subsystem-specific error enumerations.

## 20. References

- Zephyr C++ language support:
  `https://docs.zephyrproject.org/latest/develop/languages/cpp/index.html`
- Zephyr 4.4 release notes and SDK 1.0 toolchain update:
  `https://docs.zephyrproject.org/latest/releases/release-notes-4.4.html`
- Zephyr SDK releases:
  `https://github.com/zephyrproject-rtos/sdk-ng/releases`
- C++23 `std::expected`: ISO C++ standard library facility `<expected>`.
