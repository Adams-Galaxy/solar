# Prototype Evaluation And Initial API Lock

Date: 2026-08-03

Status: prototype slice complete; approved basis for production scaffolding

## Outcome

The prototype validates the accepted architecture as a complete vertical
slice. One strict IDL produces firmware declarations, a linked-ELF manifest,
an installable application-specific Python client, and compatibility and
shipment metadata. The generated client exercised parameter get/set, action
calls, output and input streams, and concurrent dynamic access against
`native_sim` over TCP.

The prototype introduces no runtime System object and passes no context object
through services. `ParameterStore` works without System; the typed facade owns
exactly one instance for an application tag. The experimental composer is a
static type and has no parameter-specific branches.

The native fixture temporarily adapts the new composition model to the current
Remote `solar::System`. That adapter is contained in the fixture, uses neither
`SOLAR_BIND_SYSTEM` nor a global frontend, and is a migration seam rather than
the future Remote module design.

## API Decisions

The following shapes are selected for production scaffolding:

- Standalone modules expose an object API and a typed static facade backed by
  the same implementation. In the parameter module these become
  `solar::parameters::Store<Schema>` and
  `solar::parameters::StaticStore<Application, Schema>`.
- Application code aliases canonical typed facades, for example
  `app::parameters`, and calls `app::parameters::get<P>()`.
- System composition retains `Compose<...>`, `Own<...>`, and explicit
  `Connect<Adapter, Endpoint...>`. An implicit connection shorthand is rejected
  because it loses adapter ownership and produces weaker diagnostics.
- Components declare `Contributions<Uses<Module, ...>, Handles<...>,
  Publishes<...>, Consumes<...>>`. Existence remains generated; contributions
  only claim participation and ownership.
- The authored files are `solar.project.yaml` and `*.solar.yaml`. They compile
  to a normalized IR before any language backend runs.
- Production C++ output will live below the build tree at
  `generated/solar/app.hpp` and use `app::generated`. Remote adapter output may
  remain a separate generated header so standalone modules do not include
  Remote.
- Application Python distributions use `<application>-solar-client`, import as
  `<application>_solar`, and expose a root `Robot` with `parameters`, `actions`,
  and `streams` namespaces.
- Generated clients require an exact interface digest by default. Compatible
  additive binding is explicit. Exact build identity remains a separate opt-in
  policy, so a compatible rebuild does not masquerade as an interface change.

## Runtime Findings

The production Remote runtime now carries authored input and output streams in
the Stream domain. Output values are explicitly published by their producer and
are delivered only to interested sessions at the negotiated rate. Input values
use explicit activation, bounded credit, cancellation, and reconnect cleanup.
The earlier output-stream-as-polled-Data prototype was removed; generated
Python stream APIs resolve the effective Stream catalog directly.

The generated parameter wire wrapper is intentionally separate from scalar
parameter storage. This keeps the standalone store independent of codecs while
giving Remote an object schema.

## Evidence

- Strict parser and compiler tests: 17 passing cases, including deterministic
  output, source-located errors, stable and retired IDs, valid and invalid
  renames, bounds, format rejection, breaking changes, and path collisions.
- Generated fake-session tests: 4 passing async cases, including exact and
  compatible binding and separate exact-build policy.
- Compile-fail fixtures cover absent parameters, duplicate ownership, missing
  dependencies, handler cardinality, and undeclared participation.
- Generated models, client, and a public usage example pass `mypy` when checked
  as the generated package boundary.
- The generated wheel installs with `solar-remote` in a clean virtual
  environment.
- Clean `native_sim/native/64` build succeeds and emits its effective manifest
  from the final ELF.
- Real TCP integration succeeds before and after simulator process restart.
- Generation measured approximately 70 ms cold and 30 ms warm on the current
  host. Generated paths are present in host and Zephyr compile commands.
- The native fixture ELF measured 224,224 bytes text, 63,828 bytes data, and
  76,926 bytes BSS. The canonical parameter facade owns one 32-byte storage
  symbol in that fixture.

The supported Linux GCC 14 host suite passes 77/77 tests. AppleClang 16 remains
unsupported for the legacy implementation because it rejects the old
`consteval` diagnostic and manifest mutation patterns and can crash compiling a
legacy target. Stale protocol-vector and Python transport issues identified
during the prototype have been corrected. The production redesign must avoid
the rejected constant-evaluation patterns and restore Clang as a hardening gate.

## Rejected Alternatives

- Runtime System/application objects: rejected because they reintroduce
  context propagation and weaken the one-static-application model.
- Separate object and static implementations: rejected because tests prove a
  static facade can forward to exactly one ordinary store.
- Strict and relaxed binding variants in the new API: rejected because explicit
  ownership and compile-time participation validation cover the useful safety
  properties without maintaining two modes.
- Schema YAML read independently by each backend: rejected because a normalized
  IR gives deterministic, language-independent generation.
- Generated transport implementations: rejected because Station owns channel
  selection and reconnect while `solar_remote` owns protocol sessions.
- Exact-build matching as the default: rejected because interface and shipment
  identity have different compatibility meanings.

## Production Migration Plan

1. Scaffold production `solar::parameters` object/static APIs from the proven
   implementation, preserving current facilities behind adapters.
2. Promote the strict frontend, IR, lock file, and build integration under a
   versioned generator command; retain the prototype grammar as the initial
   supported subset.
3. Promote generated C++ types and contract headers, then convert one real
   application schema without changing its behaviours.
4. Add generic production `Compose`, `Own`, `Connect`, and contribution
   validation alongside the current System.
5. Refactor Remote into an independently owned module and replace the fixture's
   legacy-System seam with a normal `Connect` adapter.
6. Generate final-ELF shipment artifacts and application wheels in the normal
   firmware build/release pipeline.
7. Migrate facilities one module at a time, then remove strict/relaxed bindings
   only after all production callers and compatibility tests have moved.
