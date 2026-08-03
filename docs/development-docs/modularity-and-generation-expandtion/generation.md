# Project Manifest, IDL, And Generation

Date: 2026-08-03

Status: accepted model; initial grammar and artifact flow validated

## 1. Objective

Generation is a first-class Solar capability, not incidental build glue.

Application authors declare data contracts once. Solar derives the repetitive
firmware declarations, storage schemas, wire metadata, host types,
documentation, and compatibility information from that declaration.

Handwritten C++ remains responsible for behaviour, algorithms, lifecycle
callbacks, and custom integration logic.

## 2. Authored Inputs

Solar uses two related authored inputs.

### 2.1 Project manifest

The project manifest identifies the application, imported interfaces, enabled
Solar modules, generation policy, and package metadata.

Illustrative syntax:

```yaml
solar: 1

application:
  name: robot
  namespace: app

interfaces:
  - interfaces/control.solar.yaml
  - interfaces/navigation.solar.yaml
  - interfaces/telemetry.solar.yaml

modules:
  parameters: {}
  events: {}
  metrics: {}
  remote:
    manifest: true
```

The project manifest does not initially replace the readable C++ component
graph. Devices, facilities, services, and executors remain in typed C++ unless
later evidence demonstrates a clear generation benefit.

### 2.2 Solar interface definitions

IDL files describe application-level contracts, including contracts that are
not remotely exposed.

Illustrative syntax:

```yaml
solar-interface: 1
package: robot.control

types:
  DriveCommand:
    fields:
      throttle: f32
      differential: f32

  Euler:
    fields:
      roll:
        type: f32
        unit: rad
      pitch:
        type: f32
        unit: rad
      yaw:
        type: f32
        unit: rad

parameters:
  drive.kp:
    type: f32
    default: 1.0
    constraints:
      minimum: 0.0
      maximum: 10.0
    remote: read-write

streams:
  drive.command:
    type: DriveCommand
    direction: in

  imu.euler:
    type: Euler
    direction: out
    frequencies:
      minimum: 1
      maximum: 100

actions:
  lidar.self-test:
    request: Empty
    response: DiagnosticReport
```

The concrete grammar and file extension will be selected through the minimal
compiler prototype. YAML is the leading project-manifest representation, but
the implementation must use strict parsing and must not inherit surprising
implicit scalar conversions.

## 3. Supported Contract Model

The initial IDL must model embedded constraints directly:

- booleans and explicitly sized integer and floating-point values;
- enumerations with explicit open or closed behaviour;
- structures with named fields;
- required and optional fields;
- bounded strings, byte strings, arrays, and collections;
- units, descriptions, defaults, and constraints;
- schema and endpoint versions;
- parameters, events, metrics, data, actions, topics, and streams;
- supported stream direction and frequency policy; and
- CBOR or bounded packed representation where supported.

Recursive value schemas and unbounded values are not part of the initial model.

The IDL must not embed arbitrary C++ expressions. Common constraints are
declarative. Unusual behaviour is attached through a named and typed C++
adapter or handler.

## 4. One Normalized Intermediate Representation

All generation backends consume one validated Solar IR:

```text
project manifest + IDL imports
              |
              v
    parse with source locations
              |
              v
 resolve names, IDs, versions, and types
              |
              v
       validate Solar IR
       /       |       \
      /        |        \
 C++ output  host output  docs and compatibility
```

Backends must not independently interpret source documents. This prevents C++,
Python, manifests, and documentation from disagreeing about numeric widths,
field optionality, defaults, or identity.

Every diagnostic should retain file, line, and declaration context from the
authored source.

The generator must be deterministic. Identical project inputs, configuration,
and tool versions produce byte-identical generated source and interface
artifacts.

## 5. Generated Firmware Artifacts

The generator may produce:

- C++ structures and enumerations;
- schema descriptor types;
- parameter, event, metric, data, action, topic, and stream declarations;
- aggregated module schemas;
- stable identity constants;
- codec tables or generated codecs;
- application module aliases;
- a generated project composition contribution;
- capacity summaries and compile-time checks; and
- manifest records consumed by the firmware build.

The generated project contribution supplies schemas, requirements, module
configuration, metadata, and high-level Application traits. It does not infer
behavior ownership. Services claim endpoints through explicit generated
binding tables; devices and services enter lifecycle ownership through the
handwritten Application specification. The Application compiler may synthesize
the corresponding low-level `Own<...>` composition, which remains available as
the advanced, inspectable form.

Generated headers live in the build output and are included through a stable
path, following the existing generated hardware-header model:

```cpp
#include <solar/generated/app.hpp>
```

The build must make these headers available to clangd and other IDE tooling.
Solar should also provide an explicit generation command for refreshing IDE
artifacts before a complete firmware link.

For Zephyr applications, default-on CMake/Kconfig integration discovers the
conventional project manifest, invokes this same generator, attaches the output
directory and dependency to the application target, and makes the generated
contract available through `<solar/application.hpp>`. This is automatic build
availability, not a compiler-wide forced include. Standalone module users with
no project manifest are unaffected. See
[High-level applications and Kconfig integration](application-ergonomics-and-kconfig.md).

Generated code must be readable enough to inspect while debugging. It is build
output and must be marked as generated rather than hand-edited.

## 6. Final Firmware Is The Effective Contract

The authored IDL is the source of declaration truth. The final linked firmware
is the source of truth for the capabilities actually present in one binary.

The pipeline is:

```text
IDL
 |
 v
generated C++ declarations
 |
 v
System composition + Kconfig + compilation
 |
 v
linked firmware ELF
 |
 v
effective embedded manifest
 |
 v
exact host artifacts and generated application client
```

This distinction matters because Kconfig, project composition, or a firmware
variant may omit an authored capability. A generated shipment client must not
advertise an endpoint absent from that shipment.

Solar's current final-ELF manifest extraction remains a useful foundation. The
new generator supplies the declaration side; the post-link stage verifies and
exports the exact effective side.

## 7. Stable Identity And Lock File

Solar will maintain an interface lock file, provisionally named:

```text
solar.interface.lock
```

It records assigned and retired identities, including:

- schema IDs;
- field IDs;
- parameter, event, metric, and endpoint IDs;
- versions;
- reserved removed IDs; and
- rename history where required.

The lock file prevents a source rename from silently becoming a new persistence
or wire identity. Authors may explicitly accept a breaking identity change.

Compatibility tooling will classify at least:

- wire-compatible changes;
- generated-source changes;
- behavioural or policy changes; and
- breaking changes.

The generator must reject ID reuse, collisions, incompatible field changes,
invalid default values, and values that violate declared bounds.

## 8. Generated Shipment Artifacts

A firmware build should be able to emit a cohesive shipment directory:

```text
build/solar/
|-- interface/
|   |-- manifest.bin
|   |-- manifest.cbor
|   |-- manifest.json
|   |-- manifest.sha256
|   `-- compatibility.json
|-- python/
|   |-- robot_solar/
|   `-- robot_solar_client-0.4.0-py3-none-any.whl
|-- cpp/
|   `-- app.generated.hpp
|-- docs/
|   `-- interface.md
`-- shipment.json
```

The shipment associates firmware, build identity, interface identity,
generated client, and human-readable reference material.

Protocol identity, interface identity, and build identity remain distinct. A
new firmware build may retain the same exact interface.

## 9. Build Integration

The generation stage must:

- run before any C++ translation unit requiring generated declarations;
- track imported IDL files as build dependencies;
- emit a depfile or equivalent dependency information;
- fail configuration or compilation with clear source-located diagnostics;
- place all normal artifacts under the build directory;
- expose generated includes to compile commands and IDE tooling; and
- support both Zephyr and host-side tests.

The post-link stage must:

- extract or verify the effective embedded manifest;
- generate exact host artifacts from that manifest;
- calculate deterministic digests;
- compare against previous interface locks or shipment manifests when asked;
  and
- fail when generated declaration metadata disagrees with the linked binary.

## 10. Third-Party Modules

The long-term IR must permit module-specific declaration and metadata kinds
without requiring System to know every module by name.

The initial generator may implement only first-party Solar schema kinds. A
general Python plugin system is explicitly deferred until the IR and module
contract are stable.

Third-party extensibility must eventually be based on a documented module and
IR extension contract, not arbitrary mutation of generator internals.

## 11. Non-Goals

Generation will not:

- replace application algorithms or lifecycle behaviour with YAML;
- duplicate devicetree hardware topology;
- independently infer an exact shipment interface before the firmware build;
- make generated Python code implement the wire protocol;
- permit unbounded data merely because the host language supports it; or
- require generated files to become manually maintained source.
