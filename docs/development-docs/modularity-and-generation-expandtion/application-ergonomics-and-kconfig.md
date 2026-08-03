# High-Level Applications And Kconfig Integration

Date: 2026-08-03

Status: accepted direction; API spelling requires a focused prototype

## 1. Decision

Solar will add a high-level Application layer over the production module and
composition architecture. This is progressive disclosure over one semantic
model, not a second runtime, binding mode, or compatibility facade.

The public layers are:

| Layer | Purpose |
| --- | --- |
| Standalone modules | Explicitly owned facilities such as `Store`, `Server`, and `StaticLogger` |
| Composition | Exact control through `Own`, `Connect`, `Compose`, and `Dispatch` |
| Application | Normal firmware construction through devices, services, platform, policies, and `System<Application>` |

The Application layer must expand at compile time into the same Composition
types an advanced user can write directly. There is one lifecycle engine, one
ownership model, one endpoint dispatcher, and one resulting static System.

## 2. Canonical Application Experience

The intended normal shape is approximately:

```cpp
namespace app
{

struct Application
{
    using Platform = board::Platform;

    using Devices = solar::Devices<
        devices::Lidar,
        devices::LeftMotor,
        devices::RightMotor,
        devices::SweeperServo,
        devices::GateServo,
        devices::CarriageServo,
        devices::Electromagnets>;

    using Services = solar::Services<
        solar::Run<services::Cockpit,
                   solar::Stack<3072>,
                   solar::PreemptivePriority<2>>>;

    using Configuration = solar::Configure<
        solar::Logging<solar::Retain<24>>,
        solar::Remote<>>;
};

using System = solar::System<Application>;

} // namespace app
```

The generated application integration supplies the contract, parameter schema,
Remote schema, manifest contribution, and shipment identity by default. The
handwritten application states only decisions it owns: platform, devices,
services, execution policy, integrations, and deliberate overrides.

The boot path remains explicit:

```cpp
int main()
{
    return app::System::boot() ? 0 : -1;
}
```

An advanced application may inspect or use the desugared form:

```cpp
using Composition = solar::application::Compose<Application>;
using System = solar::system::System<Application, Composition>;
```

It may override one synthesized module without replacing the rest:

```cpp
using Configuration = solar::Configure<
    solar::UseLogging<MyLogger>,
    solar::Remote<solar::FrameBytes<2048>>>;
```

Defaults and overrides must converge on the same normalized application
specification before composition. Unsupported combinations fail at compile
time with diagnostics expressed in authored application terms.

## 3. Endpoint Binding

IDL declares that an endpoint exists. A handwritten service explicitly claims
its behavior through one binding table. The table replaces both a separate
participation list and forwarding overloads whose only purpose is adapting
generated tags.

The preferred shape to prototype is:

```cpp
struct Cockpit
{
    static State state() noexcept;
    static Result<void> set_mode(OperatingMode) noexcept;
    static Result<void> drive_tank(const TankCommand&) noexcept;

    using Endpoints = solar::Endpoints<
        contract::cockpit::Mode::ReadWrite<&mode, &set_mode>,
        contract::cockpit::State::Read<&state>,
        contract::cockpit::drive::Tank::Input<
            &drive_tank, &open_tank, &close_drive>>;
};
```

From this table Solar derives contribution roles, cardinality, signatures,
dispatch, Remote adapters, and open/close support. Matching behavior from
method names alone is rejected: it is ambiguous, makes refactoring fragile,
and weakens diagnostics. Binding remains explicit; duplicate mechanical glue
does not.

Generated C++ names should be hierarchical and autocomplete-friendly, for
example:

```text
app::contract::cockpit::types::State
app::contract::cockpit::data::State
app::contract::cockpit::drive::Motor
app::contract::cockpit::drive::Differential
```

Applications should not need a handwritten header whose only job is shortening
generated names.

## 4. Synthesized Application Facilities

The Application compiler performs a deterministic, inspectable expansion.

### 4.1 Devices

`solar::Devices<A, B>` accepts modules directly and automatically applies a
standard `AsModule<Device>` lifecycle adapter to conventional devices. An
explicit adapter/policy remains available for unusual lifecycle or error
translation. Applications do not rewrite identical `DeviceModule` wrappers.

### 4.2 Services and execution

`solar::Services<Cockpit>` uses documented execution defaults.
`solar::Run<Cockpit, Stack<...>, Priority<...>>` overrides them locally.
Dependencies remain declared by the service or its hardware boundary and are
validated after all devices and services have been collected.

### 4.3 Platform

The platform owns target variation: console, Remote links, platform devices,
and target-specific integration types. Application composition should not
repeat Teensy/native `#if` branches and nearly identical ownership lists.
Devicetree generation may help construct a platform, but C++ remains the place
for application-level platform policy.

### 4.4 Logging

Normal applications receive a compile-time facade such as:

```cpp
using log = solar::log::For<Application>;
```

The source catalog is derived from the normalized devices, services, modules,
and explicit extra sources. Built-in domains do not require enumeration.
Standalone logging retains its explicit source/domain form. No runtime logger
registration or nullable global frontend is introduced.

### 4.5 Parameters and Remote

The generated contract synthesizes the canonical parameter store and Remote
contract when their integrations are enabled. An empty parameter schema is
elided. The Remote runtime derives its schemas and endpoints from the generated
contract, its handlers from service bindings, and its links from the platform.
Users do not normally spell `GeneratedRemote`, `RemoteArchitecture`, manifest
emission, or an empty parameter store.

## 5. Configuration Ownership

Solar has four declarative inputs. They must not duplicate one another:

| Input | Owns |
| --- | --- |
| Project manifest and IDL | Application identity, interface types, endpoints, stable IDs, bounds, compatibility, host packages |
| C++ Application | Behavior ownership, devices, services, dependencies, custom adapters, deliberate policy overrides |
| Devicetree | Physical hardware instances, buses, pins, addresses, and board wiring |
| Kconfig | Compiled feature availability, automatic integration policy, bounded resources, diagnostics, and target build choices |

CMake is the deterministic join point. It discovers inputs, invokes generation,
publishes generated include directories, adds dependencies, and verifies the
linked result. CMake is not another authored application schema.

Kconfig must not redeclare endpoint names, schema fields, stable identities,
service ownership, or C++ type names. The project manifest must not silently
select Kconfig features: Zephyr resolves Kconfig before normal application
generation, and hidden reverse configuration would be difficult to inspect.
Instead, generated requirements are validated against the resolved Kconfig.

## 6. Default-On Kconfig Integration

Solar's Zephyr module will provide a default-on automatic integration policy.
When `solar.project.yaml` exists at the application root, the module should be
able to perform the normal generation pipeline without application-level
`solar_generate_contract()` or `solar_generate_shipment()` calls.

The proposed symbols are:

```text
CONFIG_SOLAR_APPLICATION_AUTO_INTEGRATION=y
CONFIG_SOLAR_APPLICATION_PROJECT="solar.project.yaml"
CONFIG_SOLAR_APPLICATION_GENERATE_PYTHON=y       if SOLAR_REMOTE
CONFIG_SOLAR_APPLICATION_GENERATE_SHIPMENT=y     if SOLAR_REMOTE
CONFIG_SOLAR_APPLICATION_REQUIRE_LOCK=y
CONFIG_SOLAR_APPLICATION_EXPLAIN=y
```

Exact names require a Kconfig/CMake prototype, but the semantics are accepted:

- auto integration defaults on and is a no-op when the conventional project
  manifest is absent, preserving standalone module use;
- the project path is relative to `APPLICATION_SOURCE_DIR` unless absolute;
- contract C++ generation and generated include publication are inseparable
  from auto integration rather than independent toggles that can create a
  half-generated application;
- Python and shipment outputs may be disabled for constrained development
  workflows without changing the firmware contract;
- a checked-in identity lock is required by default;
- updating the lock is never an implicit build option because builds must not
  silently mutate authored source;
- an explanation artifact is generated by default and has no firmware cost.

Turning auto integration off is an advanced build escape hatch, not another
Solar architecture. The existing explicit CMake functions then produce the
same IR, headers, manifest, shipment, and composition inputs.

## 7. Automatic Artifact Use

Automatic inclusion means automatic build availability and typed consumption,
not a compiler-wide forced include.

The default pipeline is:

```text
resolved Kconfig
      +
solar.project.yaml + IDL + identity lock
      |
      v
normalized IR and generated C++
      |
      +-- generated include directory attached to app target
      +-- generation dependency attached to app target
      +-- Python package and explanation artifacts
      |
      v
<solar/application.hpp>
      |
      +-- conditionally includes generated application integration
      +-- maps app::Application to its generated Project/Contract traits
      |
      v
solar::System<app::Application>
      |
      v
linked-ELF verification and shipment
```

The generator knows the authored application namespace and emits a trait for
the canonical application identity. This lets `System<Application>` obtain the
default generated contract without a handwritten alias. A nested explicit
contract or application customization has documented precedence.

Solar must not use `-include solar/generated/app.hpp` for every translation
unit. Forced inclusion pollutes unrelated C and standalone-module sources,
obscures dependencies, and produces poor IDE behavior. One stable high-level
include gives the convenience without hidden textual injection.

## 8. Validation And Anti-Drift Gates

Automatic integration is useful only if forgetting a step becomes impossible.
The build must validate:

- every manifest-required Solar feature is enabled by Kconfig;
- generated headers and the identity lock come from the same normalized IR;
- the high-level System consumes the expected generated contract;
- the linked manifest digest matches the generated interface digest;
- the shipment describes the exact linked ELF;
- disabled Python/shipment outputs do not change firmware declarations;
- manual and automatic CMake integration produce byte-identical contract
  artifacts; and
- Clangd receives the generated include directory and dependency outputs.

If an application manifest is detected and the configured high-level build
does not consume its generated integration, the build should fail rather than
quietly link a firmware image with a partial interface.

## 9. Explainability

The normal build emits a machine-readable and human-readable expansion report.
An equivalent command should be available:

```sh
solar-codegen explain application
```

It reports the generated contract, owned modules, adapted devices, services,
endpoint owners, execution policies, platform links, elided facilities,
connections, and the reason for each dependency edge. This is the primary
escape from high-level convenience becoming a black box.

## 10. Prototype Gates

Before final API spelling is locked, prototype the current robot application
and require:

1. `app.hpp` and `system.hpp` collapse to the high-level form without changing
   the resulting runtime architecture;
2. Cockpit uses one endpoint binding table with no participation or forwarding
   duplication;
3. normal CMake contains no explicit Solar generation calls;
4. auto and manual integration generate identical artifacts;
5. a missing lock, disabled required Kconfig feature, unhandled endpoint,
   duplicate owner, and missing platform link each produce source-located
   diagnostics;
6. native simulation and Teensy builds select different platforms without
   application-level conditional module lists;
7. all generated paths work in Clangd from a clean build; and
8. the expansion report accurately explains the final composition.

Line-count reduction is useful evidence, but clarity and diagnostic quality
are the acceptance criteria. The prototype must not hide safety policy or
service ownership merely to minimize code.
