# Stage 03: Blueprint, System, Binding, And Frontends

Status: landed

Landed date: 2026-07-16

Implementation repository/branch: `/workspaces/solar`, `static_reform`

Baseline: Stage 02 landed worktree based on `bed432f pre-solar-implementation`

## 1. Objective

Land Solar's static composition architecture: an order-independent Blueprint,
one normalized System type, validated component graph and subsystem catalogs,
distributed type-owned state, one application binding customization point, and
shared relaxed/strict typed frontend machinery. The stage must make future
global subsystem APIs possible without creating a System object, runtime
registry, monolithic storage object, or lifecycle placeholder.

## 2. Specification Coverage

| Specification | Coverage | Notes |
| --- | --- | --- |
| `01-system-blueprint-and-binding.md` | sections, normalization, categories, graph, built-ins, configuration, application binding, static ownership, strict/relaxed frontends, include direction, queries, diagnostics | complete for generic Stage 03 infrastructure |
| `02-identity-contributions-and-catalogs.md` | contribution collection into the normalized effective catalog | complete |
| `03-lifecycle-kernel-and-configuration.md` | stable boot/stop and focused lifecycle query declarations only | runtime lifecycle behavior remains Stage 06 |
| `14-integrated-architecture.md` | static System ownership and strict/relaxed architecture boundary | complete for this stage |
| Stage 03 of `04-implementation-roadmap.md` | declared implementation and verification scope | complete |

No boot, stop, component hook, report, kernel, or concrete subsystem behavior
is implemented by this stage.

## 3. Public Surface Landed

### Composition

- `solar::Blueprint<Sections...>` accepts recognized sections in any order.
- `Devices`, `Facilities`, `Services`, `Executors`, and `Execution` retain the
  accepted component and leaf-registration categories.
- `CatalogSection<Tag, ...>` and subsystem-specialized `section_traits` extend
  catalog composition without modifying Solar's normalizer.
- `SubsystemConfiguration<Tag, ...>` keeps policy separate from catalog
  membership.
- `Builtins`, `BuiltinCandidates`, and `builtin_traits` provide generic
  explicit, demand-derived, required-derived, and always-present inclusion.
- `Dependencies<...>` and `ComponentGraph` expose and validate the effective
  dependency DAG.
- `effective_blueprint_t<Blueprint>` exposes deterministic normalized types.

### Static System

- `solar::System<Blueprint>` is a named static type and owns no System object.
- `System::Components`, `Builtins`, `Catalogs`, `ExecutionRegistrations`, and
  `ConfigurationSections` expose normalized facts.
- `System::catalog::Of<Tag>` and `System::catalog::components()` expose immutable
  catalogs.
- `System::graph::Dependencies<Component>` and `Category<Component>` expose
  focused graph facts.
- `System::configuration::Policy` and `EffectivePolicy` implement the accepted
  declaration, Blueprint, then Kconfig precedence order.
- `System::StateSlot<Owner, Key, State>` owns canonical distributed static
  state by exact System, owner, key, and state type.

### Binding and frontends

- `system_binding<Application>` is the one application binding trait.
- `SOLAR_BIND_SYSTEM` and `SOLAR_BIND_SYSTEM_FOR` define default and alternate
  application bindings.
- `bound_system_t<Application>` resolves and validates the selected System.
- `frontend::Operation<Policy, Declaration, Application>` is the shared
  operation surface in both build modes.
- `frontend::Of<Application>` supplies the generic alternate-application form
  that concrete subsystem `Of<Application>` surfaces can wrap later.
- `frontend::bind_catalog`, `bind_disabled`, and focused test reset helpers
  connect only declarations already admitted by the compile-time catalog.
- `solar::boot`, `solar::stop`, explicit `System::boot/stop`, and focused
  lifecycle query names are declared for Stage 06 definition. Their deduced
  return types have no definition yet and cannot provide placeholder behavior.

Representative application shape:

```cpp
using RobotBlueprint = solar::Blueprint<
    solar::Devices<Imu>,
    solar::Facilities<NavigationState>,
    solar::Services<Controller>,
    solar::Executors<Worker>,
    solar::Execution<ControlJob>,
    solar::Parameters<DriveKp>>;

using RobotSystem = solar::System<RobotBlueprint>;
SOLAR_BIND_SYSTEM(RobotSystem);
```

Ordinary component headers include Solar and direct declarations only. Relaxed
builds may keep ordinary bound calls inline. Strict builds retain the same
method declaration and define ordinary methods in a source file that includes
the composition root. A defaulted function-template method remains available
for deliberately inline strict code.

## 4. Runtime Ownership

| Owner | Storage/resource | Capacity | Synchronization | Lifetime |
| --- | --- | --- | --- | --- |
| `StaticStateSlot<System, Owner, Key, State>` | subsystem-selected canonical `State` | exactly one per odr-used type combination | selected by owning subsystem | program lifetime |
| relaxed `BindingState<Application, Policy>` | one-byte availability enum | one per odr-used application/policy pair | `std::atomic`, acquire/release | program lifetime |
| relaxed `OperationSlot<Policy, Declaration, Application, Signature>` | one non-owning function pointer | one per odr-used operation specialization | `std::atomic`, acquire/release | program lifetime |

Strict mode adds no binding state or operation pointer. Blueprint, effective
Blueprint, graph, catalogs, configuration, and application trait binding are
compile-time types.

There is no heap use, runtime registration, constructor registration, System
object, central state store, mutex, queue, thread, stack, workqueue, timer, or
poll object. Binding performs no lifecycle action. Inline static state has one
canonical address across translation units and under LTO.

## 5. Compile-Time Behavior

Normalization performs these finite steps:

1. validate every section and reject duplicate keys;
2. supply omitted empty component, execution, built-in, and extension sections;
3. collect user components and direct subsystem declarations;
4. form provisional catalogs used by built-in demand predicates;
5. close explicit, demanded, always-present, and required built-ins;
6. restore selected built-ins to deterministic candidate order;
7. form and validate the complete effective component graph;
8. recollect final contribution catalogs from effective components;
9. validate subsystem policy recognition, exclusive axes, Kconfig
   availability, and subsystem-wide combinations;
10. expose immutable effective types through `System`.

Custom subsystem catalog sections are converted through `section_traits`, not
through a closed specialization on `CatalogSection`. Built-in support being
enabled does not imply presence. An enabled unused fixture is absent; demand,
always-present selection, explicit selection, and required closure are all
verified.

Strict operations resolve `bound_system_t<Application>`, validate catalog
membership at compile time, and invoke the policy directly. Relaxed operations
load one availability atom and one operation pointer before indirect dispatch.
Both modes invoke the same policy against the same `System::StateSlot`.

## 6. Error And Availability Behavior

Relaxed operation errors are explicit `Result` errors:

| Condition | Error |
| --- | --- |
| frontend has not been connected | `frontend::Error::NotReady` |
| subsystem is unavailable in the build | `frontend::Error::Disabled` |
| declaration is absent from the connected catalog | `frontend::Error::NotRegistered` |

Compile-time diagnostics now cover:

- unknown and duplicate Blueprint sections;
- repeated or cross-category components;
- malformed, absent, and cyclic component dependencies;
- disabled selected built-ins and missing built-in requirements;
- unrecognized, unavailable, duplicate-axis, and incompatible subsystem
  policies;
- missing, duplicate, and invalid application bindings;
- strict use of an unregistered declaration;
- eager and instantiated lazy strict calls without visible binding.

Each contract has a stable `SOLAR_DIAGNOSTIC_*` token test.

## 7. Zephyr Integration

The composition machinery requires no Zephyr runtime API. Zephyr supplies the
canonical generated value of `CONFIG_SOLAR_STRICT_CATALOG_BINDING`; there is no
fallback config header. Native tests use C++23, full libcpp, no exceptions, and
no RTTI.

Relaxed atomics use standard C++ atomics supported by the workspace Zephyr 4.4
toolchain. Frontend connection is intended to occur during the later boot
barrier before concurrent component execution. No operation is claimed ISR-safe
by this generic stage.

## 8. Files Changed

### Added

- `include/solar/system/sections.hpp`
- `include/solar/system/graph.hpp`
- `include/solar/system/blueprint.hpp`
- `include/solar/system/system.hpp`
- `include/solar/system/binding.hpp`
- `include/solar/system/frontend.hpp`
- `include/solar/system/api.hpp`
- Stage 03 host fixtures and sources under `tests/host/`
- 18 Stage 03 compile-failure fixtures under `tests/compile_fail/fixtures/`
- `tests/zephyr/system/`

### Reshaped

- `include/solar/system.hpp`
- `include/solar/solar.hpp`
- `tests/host/CMakeLists.txt`

### Removed

None beyond the superseded architecture already removed by Stage 00.

## 9. Tests And Evidence

| Command | Platform/configuration | Result | What it proves |
| --- | --- | --- | --- |
| `cmake -S . -B build/host -DCMAKE_BUILD_TYPE=Debug && cmake --build build/host -j$(nproc) && ctest --test-dir build/host --output-on-failure` | host GCC 13, relaxed/strict/LTO | 47/47 pass | all Stage 00-03 host regressions, runtime errors, canonical state, section order, built-ins, configuration, bindings, include direction, and 38 expected failures |
| public System-header standalone syntax loop with `-std=c++23 -Wall -Wextra -Wpedantic` | host GCC 13 | pass | every new public header is self-contained and warning-clean |
| `west twister -T tests/zephyr -p native_sim/native/64 --inline-logs --outdir build/twister-stage03` | Zephyr 4.4, GNU SDK, all Solar native suites | 8/8 configurations and 14/14 cases pass, no warnings | Stage 00-02 regressions plus initial strict/relaxed Stage 03 integration |
| `west twister -T tests/zephyr/system -p native_sim/native/64 --inline-logs --outdir build/twister-stage03-system-closure` | Zephyr 4.4 strict and relaxed final sources | 2/2 configurations and 4/4 cases pass, no warnings | final availability atom, strict lazy/out-of-line paths, and multi-TU state |
| host relaxed target built with CMake IPO | GCC LTO | pass | frontend and state identity survive LTO |
| `size` and demangled `nm` over final native ELFs | native strict/relaxed | inspected | one canonical state, no strict frontend slots, bounded relaxed slots |
| `git diff --check` | both worktrees | pass | patch hygiene |

Final representative native Ztest images:

| Mode | text | data | bss | total |
| --- | ---: | ---: | ---: | ---: |
| relaxed | 44,830 B | 901 B | 6,457 B | 52,188 B |
| strict | 43,675 B | 901 B | 6,407 B | 50,983 B |

These are complete Ztest images, not Solar-only attribution. Demangled symbols
account for the fixture's 50-byte BSS difference: two one-byte availability
states, five eight-byte operation pointers exercised by the tests, and two
additional four-byte declaration state slots instantiated while binding the
three-entry relaxed catalog. Real subsystem storage and operation counts remain
declaration and policy dependent.

No `clang++` executable exists in this development container. GCC 13 and the
Zephyr SDK GNU 14.3 toolchains are verified here; a Clang build lane remains a
tooling/CI follow-up and is not represented as passing evidence.

## 10. Specification Refinements

None. Implementation details were resolved inside the accepted ownership and
behavior contracts.

## 11. Firmware And Host Impact

No firmware migration is required at this roadmap stage. Firmware remains on
its development branch until a designated integration checkpoint. Host tests
now model the target project layout: declaration-only headers do not include the
application root, the root owns Blueprint/System/binding, and strict ordinary
method definitions include the root out of line.

## 12. Known Limits And Deferred Work

- Global boot, stop, lifecycle reports, hook invocation, and frontend boot
  connection are declarations only until Stage 06.
- Concrete built-in facilities and their Kconfig demand traits land with their
  owning subsystem stages.
- Concrete subsystem operations, state policies, and `Subsystem::Of` wrappers
  land with their owning subsystem stages.
- Frontend hot-path benchmarking belongs to the first concrete atomic metric or
  ISR-capable subsystem that uses this machinery.
- A Clang verification lane requires a Clang toolchain in the development or CI
  environment.

## 13. Documentation Handoff

The later public documentation pass should explain:

- the composition-root and ordinary component-header layout;
- order-independent Blueprint sections and omitted empty sections;
- how subsystem declarations remain separate from subsystem configuration;
- built-in enabled-versus-present semantics;
- default and alternate application binding;
- relaxed runtime error timing and exact per-operation overhead;
- strict out-of-line and optional lazy-inline source shapes;
- distributed `System::StateSlot` ownership;
- that boot declarations become usable only after lifecycle implementation.

`tests/host/system_fixture.hpp`, `system.cpp`, `system_inline_no_root.cpp`, and
`system_strict_tu.cpp` are the executable examples to adapt.

## 14. Local Implementation Decisions

### Generic custom catalog sections

Problem: converting only the concrete `CatalogSection<Tag, ...>` template would
make `section_traits` extension cosmetic rather than functional.

Constraints: subsystem sections must remain open without a central subsystem
switch.

Options considered: specialize conversion for every Solar section; require all
subsystems to alias `CatalogSection`; convert from trait-provided tag and entry
types.

Decision: convert every `SubsystemCatalog` role through
`section_traits<Section>::CatalogTag` and `Entries`.

Why: this preserves the locked extension protocol and still produces the one
generic `DirectDeclarations` form used by Stage 02.

Physical implementation: `include/solar/system/blueprint.hpp` and the custom
`AlphaSection` in `tests/host/system_fixture.hpp`.

Tests/evidence: host and Zephyr fixtures collect direct and component-contributed
alpha declarations through the custom section.

Reversal path: a future constrained section concept can replace the private
converter without changing Blueprint or catalog types.

### Built-in closure order

Problem: fixed-point discovery can append a prerequisite after the component
that requested it, making final component order depend on discovery passes.

Constraints: closure must be finite and deterministic while candidate order is
the built-in owner's canonical order.

Options considered: accept discovery order; topologically sort requirements;
close membership then filter the candidate list.

Decision: compute membership to a fixed point, then restore candidate order by
filtering candidates against the selected set.

Why: deterministic ordering is simple, bounded, and independent of the pass in
which a requirement was found. Component dependency order remains a separate
lifecycle concern.

Physical implementation: `OrderSelectedBuiltins` in
`include/solar/system/blueprint.hpp`.

Tests/evidence: the support facility appears before its demanding facility even
though it is discovered on the following closure pass.

Reversal path: replace only the ordering metafunction if a later specification
requires topological lifecycle order in the effective list.

### Effective graph validation timing

Problem: validating dependencies against user components before selected
built-ins exist rejects a user component that names a facility explicitly
present in the effective Blueprint.

Constraints: dependencies must never add components implicitly, and duplicate
components still need an early focused error.

Options considered: ban dependencies on built-ins; pre-add all candidates;
validate uniqueness early and full dependencies after closure.

Decision: reject duplicate user components early and validate dependency
presence/cycles against final effective components.

Why: only facilities actually selected by normalization satisfy dependencies;
unused enabled candidates remain absent.

Physical implementation: normalization ordering in
`include/solar/system/blueprint.hpp`.

Tests/evidence: `Controller` depends on the required-derived `SupportFacility`;
missing and cyclic dependency fixtures still fail with focused tokens.

Reversal path: add a separate declared built-in-requirement relation for user
components if a future design chooses to let dependencies drive inclusion.

### Subsystem configuration extension

Problem: generic normalization must validate policy ownership and precedence
without knowing concrete subsystem policies.

Constraints: configuration cannot become an untyped global property pool, and
Kconfig-disabled capability cannot be re-enabled locally.

Options considered: central policy registry; policies self-identify by nested
members; subsystem-specialized policy and configuration traits.

Decision: use `subsystem_policy_traits<Tag, Policy>` for recognition, exclusive
axis, and optional availability, plus
`subsystem_configuration_traits<Tag>::validate<List>` for combinations.

Why: the owning subsystem keeps all policy meaning while generic normalization
can enforce shape, exclusivity, and precedence.

Physical implementation: `sections.hpp`, `blueprint.hpp`, and System
configuration queries in `system.hpp`.

Tests/evidence: valid override/precedence assertions and four policy diagnostic
fixtures.

Reversal path: concrete subsystem traits can delegate to a richer policy-set
type later without changing authored `SubsystemConfiguration` sections.

### Relaxed availability state

Problem: separate ready and disabled booleans require two loads and permit
contradictory transient combinations.

Constraints: one readiness check and one pointer load, no universal lock, and
explicit `NotReady`, `Disabled`, and `NotRegistered` results.

Options considered: two atomic booleans; nullable pointer sentinels; one atomic
availability enum plus operation pointer.

Decision: use one byte-sized atomic availability enum and then load the typed
operation pointer only when ready.

Why: state is unambiguous, disabled cannot call a stale target, and the common
path matches the accepted overhead model.

Physical implementation: `include/solar/system/frontend.hpp`.

Tests/evidence: host/native pre-bind, disabled, absent, registered, multi-TU,
and size/symbol evidence.

Reversal path: a measured hot-path handle can bypass repeated availability
resolution without changing canonical storage or catalog membership.

### Stable duplicate-binding diagnostic

Problem: GCC's class-specialization redefinition error does not print a token
placed inside the specialization body.

Constraints: duplicate bindings must remain a language-level error and compile
to no runtime owner.

Options considered: accept compiler wording; generated binding registry;
per-application variable-template specialization marker.

Decision: the binding macro specializes a diagnostic-named inline constexpr
variable template before specializing `system_binding`.

Why: duplicate specializations reliably print
`SOLAR_DIAGNOSTIC_DUPLICATE_SYSTEM_BINDING` and optimize to no storage.

Physical implementation: `include/solar/system/binding.hpp`.

Tests/evidence: the duplicate-binding expected-failure test matches the stable
token.

Reversal path: remove the marker if future compiler diagnostics or generated
composition provide a stronger first-class duplicate diagnostic.

### Lifecycle names without placeholder behavior

Problem: Stage 03 must establish names needed by later lifecycle definition but
does not own report types or boot behavior.

Constraints: no fake success, hidden runtime, or premature report design.

Options considered: omit all declarations; invent incomplete report wrappers;
declare deduced-return function templates for later definition.

Decision: declare accepted global, explicit System, and focused lifecycle names
with deduced return types and no definitions.

Why: include and naming contracts exist, but calls cannot compile until Stage 06
provides genuine definitions and return types.

Physical implementation: `include/solar/system/api.hpp` and declarations in
`include/solar/system/system.hpp`.

Tests/evidence: standalone public-header checks pass; no lifecycle symbol or
runtime behavior appears in host/native images.

Reversal path: Stage 06 defines these templates and members in place using its
accepted Result/report contract.

## 15. Closure Statement

Stage 03 is complete. A representative robot Blueprint normalizes identically
across section order, contributes through an open subsystem extension, derives
the correct built-ins, validates configuration and graph structure, exposes one
canonical static System state, and supports the same typed operation API in
relaxed and strict Zephyr builds. All Stage 00-03 tests are green. Stage 04
Kernel Core Primitives is unblocked.
