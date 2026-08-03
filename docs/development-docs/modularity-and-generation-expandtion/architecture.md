# Modular Architecture And Static Application Ownership

Date: 2026-08-03

Status: accepted architecture; initial API locked by prototype evidence

## 1. Direction

Solar will become an explicitly layered framework:

```text
application behaviour
        |
solar::system                 optional composition and lifecycle
        |
integration adapters          remote/parameters, settings, metrics, logging
        |
standalone modules            parameters, events, bus, metrics, log, remote
        |
kernel and hardware           typed Zephyr abstractions
        |
core                          Result, identity, catalogs, fixed containers
        |
Zephyr
```

`solar::core`, `solar::kernel`, and `solar::hardware` must remain independently
usable. Higher-level modules will be reworked so their core storage and
operations can also be used without a Solar System.

`solar::system` is the optional module that composes the other modules, derives
the component graph and catalogs, validates connections, and orchestrates
lifecycle.

## 2. One Application And One Static System

Normal firmware has one application and one System. Both remain type-level
entities. The redesign does not create an ordinary System object.

Solar will not require code such as:

```cpp
controller.update(system.parameters(), system.metrics());
```

or pass an application context through every lifecycle callback and service.

Instead, an application declares a type identity and canonical module types.
The proposed convention is:

```text
app/app.hpp          application identity and canonical module ownership
app/system.hpp       components, adapters, connections, and System composition
services/*.hpp       handwritten application behaviour
main.cpp             boots app::system
```

Illustrative `app.hpp`:

```cpp
namespace app
{

struct Application;

using parameters =
    solar::parameters::StaticStore<Application, generated::ParameterSchema>;

using events =
    solar::events::StaticHub<Application, generated::EventSchema>;

using metrics =
    solar::metrics::StaticRegistry<Application, generated::MetricSchema>;

} // namespace app
```

`Application` is an identity tag, not a state bag that must be instantiated.
Each module remains the owner of its own state.

Illustrative `system.hpp`:

```cpp
namespace app
{

using system = solar::System<solar::Compose<
    generated::Project,
    solar::Own<parameters, events, metrics>,
    solar::Devices<Imu, Lidar>,
    solar::Facilities<Navigation>,
    solar::Services<Cockpit>>>;

} // namespace app
```

Boot is explicit and compile-time resolved:

```cpp
int main()
{
    return app::system::boot() ? 0 : 1;
}
```

There is no application-binding registration step in this model.

## 3. One Implementation, Two Ownership Forms

Standalone object access and application-owned typed access are both required,
but they must not become two independent implementations.

### 3.1 Standalone object

The object form is the module's fundamental implementation:

```cpp
using Schema = solar::parameters::Schema<DriveKp, DriveKi>;

solar::parameters::Store<Schema> parameters;

parameters.initialize();
parameters.set<DriveKp>(1.25F);
```

It supports:

- small applications that use no Solar System;
- unit tests and independent fixtures;
- multiple intentionally separate instances;
- reusable libraries; and
- explicit ownership when static application ownership is inappropriate.

Object ownership does not imply dynamic allocation. Stores can be statically
allocated, non-moving, and entirely bounded.

### 3.2 Typed static facade

The normal firmware form is a static type facade over exactly one object:

```cpp
struct Application;

using parameters =
    solar::parameters::StaticStore<Application, Schema>;

parameters::set<DriveKp>(1.25F);
```

Conceptually, the facade contains one inline static object and forwards to it:

```cpp
template <typename Owner, typename Schema>
struct StaticStore
{
    inline static Store<Schema> storage{};

    template <typename Parameter>
    static auto get() noexcept
    {
        return storage.template get<Parameter>();
    }
};
```

This is a single operation model with two ownership forms. Results,
validation, locking, persistence integration, and state transitions must not
differ between them.

## 4. Standalone Module Contract

Each higher-level module will be separated into:

1. declarations and schema;
2. explicitly owned storage/runtime;
3. a narrow typed API operating on that owner; and
4. optional integration adapters.

Lifecycle gates, scheduling, persistence, logging, inspection, and Remote
exposure must not be unconditional dependencies of the core module.

For example, the parameter store should not require the System lifecycle engine
to read a value. System integration may install a run-state gate around an
operation, but that gate is an adapter or injected policy.

The eventual module contract must let a third-party module contribute:

- stable module identity;
- schema and catalog kinds;
- owned storage;
- optional lifecycle hooks;
- dependencies;
- inspection and generated metadata providers; and
- supported connection adapters.

The precise concept or traits API will be selected through prototypes.

## 5. Generic System Composition

System must stop knowing every Solar subsystem by name. It will fold over
generic module and component contributions.

`Own<Module...>` means that this System:

- adopts each module as a canonical application member;
- validates that ownership is unambiguous;
- initializes, starts, stops, and deinitializes it when supported;
- includes its catalogs and inspection providers; and
- gives it the System services supplied by explicit adapters.

It does not require System to contain an ordinary C++ member object.

Connections instantiate named integrations. The preferred prototype starts
with an explicit adapter:

```cpp
solar::Connect<
    solar::parameters::SettingsPersistence,
    app::parameters,
    app::settings>
```

```cpp
solar::Connect<
    solar::remote::ExposeParameters,
    app::remote,
    app::parameters>
```

The conceptual form is `Connect<Adapter, Endpoint...>`. An automatic
`Connect<A, B>` shorthand may be considered only if adapter selection is
unambiguous and diagnostics remain clear.

## 6. Generated Existence And C++ Participation

Application schemas are complete before System composition. IDL declares that
parameters, events, metrics, types, actions, topics, and streams exist.

C++ components declare how they participate:

```cpp
struct DriveController
{
    using Parameters = app::parameters::Uses<generated::DriveKp>;
    using Streams = app::remote::Consumes<generated::DriveCommand>;
    using Metrics = app::metrics::Updates<generated::DriveCommands>;

    static Result<void> apply(const generated::DriveCommand& command);
};
```

The exact contribution names remain provisional. The semantic separation is
accepted.

Different declaration kinds require different participation rules:

| Declaration | Module responsibility | Component roles |
| --- | --- | --- |
| Parameter | value, revision, validation, transactions | read, write, validate, observe |
| Action | protocol registration and request state | exactly one handler |
| Output stream | session, policy, flow control | source or publisher |
| Input stream | transport and credit state | consumer/controller |
| Event | delivery and retained event state | emitter, observer, processor |
| Metric | metric value and aggregation | update and observe |
| Topic | fan-out and subscription state | publisher and subscriber |

System will validate cardinality, missing handlers, incompatible roles,
undeclared references, missing connections, and identity collisions.

## 7. Service Access Styles

Application-specific services should use explicit application module types:

```cpp
auto gain = app::parameters::get<generated::DriveKp>();
```

This retains Solar's concise static style while naming the owner.

A reusable typed service may accept module types at compile time:

```cpp
template <typename Parameters, typename Metrics>
struct DriveController
{
    static Result<void> update(float gain)
    {
        auto result = Parameters::template set<generated::DriveKp>(gain);
        if (!result) {
            return fail(result.error());
        }
        return Metrics::template inc<generated::DriveUpdates>();
    }
};

using Drive = DriveController<app::parameters, app::metrics>;
```

This is compile-time dependency injection, not runtime context propagation.

Ordinary libraries remain free to accept `Store&` objects. Solar will not force
standalone C++ libraries to adopt application static ownership.

## 8. Binding Removal

The strict and relaxed global binding modes will eventually be removed.

The replacement has one semantic path:

- direct object access for standalone modules;
- direct typed access through an explicitly named application module;
- direct static System boot and lifecycle calls; and
- explicit dynamic lookup only for Remote, inspection, and host tooling.

There will be no framework-wide choice between direct calls and runtime
function-pointer slots. Migration compatibility facades may exist temporarily,
but they are not part of the target architecture.

## 9. Packaging

Solar will expose meaningful package and build targets rather than only one
monolithic target. Expected target families include:

```text
solar::core
solar::kernel
solar::hardware
solar::parameters
solar::events
solar::metrics
solar::remote
solar::system
```

The initial implementation remains in one repository. Repository splitting is
not required to establish genuine module boundaries.

Kconfig controls whether implementations are available and sets hard platform
limits. The project manifest and C++ composition decide which modules belong to
an application.

## 10. High-Level Application Compilation

Normal applications will use a progressive-disclosure layer centered on
`solar::System<Application>`. The Application type declares platform, devices,
services, and deliberate policy overrides. Generated traits supply the authored
contract, while an Application compiler synthesizes the same `Own`, `Connect`,
`Compose`, and `Dispatch` model described above.

Kconfig and CMake may automatically discover and generate the conventional
application artifacts, but they do not infer service ownership or form a second
composition language. The full accepted boundary, target API, override model,
and prototype gates are defined in
[High-level applications and Kconfig integration](application-ergonomics-and-kconfig.md).

## 10. Non-Goals

This expansion will not:

- replace typed C++ application behaviour with YAML;
- move Solar service policy into devicetree;
- introduce a heap-dependent runtime component registry;
- use linker sections as the primary application composition mechanism;
- require multiple Systems in normal firmware;
- make a System/context object flow through application code; or
- immediately split Solar into many repositories.
