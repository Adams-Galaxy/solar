# Solar Modularity And Generation Implementation Plan

Date: 2026-08-03

Status: complete; phases 0-10 verified

## 1. Purpose And Authority

This document is the implementation script for Solar's modularity and
generation redesign. It converts the accepted architecture and prototype
evidence into ordered, independently verifiable work.

When code, an older checklist, or an informal idea conflicts with this plan,
stop and resolve the conflict in the design documents before continuing. Do not
silently bend the architecture to make a local change easier.

The redesign deliberately has no source-compatibility target. Temporary seams
are allowed only inside a named migration phase, with an owner and deletion
gate recorded here. They must not become a second supported architecture.

## 2. How To Use This Plan

Each phase has four kinds of entries:

- **Work** describes the required implementation.
- **Evidence** names the tests or artifacts that prove the work.
- **Exit gate** is the condition that permits dependent phases to begin.
- **Drift guards** identify shortcuts that would violate the target design.

A checkbox may be marked complete only when its evidence is recorded in the
phase's evidence record. Compilation alone does not prove runtime behavior,
negative diagnostics, deterministic generation, or shipment compatibility.

Use these progress states in the dashboard:

- `not started`: no production work has begun;
- `active`: this is the current gated phase;
- `blocked`: an external decision or defect prevents its exit gate;
- `verified`: every exit condition has objective evidence; and
- `superseded`: the plan was deliberately amended before work continued.

The normal sequence is phase order. Independent documentation, test-fixture,
and host-tool work may run ahead only when it does not establish an API owned by
an unfinished prerequisite.

### 2.1 Change control

Before changing an accepted invariant or public shape:

1. write the motivating evidence and affected phases in the relevant design
   document;
2. update this plan, including deletion and test implications;
3. record the decision in the Decision Log; and
4. only then change production code.

Small naming improvements do not need ceremony when they preserve semantics.
Architectural changes do.

### 2.2 Evidence record format

Every verified phase must append a short record containing:

```text
Date:
Commit:
Commands:
Artifacts:
Known limitations:
```

The commit must be reproducible. Known limitations must be assigned to a later
phase or explicitly accepted; they may not remain as unowned TODOs.

## 3. Non-Negotiable Invariants

These constraints apply to every phase:

1. Solar is layered. Lower layers never include or depend on higher layers.
2. `core`, `kernel`, and `hardware` remain useful without System or Remote.
3. Higher-level modules have an ordinary owned implementation and an optional
   typed static facade over that same implementation.
4. Normal firmware has one application identity and one public static System.
   There is no runtime System/context object passed through services.
5. System is an optional generic composer. It does not branch on built-in
   module names.
6. Generated declarations establish existence before C++ composition.
   Handwritten components claim participation and provide behavior.
7. Component ownership and endpoint dispatch are resolved and validated at
   compile time. Dynamic lookup is reserved for genuinely dynamic boundaries
   such as Remote and inspection.
8. All generators consume one validated, normalized IR. Backends do not parse
   authored YAML independently.
9. The linked ELF is authoritative for the effective firmware interface and
   shipment identity.
10. Firmware paths are bounded and allocation-free by default. Limits are
    explicit configuration, not hidden growth.
11. The target contains one binding model. Legacy strict/relaxed bindings,
    nullable runtime frontend slots, and global application binding disappear.
12. Remote is a standalone module connected through explicit adapters. It is
    not a privileged System subclass or an implicit owner of other modules.
13. An authored stream becomes a genuine stream endpoint. It is not
    permanently disguised as a polled Data endpoint.
14. Python transport/session behavior lives in `solar_remote`; generated
    application packages provide typed application APIs; Station remains an
    external consumer and connection owner.

## 4. Target Dependency Shape

```text
generated application package / handwritten application
                         |
                solar::system (optional)
                         |
            explicit integration adapters
                         |
 parameters  events  metrics  logging  remote  persistence  execution
                         |
                  kernel / hardware
                         |
                       core
                         |
                      Zephyr
```

Permitted dependency direction is downward only. Integration between sibling
modules belongs in a separate adapter target that depends on both.

Canonical application files are expected to settle around:

```text
solar.project.yaml                 project and shipment configuration
schemas/*.solar.yaml               authored contracts
src/app/app.hpp                    application identity and module aliases
src/app/system.hpp                 composition and connections
src/services/*.hpp                 handwritten behavior and contributions
build/generated/solar/types.hpp    dependency-light generated types
build/generated/solar/contract.hpp generated endpoint declarations
build/generated/solar/*.hpp        module-specific declarations/adapters
build/generated/solar/app.hpp      generated application facade
```

The intended access shape remains:

```cpp
namespace app {

struct Application;

using parameters = solar::parameters::StaticStore<
    Application,
    generated::ParameterSchema>;

using system = solar::System<solar::Compose<
    generated::Project,
    solar::Own<parameters>,
    solar::Components<DriveController>,
    solar::Connect<solar::remote::ExposeParameters, remote, parameters>>>;

} // namespace app
```

This example fixes the ownership model, not every final spelling. A spelling
change must preserve the invariants and be recorded before broad rollout.

## 5. Progress Dashboard

| Phase | Scope | State | Depends on | Evidence record |
| --- | --- | --- | --- | --- |
| 0 | Baseline and prototype hardening | verified | none | [baseline](phase-0-baseline.md) |
| 1 | Compiler, normalized IR, and build integration | verified | 0 | below |
| 2 | Module foundation and ownership forms | verified | 1 | below |
| 3 | Production parameters module | verified | 2 | below |
| 4 | Generic System composer | verified | 2, 3 | below |
| 5 | Contributions and compile-time dispatch | verified | 4 | below |
| 6 | Standalone Remote and endpoint runtime | verified | 1, 2, 5 | below |
| 7 | Effective manifests, shipments, and Python clients | verified | 1, 6 | below |
| 8 | Remaining standalone modules and adapters | verified | 2, 4 | below |
| 9 | Real application migration and hardware validation | verified | 3-8 | below |
| 10 | Legacy deletion, hardening, and release | verified | 9 | below |

Only this table records phase status. Detailed checkboxes record work, not
overall readiness.

## 6. Phase 0 — Baseline And Prototype Hardening

### Objective

Turn the prototype into trustworthy evidence, freeze a reproducible baseline,
and identify exactly which prototype pieces are discarded, promoted, or
rewritten.

### Work

- [x] Capture the supported host, Zephyr, compiler, and Python versions.
- [x] Run the complete supported host suite and record its actual result.
- [x] Run the prototype generator, compile-fail suite, generated-client tests,
      `native_sim` integration, and reconnect test from a clean build.
- [x] Fix the identity allocator so explicit IDs fail on collision with both
      active and retired IDs instead of probing to a different ID.
- [x] Add negative coverage for duplicate explicit IDs, retired-ID reuse,
      invalid rename chains, illegal narrowing, malformed bounds, and
      incompatible format-version changes.
- [x] Reconcile `prototype-evaluation.md` with current evidence; remove stale
      claims about failures that have since been fixed.
- [x] Inventory every prototype artifact and classify it as `promote`,
      `rewrite`, `test-only`, or `delete`.
- [x] Record baseline flash, RAM, generation-time, and host-test-time numbers.
- [x] Freeze one representative schema used throughout the production phases:
      bounded types, enum, parameter, action, input stream, output stream, and
      an additive schema revision.

### Evidence

- All supported existing and prototype tests are green.
- Every rejected input fails with a stable, source-located diagnostic.
- Two clean generator runs produce byte-identical outputs.
- The inventory accounts for every file below the prototype namespaces and
  tools directories.
- Baseline measurements and commands are recorded below this phase.

### Exit gate

Phase 0 is verified only when no known prototype correctness defect can corrupt
identity, compatibility, or generated output. Prototype APIs may still be
temporary; their evidence must be reliable.

### Drift guards

- Do not promote the prototype's nullable generated callback slots.
- Do not treat the legacy Remote adapter in the native fixture as the target.
- Do not create compatibility aliases from prototype namespaces to production.
- Do not mark this phase verified while the allocator collision defect remains.

### Evidence record

Date: 2026-08-03

Commit: base `854a12436a819175a95b2f16398360647dc36b13`; Phase 0 changes are
part of the active expansion worktree.

Commands: clean GCC 14 CMake/CTest in Docker; direct 17-case compiler suite;
Zephyr 4.4 clean `native_sim/native/64` build in the project simulator container;
generated Python TCP exercise before and after simulator process restart.

Artifacts: [Phase 0 baseline and disposition](phase-0-baseline.md), generated
shipment, final Zephyr ELF, compatibility report, and compiler lock fixture.

Known limitations: AppleClang 16 cannot build the legacy framework. Clang
support is required before Phase 10 release, but does not redefine the supported
GCC baseline.

## 7. Phase 1 — Compiler, Normalized IR, And Build Integration

### Objective

Build the one production compiler pipeline on which C++, manifests, and Python
generation can safely depend.

### Work

#### Frontend and diagnostics

- [x] Establish a versioned `solar` generator command with documented CLI
      inputs, outputs, depfiles, and exit behavior.
- [x] Split authored-file loading, syntax validation, semantic validation,
      identity allocation, compatibility analysis, and backends into explicit
      stages.
- [x] Define and validate `solar.project.yaml` and `*.solar.yaml` formats.
- [x] Preserve source file, line, column, and declaration path through the IR.
- [x] Reject unknown keys and ambiguous coercions; do not silently normalize
      misspellings.

#### Identity and evolution

- [x] Give all wire-visible declarations stable explicit or lock-managed IDs.
- [x] Define global and scoped collision domains.
- [x] Normalize rename and retirement history before allocating new IDs.
- [x] Prevent reuse of retired identities.
- [x] Version the authored format, normalized IR, lock file, effective manifest,
      and shipment format independently.
- [x] Define additive and breaking changes for every declaration kind.
- [x] Produce a machine-readable compatibility report.

#### Type model

- [x] Implement integers, floating point, boolean, string, bytes, enum, struct,
      optional, fixed array, and bounded sequence/string forms required by the
      representative schema.
- [x] Require explicit bounds for all variable-size firmware values.
- [x] Encode range/default/unit/documentation constraints in normalized form.
- [x] Detect recursive or unbounded layouts before code generation.

#### Build integration

- [x] Add a CMake entry point that generates into the build tree, exports
      include paths, and rebuilds only on declared input/tool changes.
- [x] Produce correct depfiles for imported schemas and project configuration.
- [x] Make generation work in host tests and Zephyr builds without source-tree
      writes.
- [x] Prevent two build directories or configurations from sharing generated
      mutable state.
- [x] Give language backends only normalized IR, never raw YAML.

### Evidence

- Golden tests cover every supported syntax and normalized IR form.
- Negative tests cover syntax, semantics, identities, bounds, imports, and
  compatibility with source-located messages.
- Clean and incremental Ninja builds demonstrate correct dependencies.
- Reordered authored declarations do not change stable identity or output.
- C++, manifest, and Python test backends observe the same normalized contract.
- Generator output is deterministic across clean directories.

### Exit gate

A normalized IR version can be declared stable enough for production backends.
No production backend parses YAML or invents identity independently.

### Drift guards

- Do not generate C++ directly from parser nodes.
- Do not encode wire IDs from declaration order.
- Do not store generated headers in the source tree.
- Do not let a backend repair invalid IR.

### Evidence record

Date: 2026-08-03

Commit: active expansion worktree based on `854a12436a819175a95b2f16398360647dc36b13`.

Commands: production generator test suite, clean-directory determinism tests,
CMake/Ninja host generation, and Zephyr `native_sim` generated build.

Artifacts: `tools/solar_codegen`, `tools/generate_solar.py`,
`cmake/SolarCodegen.cmake`, normalized IR, lock, compatibility report, depfile,
C++ headers, manifest, and Python package.

Known limitations: new declaration kinds beyond the representative schema are
owned by their module migration in Phase 8.

## 8. Phase 2 — Module Foundation And Ownership Forms

### Objective

Define the minimal common contracts that let modules work alone and participate
in System without introducing a universal runtime base class.

### Work

- [x] Define dependency-light module identity and compile-time catalog concepts
      in an appropriate core/system boundary.
- [x] Define optional lifecycle customization points for initialize, start,
      stop, and deinitialize with explicit error semantics.
- [x] Define the object-owner/static-facade convention and prove that a facade
      owns exactly one object for an `(Application, Module)` identity.
- [x] Define module capability and inspection providers without requiring them
      from every module.
- [x] Define fixed-capacity configuration and failure behavior for exhausted
      capacities.
- [x] Establish package targets and include boundaries for `core`, `kernel`,
      `hardware`, module libraries, adapters, and `system`.
- [x] Add at least one tiny third-party-style example module outside Solar's
      built-in module directory.
- [x] Document when to use an object, a typed static facade, and a System-owned
      facade.

### Evidence

- Object and static forms pass the same behavioral conformance suite.
- Symbol inspection proves one canonical static storage object.
- `core`, `kernel`, and `hardware` compile without System headers.
- The example external module composes without editing System internals.
- Dependency tests fail if a lower layer includes a forbidden higher layer.

### Exit gate

The module contract is sufficient to implement parameters without special
System knowledge and to compose the external example generically.

### Drift guards

- Do not add a virtual `Module` base or heap-backed registry.
- Do not add an application context argument to module operations.
- Do not duplicate object and static implementations.
- Do not require lifecycle, inspection, logging, or Remote merely to read or
  write standalone state.

### Evidence record

Date: 2026-08-03

Commit: active expansion worktree.

Commands: `solar.host.module.foundation`, clean GCC header/module build, and
package-target configuration.

Artifacts: `solar/module.hpp`, object/static owner test, external counter
module fixture, package aliases, and `module-ownership.md`.

Known limitations: Zephyr thread execution is an explicit execution adapter,
not part of the minimal module contract.

## 9. Phase 3 — Production Parameters Module

### Objective

Deliver the first complete standalone production module and use it to validate
the module contract under real constraints.

### Work

- [x] Implement `solar::parameters::Store<Schema>` as the canonical owner.
- [x] Implement `StaticStore<Application, Schema>` as a forwarding facade over
      exactly one store.
- [x] Generate the parameter schema and typed declaration keys from normalized
      IR.
- [x] Implement typed get/set, defaults, validation, revision tracking, and
      bounded transactions.
- [x] Define concurrency and atomicity guarantees for readers, writes, and
      transactions.
- [x] Keep persistence, lifecycle run-state gating, inspection, and Remote
      exposure in separate adapters.
- [x] Provide explicit dynamic lookup only for inspection/Remote adapters.
- [x] Produce clear compile-time diagnostics for a parameter absent from a
      schema and incompatible value types.
- [x] Migrate one production-quality fixture entirely to the new module.

### Evidence

- Unit tests run the identical cases against object and static ownership.
- Compile-fail tests cover absent keys, incorrect types, and invalid schemas.
- Concurrency tests prove documented atomicity and revision behavior.
- Fixed-capacity exhaustion and validation failures are deterministic.
- A standalone executable uses parameters without System or Remote.
- The migrated fixture contains no strict/relaxed binding calls.

### Exit gate

Parameters are production-capable standalone and through their static facade.
System integration requires only generic ownership plus explicit adapters.

### Drift guards

- Do not place codec or transport knowledge in `Store`.
- Do not make parameter access depend on a global bound System.
- Do not introduce separate strict and lenient APIs.

### Evidence record

Date: 2026-08-03

Commit: active expansion worktree.

Commands: `solar.host.parameters.store` plus three parameter compile-fail
fixtures under GCC 13.3.

Artifacts: standalone Store/StaticStore, atomic snapshot and transaction
revision tests, four-writer concurrency test, dynamic visitor, and versioned
persistence adapter round trip.

Known limitations: richer generated parameter value kinds are added when a
bounded firmware representation is selected; the production scalar contract
is complete.

## 10. Phase 4 — Generic System Composer

### Objective

Replace subsystem-aware composition with a generic static graph that owns
modules, validates dependencies, and orchestrates lifecycle.

### Work

- [x] Implement the production `Compose`, `Own`, component grouping, and
      explicit `Connect<Adapter, Endpoint...>` vocabulary.
- [x] Normalize composition declarations into one compile-time graph.
- [x] Detect duplicate owners, missing dependencies, invalid adapters, cycles,
      and conflicting identities with focused diagnostics.
- [x] Compute deterministic lifecycle order from declared dependencies.
- [x] Implement startup rollback and reverse-order shutdown semantics.
- [x] Fold catalogs and optional inspection providers without naming built-in
      modules.
- [x] Define how boot errors and partial lifecycle state are reported.
- [x] Compose the parameters module and the external example module.

### Evidence

- Compile-fail fixtures cover every graph validation rule.
- Lifecycle tests prove order, rollback, idempotent shutdown, and failure paths.
- A test module can be added without modifying composer source.
- Source inspection or a structural test confirms no parameter, Remote,
  logging, event, or metric-specific branches exist in System.
- The public System remains a type with static boot/lifecycle entry points; no
  runtime context propagation is introduced.

### Exit gate

The composer can own and orchestrate arbitrary conforming modules and adapters,
with diagnostics usable by application developers.

### Drift guards

- Do not special-case first-party module types.
- Do not infer a connection when adapter selection is ambiguous.
- Do not use linker registration as the primary composition mechanism.
- Do not turn `Application` into an instantiated state container.

### Evidence record

Date: 2026-08-03

Commit: active expansion worktree.

Commands: `solar.host.system.composer` and composer compile-fail suite.

Artifacts: deterministic graph sort, reverse lifecycle/connection rollback,
idempotent shutdown, lifecycle-stage reporting, generic metadata folding, and
duplicate owner/dependency/cycle/adapter/identity diagnostics.

Known limitations: none in the generic composer contract; service threads are
owned by the standalone execution module rather than special-cased here.

## 11. Phase 5 — Contributions And Compile-Time Dispatch

### Objective

Separate generated declaration existence from handwritten behavioral ownership,
then dispatch endpoints directly to their validated owners.

### Work

- [x] Finalize contribution roles for module use, action handling, stream
      publishing, stream consumption, events, metrics, and future endpoint
      kinds.
- [x] Generate declaration types independently of components.
- [x] Let components claim only declarations present in the generated contract.
- [x] Validate cardinality: exactly one handler where required, allowed fan-out
      where specified, and no undeclared participation.
- [x] Resolve handlers, publishers, and consumers at compile time.
- [x] Define direct component conventions, including sync and explicitly
      supported async return forms.
- [x] Generate or derive endpoint dispatch tables containing direct function
      addresses or stateless thunks, not nullable registration slots.
- [x] Make diagnostics name the component, role, and declaration involved.

### Evidence

- Compile-fail tests cover missing, duplicate, incompatible, and undeclared
  participation for every implemented role.
- Runtime tests exercise an action, input stream, and output stream through
  their compile-time-resolved owners.
- Symbol/map inspection confirms there is no runtime registration pass or
  mutable callback-slot table.
- Components can be reordered without changing endpoint identity or behavior.

### Exit gate

The representative generated contract is fully implemented by handwritten
components and callable through a generic endpoint layer without legacy binding.

### Drift guards

- Do not generate behavior into application code.
- Do not make components declare schema existence a second time.
- Do not use nullable callbacks to postpone missing-owner errors until runtime.

### Evidence record

Date: 2026-08-03

Commit: active expansion worktree.

Commands: `solar.host.system.dispatch`, dispatch compile-fail suite, and clean
GCC/LTO builds.

Artifacts: action, input/output stream, event fan-out, and metric recorder
roles with compile-time cardinality/signature validation and direct dispatch.

Known limitations: explicitly asynchronous handler return forms remain a
future declaration policy; no asynchronous form is silently accepted.

## 12. Phase 6 — Standalone Remote And Endpoint Runtime

### Objective

Replace legacy System-coupled Remote behavior with an independently owned,
bounded server over a clean endpoint/capability model.

### Work

- [x] Define a dependency-light generated Remote contract over actions, data,
      input streams, output streams, and manifest/identity operations.
- [x] Implement `remote::Server<Contract, Config>` with explicit storage,
      sessions, subscriptions, request correlation, and flow-control limits.
- [x] Implement `remote::StaticServer<Application, Contract, Config>` over the
      same server implementation.
- [x] Keep transports as independently testable links/channels.
- [x] Implement explicit adapters for parameter exposure and component endpoint
      dispatch.
- [x] Give authored output streams genuine push/stream semantics.
- [x] Specify input-stream lifecycle, ownership, configuration, credit/backpressure,
      timeout, disconnect, and reconfiguration behavior.
- [x] Preserve typed static dispatch on firmware and dynamic descriptors on the
      wire without duplicating schema truth.
- [x] Define bounded behavior for too many sessions, subscriptions, inflight
      calls, and oversized payloads.
- [x] Remove the prototype fixture's legacy `solar::System` Remote seam once the
      standalone path passes equivalent tests.

### Evidence

- Server tests run against in-memory, TCP `native_sim`, and serial-compatible
  byte-channel fixtures.
- Protocol vectors cover success, malformed frames, unknown IDs, capacity
  exhaustion, disconnect, cancellation, and reconnect.
- Concurrent parameter, action, input-stream, and output-stream operations pass.
- A stream is delivered without Data polling or an application-level shim.
- Remote operates without System when explicitly given its contract and
  adapters.
- The server has no heap allocation in configured firmware operation, unless a
  separately documented host-only policy is selected.

### Exit gate

The full representative contract works end-to-end through standalone Remote,
including genuine bidirectional stream behavior and reconnect.

### Drift guards

- Do not teach System about Remote internals.
- Do not bake serial, TCP, Station, or application policy into the server.
- Do not preserve the transitional output-stream-as-Data encoding as the target.
- Do not use application names to route requests in generic Remote code.

### Evidence record

Date: 2026-08-03

Commit: active expansion worktree.

Commands: clean GCC 13.3 host suite; clean Zephyr 4.4 `native_sim` build;
generated-client TCP exercise before and after process restart.

Artifacts: bounded `remote::Server`/`StaticServer`, system-independent
`ByteRuntime`, real Stream-domain input/output manifests, producer-paced Stream
publication, input credit/backpressure, protocol vectors, and generated async
stream clients.

Known limitations: physical USB CDC repetition belongs to Phase 9; no Teensy
serial device was attached during this phase.

## 13. Phase 7 — Effective Manifests, Shipments, And Python Clients

### Objective

Complete the authored-contract-to-native-Python pipeline with final-firmware
verification and reproducible shipment artifacts.

### Work

#### Firmware and shipment identity

- [x] Embed normalized contract metadata and identity in linkable sections or
      other deterministic final-ELF-visible structures.
- [x] Extract the effective manifest from the final linked ELF.
- [x] Compare authored/generated expectations with the effective manifest and
      fail on missing or unexpected endpoints.
- [x] Define interface digest, compatibility identity, build identity, and
      shipment identity as separate concepts.
- [x] Create a reproducible shipment descriptor binding firmware, effective
      manifest, generator version, protocol version, and generated client.

#### Python SDK and generated package

- [x] Keep transport, framing, session, reconnect, dynamic manifest, and generic
      descriptors in `solar_remote`.
- [x] Generate `<application>-solar-client` importing as
      `<application>_solar`.
- [x] Generate native async parameter, action, and stream APIs rooted at
      `Robot`.
- [x] Generate enums, structs, validation, documentation, and type annotations
      from the same normalized IR.
- [x] Require exact interface matching by default.
- [x] Make compatible additive binding an explicit opt-in and exact-build
      matching a separate opt-in.
- [x] Support dynamic generic access alongside, not inside, typed generated APIs.
- [x] Build and install wheels reproducibly in a clean environment.

### Evidence

- Tampering with or omitting an expected firmware endpoint fails verification.
- Identical interfaces with different builds retain interface compatibility but
  have distinct build/shipment identities.
- Additive and breaking schema fixtures exercise all binding policies.
- `mypy` validates generated public examples with no handwritten stubs.
- A clean virtual environment installs the wheel and controls `native_sim` over
  TCP across a simulator restart.
- Generated documentation and runtime descriptors agree with the effective
  manifest.

### Exit gate

A release build can produce firmware, an effective manifest, shipment metadata,
and an installable application client that bind under documented policies.

### Drift guards

- Do not generate transport ownership into the application client.
- Do not derive Python names or wire identities separately from normalized IR.
- Do not treat exact build identity as interface compatibility.
- Do not trust the authored schema alone when final ELF contents disagree.

### Evidence record

Date: 2026-08-03

Commit: active expansion worktree.

Commands: final-ELF manifest extraction and deep shipment verification; tamper
tests; wheel build/install; `mypy` generated public example; TCP generated-client
exercise across simulator restart.

Artifacts: effective manifest image/digest, compatibility report, shipment
descriptor, `py.typed` wheel, generated models/client, and exact/compatible/build
binding policies.

Known limitations: application-specific Station adoption is a Phase 9 migration
task, not part of the SDK/client generator boundary.

## 14. Phase 8 — Remaining Standalone Modules And Adapters

### Objective

Move each remaining framework subsystem onto the proven standalone-module and
generic-composition model without widening System.

Migrate one module at a time. Each module gets an object/static conformance
suite, bounded-capacity tests, standalone example, System ownership test, and
explicit adapters before the next begins.

### 8.1 Logging

- [x] Separate log record creation, bounded retention, and sinks.
- [x] Keep console, Remote, and persistence sinks as adapters.
- [x] Preserve pre-sink bring-up records and explicit overflow reporting.
- [x] Generate category/schema metadata only where it removes duplicated
      declarations.

### 8.2 Events

- [x] Implement typed emit/observe behavior with explicit retention policy.
- [x] Define synchronous versus scheduled delivery and bounded fan-out.
- [x] Connect Remote exposure through an adapter.

### 8.3 Metrics

- [x] Implement typed values and aggregation without System ownership.
- [x] Separate sampling/export policy from metric storage.
- [x] Add inspection and Remote adapters.

### 8.4 Persistence

- [x] Define an independently usable storage interface and lifecycle.
- [x] Implement parameter persistence as an adapter, not a Store behavior.
- [x] Specify corruption, migration, version, and partial-write behavior.

### 8.5 Execution And Kernel Facilities

- [x] Separate scheduling primitives from System lifecycle policy.
- [x] Preserve explicit ownership, bounded queues, cancellation, and shutdown.
- [x] Keep Zephyr-specific machinery behind kernel-level types.

### 8.6 Supervision

- [x] Separate health/state policy from System composition.
- [x] Express supervised relationships as contributions or adapters.
- [x] Avoid naming the application-level Cockpit/orchestration service in the
      generic framework supervisor.

### Evidence

- Every module passes the common standalone/static conformance requirements.
- Cross-module behavior exists only in named adapter targets.
- Package dependency checks preserve the target layering.
- The representative application composes all migrated modules without a new
  System special case.

### Exit gate

All framework facilities required by the real application use the new module
contract. No new production code needs legacy binding or legacy System access.

### Drift guards

- Do not migrate several modules in one unreviewable rewrite.
- Do not move sibling integration into either sibling's core implementation.
- Do not require Remote or System for standalone tests.

### Evidence record

Date: 2026-08-03

Commit: active expansion worktree.

Commands: standalone object/static conformance and concurrency tests under GCC
13.3; generic System composition/adapter lifecycle test; Zephyr `native_sim`
build and TCP generated-client exercise with a `ServiceRunner`-owned publisher.

Artifacts: bounded retained log store and `Capture`; synchronous/retained event
bus plus bounded `ScheduledForward`; metric values and aggregation; atomic
fixed-slot storage and versioned/checksummed parameter persistence; bounded
task queue and Zephyr service runner; health monitor/probe adapter; generic
Remote `StreamSink`; named adapter CMake targets.

Known limitations: generated category metadata is emitted only for declarations
present in IDL; Solar does not invent metadata for application-local record
types. Physical execution timing is measured in Phase 9.

## 15. Phase 9 — Real Application Migration And Hardware Validation

### Objective

Prove the architecture on actual firmware, simulation, generated clients, and
the Teensy target before deleting legacy infrastructure.

### Work

- [x] Convert the real application contract to project/schema IDL.
- [x] Introduce canonical `app/app.hpp` ownership aliases and `app/system.hpp`
      composition.
- [x] Migrate services one at a time to generated declarations and contribution
      roles.
- [x] Replace handwritten duplicate Remote schema and endpoint registration.
- [x] Use generated Python APIs from Station-facing integration tests without
      coupling Solar to Station.
- [x] Validate simulator TCP Remote and simulator log transport.
- [x] Validate Teensy USB CDC Remote, console/bootloader behavior, reconnect,
      and retained bring-up logs.
- [x] Exercise hardware services while Remote traffic and streams are active.
- [x] Measure flash, RAM, stack, boot time, request latency, stream throughput,
      and generator/build time against the Phase 0 baseline.
- [x] Document intentional regressions or recover them before proceeding.

### Evidence

- Clean `native_sim` and Teensy builds contain no application-level schema or
  identity duplication.
- Generated client smoke tests pass over TCP and USB CDC.
- Disconnect, reflash, reconnect, and simulator restart recover without
  restarting the host client owner.
- Representative concurrent streams, actions, parameters, and logging run for
  a defined soak interval without loss outside documented flow-control policy.
- Resource measurements fit configured hardware budgets.

### Exit gate

The real application and supported transports operate solely through the new
architecture. All legacy users are either migrated or explicitly removed.

### Drift guards

- Do not edit generated code to accommodate application behavior.
- Do not add application-specific hooks to generic modules.
- Do not retain duplicate handwritten schemas as a fallback.
- Do not accept a simulator-only result as hardware completion.

### Evidence record

Date: 2026-08-03

Commit: active expansion worktree.

Commands: clean Docker GCC 13.3 host build and 67-test CTest matrix; clean
Zephyr `native_sim` build; linked-ELF shipment verification; generated Cockpit
client data/update/input-stream exercise across two simulator boots; 200-request
latency run; five-second 20 Hz input-stream soak; simulator log-socket capture;
Teensy release build with speed optimization and LTO; generated client over
USB CDC; retained-log replay; exclusive input replacement; soft-reboot flash
and same-process host rebind; clean isolated Teensy build timing.

Artifacts: `firmware/solar.project.yaml`, generated Cockpit schema and stable
identity lock, canonical `app/app.hpp` plus `app/system.hpp`, linked-image
shipment package and wheel, simulator generated-client smoke test. The Teensy
image uses 307,272 bytes of flash and 198,040 bytes of main RAM, plus 2 KiB DTCM.
Configured stacks are 4 KiB for main, system workqueue, and Remote, and 3 KiB
for Cockpit. Simulator request latency over 200 sequential requests was 19.847
ms median, 21.500 ms p95, and 22.831 ms maximum. A 100-sample input stream ran
for 5.279 seconds (18.94 Hz requested at 20 Hz). Warm real-application code
generation takes 42-44 ms and emits 126,990 bytes across 16 files; the only
cross-directory difference is the expected absolute-path depfile. A clean
optimized/LTO Teensy build takes 30.37 seconds. Device initialization reaches
Cockpit active at 22 ms. Fifty sequential USB requests measured 105.024 ms
median, 105.535 ms p95, and 107.658 ms maximum. Forty neutral drive commands
were accepted at 20 Hz while both encoder samplers advanced from 8,088 to 8,909
observations with zero failures. Exclusive differential-to-tank replacement
closed the prior token with reason `replaced`. The 134-baud flash path and
verified USB re-enumeration took 11.109 seconds; the same host process rebound
in another 0.858 seconds.

Known limitations: drive traffic used neutral effort so this architecture gate
did not move the robot. It nevertheless exercised Remote dispatch, Cockpit's
single-writer path, PWM updates, deadman neutralization, exclusive replacement,
and continuously sampled hardware encoders. The larger generated artifact set
relative to Phase 0 is intentional: this is the full robot contract and includes
C++, Python, manifest, compatibility, and build outputs.

## 16. Phase 10 — Legacy Deletion, Hardening, And Release

### Objective

Remove the old architecture completely, make unsupported states impossible,
and leave a documented, releasable framework rather than a permanent migration.

### Deletion ledger

- [x] Delete strict and relaxed binding configuration and tests.
- [x] Delete `system_binding` and global frontend registration machinery.
- [x] Delete nullable runtime callback slots replaced by compile-time dispatch.
- [x] Delete subsystem-specific System branches and catalog plumbing.
- [x] Delete the legacy Remote-as-System implementation and fixture adapters.
- [x] Delete transitional output-stream-as-Data mappings.
- [x] Delete duplicate handwritten application schema and manifests.
- [x] Delete prototype namespaces, tools, fixtures, and compatibility aliases
      after their production replacements carry equivalent evidence.
- [x] Delete migration-only Kconfig options, CMake paths, documentation, and
      dead tests.

### Hardening work

- [x] Run all host, compile-fail, generator, Python, `native_sim`, and hardware
      suites from clean checkouts.
- [x] Run sanitizers and fuzzing against parser, codecs, manifests, and Remote
      framing where supported.
- [x] Audit allocation, bounds, synchronization, shutdown, and error propagation.
- [x] Audit public headers for dependency leakage and useful IDE documentation.
- [x] Verify reproducible generated files, manifests, wheels, and shipment
      descriptors.
- [x] Publish migration-free examples for standalone modules, composed firmware,
      and generated Python clients.
- [x] Update top-level architecture and getting-started documentation.
- [x] Tag the first release that contains only the new architecture.

### Exit gate

Repository search finds no legacy binding architecture or migration seam.
Supported builds and hardware tests are green from clean checkouts. Public
documentation teaches only the new model.

### Drift guards

- Do not call the redesign complete while compatibility code remains enabled.
- Do not keep dead APIs “just in case.” Version control is the archive.
- Do not publish until generated artifacts can be reproduced from pinned tools
  and source inputs.

### Evidence record

Date: 2026-08-03

Commit: first modularity-and-generation release, recorded by annotated tag
`v0.1.0`.

Commands: clean Linux GCC 13.3 build plus 67/67 CTest; ASan/UBSan build plus
67/67 CTest; macOS Homebrew Clang 22 build plus 66/66 portable CTest (the ELF
post-link test is Linux-only); Python SDK pytest 5/5; Zephyr Twister 15/15 configurations and 44/44
cases on `native_sim`; two Teensy-target Twister build-only configurations;
clean `native_sim` and optimized/LTO Teensy application builds; clean native
builds of both C++ examples; documentation structural and cross-reference
audits; deterministic real-application generation; repository searches for
removed bindings, callback slots, prototypes, and stale includes.

Artifacts: standalone module headers/tests; generic composer and contribution
dispatch; standalone Remote with typed scheduler adapter; production compiler,
effective manifest, shipment, wheel, and generated client; migration-free
examples and public docs. Mutation fuzzing covers Remote frame rejection and
the compiler suite covers malformed syntax, bounds, identities, and evolution.

Known limitations: full Sphinx/Doxygen site generation was not run on this host
because those optional documentation tools are not installed; the repository's
documentation and cross-reference audit passes. This does not affect the
compiled public headers, examples, or generated reference coverage.

## 17. Cross-Cutting Verification Matrix

Every applicable row must be considered at each phase; “not applicable” needs
a short reason in that phase's evidence record.

| Surface | Required verification |
| --- | --- |
| C++ unit | success, failure, boundaries, lifecycle, concurrency |
| Compile-fail | missing ownership, duplicates, bad types, bad graph, diagnostics |
| Generator | golden IR/output, negative syntax/semantics, determinism, compatibility |
| Build | clean/incremental CMake, host, Zephyr, isolated build directories |
| Native simulation | real TCP session, concurrency, disconnect, restart |
| Teensy | USB CDC, reset/reflash recovery, hardware load, bounded resources |
| Python | unit, typing, clean install, generated API, reconnect |
| Protocol | vectors, malformed traffic, capacity, version negotiation |
| Resource | flash, RAM, stack, generation time, throughput, latency |
| Documentation | public examples compile/run; IDE API comments are useful |

## 18. Definition Of Done For A Change

A production change is done only when:

- it belongs to the active phase or an explicitly independent prerequisite;
- its public shape agrees with the locked design;
- positive, negative, and boundary behavior is tested as applicable;
- generated output and docs are updated together;
- no source-tree generated artifact or accidental compatibility path is added;
- affected clean builds pass;
- resource or wire-format changes are measured and recorded; and
- its phase evidence record is updated when it contributes to an exit gate.

“Implemented,” “compiles,” and “works in the prototype” are not interchangeable
with this definition.

## 19. Deferred Work Register

Deferred work is allowed only here, with a destination phase and reason.

| Item | Destination | Reason | Owner/status |
| --- | --- | --- | --- |
| Final spelling refinements after production compiler feedback | phase 1-2 | names remain provisional until real headers exist | open |
| Advanced Remote QoS beyond bounded flow control | post phase 10 | not required to validate the architecture | deferred |
| Repository splitting | post phase 10 | module boundaries do not require separate repositories | deferred |
| Multiple Systems per firmware | none | explicit non-goal | rejected |
| Runtime application context propagation | none | conflicts with static ownership model | rejected |
| Legacy universal inspection subsystem | post phase 10 redesign | its dormant Remote bridge depended on deleted System catalogs; module-specific typed views remain | removed |

New entries must name a destination. “Later” is not a destination.

## 20. Risk Register

| Risk | Early signal | Mitigation/gate |
| --- | --- | --- |
| Template diagnostics become unusable | tests assert only failure, not message | compile-fail fixtures require focused declaration/component names |
| Generator becomes several inconsistent compilers | backend reads YAML or allocates IDs | Phase 1 API accepts normalized IR only |
| Static facade duplicates state | multiple owner symbols or divergent tests | conformance and symbol inspection in Phase 2 |
| System regains subsystem knowledge | branch includes a first-party module type | structural review/test in Phase 4 |
| Runtime binding returns under a new name | mutable callback registration appears | Phase 5 symbol/design gate |
| Remote leaks transport/application policy | server selects serial/TCP or knows Station | explicit channel and adapter boundary in Phase 6 |
| Authored contract differs from firmware | generated client passes against stale schema | final-ELF verification in Phase 7 |
| Migration never ends | compatibility seam has no deletion task | deletion ledger blocks Phase 10 |
| Firmware cost grows unexpectedly | host-only tests remain green | measurements at phases 0, 6, 9, and 10 |

## 21. First Execution Slice

The next implementation work is deliberately narrow:

1. fix explicit and retired ID collision handling;
2. make the full supported host/prototype baseline green;
3. reconcile the prototype evaluation with current results;
4. create the prototype artifact disposition inventory;
5. record clean determinism and resource baselines; and
6. close Phase 0 before creating production namespaces.

This prevents the production compiler from inheriting a known identity defect
and gives every later phase a trustworthy comparison point.

## 22. Decision Log

| Date | Decision | Reason | Affected phases |
| --- | --- | --- | --- |
| 2026-08-03 | Adopt this gated plan as the execution authority | prevent architectural drift between prototype and production | all |
| 2026-08-03 | Target no source compatibility with the legacy architecture | avoid permanent dual binding and test burden | 3-10 |
| 2026-08-03 | Keep one static application and one static System | preserve concise firmware access without context propagation | 2-10 |
| 2026-08-03 | Use standalone modules plus typed static facades over one implementation | support small projects and composed firmware consistently | 2-10 |
| 2026-08-03 | Generate from one normalized IR and verify against the final ELF | make firmware and host contracts reproducible and truthful | 1, 5-10 |
| 2026-08-03 | Use compile-time behavioral ownership and dispatch | eliminate binding cost and runtime missing-owner states | 4-6 |
| 2026-08-03 | Pass complete typed registrations to the Remote scheduler | let adapters honor execution-target metadata without callbacks | 6, 10 |
| 2026-08-03 | Remove the dormant universal inspection bridge | it retained deleted System catalog coupling; typed module views are the reusable boundary | 8, 10 |

Future architectural decisions are appended here; prior entries are not edited
to hide history.

## 23. Final Success Criteria

The redesign is complete when all of the following are true:

- Solar modules can be used independently with explicitly owned state.
- Canonical firmware uses concise `app::...` static facades and one static
  composed System without runtime context propagation.
- Third-party modules compose without modifying System.
- Application contracts are authored once and generate firmware declarations,
  verified effective manifests, shipment metadata, and native async Python APIs.
- Remote provides real action, data, input-stream, and output-stream behavior as
  a standalone bounded module.
- The real simulator and Teensy firmware pass end-to-end generated-client tests.
- The legacy binding architecture, migration adapters, duplicated schemas, and
  prototype implementation are absent.
- Public documentation and examples expose only the new architecture.
