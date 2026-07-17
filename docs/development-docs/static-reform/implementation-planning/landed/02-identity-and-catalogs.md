# Stage 02: Identity, Contributions, And Catalogs

Status: landed

Landed date: 2026-07-15

Implementation repository/branch: `/workspaces/solar`, `static_reform`

Baseline: Stage 01 landed worktree based on `bed432f pre-solar-implementation`

## 1. Objective

Implement one extensible compile-time information model for Solar declarations.
The model must preserve type identity, semantic owner, registration origin,
local and external identity, immutable metadata, and subsystem extension
without introducing runtime registration, a central subsystem switch, or a
System object.

## 2. Specification Coverage

| Specification | Coverage | Notes |
| --- | --- | --- |
| `02-identity-contributions-and-catalogs.md` | common identity, descriptor traits, contributions, provenance, catalog traits, catalog sets, expansion, dependencies, filtering, views, lookup, diagnostics | complete for generic Stage 02 machinery |
| `01-system-blueprint-and-binding.md` | contribution and include-direction prerequisites | System, binding, and effective-blueprint formation remain Stage 03 |
| `00-design-conventions.md` | static ownership, Kconfig metadata policy, no runtime registry | complete |
| Stage 02 of `04-implementation-roadmap.md` | all declared implementation and verification scope | complete |

Subsystem descriptor payloads, stable-ID policies, capacities, and runtime
records remain with their owning subsystem stages.

## 3. Public Surface Landed

### Identity and metadata

- `solar::LocalId<Tag, Rep>` is dense, typed, bounded, and has an explicit
  invalid sentinel.
- `solar::StableId<IdentityDomain, Rep>` separates external identity domains.
- `solar::descriptor_traits<Tag, Declaration>` defaults to
  `Declaration::descriptor` and supports explicit third-party specialization.
- `solar::catalog_traits<Tag>` is the subsystem extension point for descriptor
  type, descriptor view, identity domain, validation, stable-ID requirement,
  expansion, dependencies, and view materialization.
- `solar::component::Descriptor` provides the compact common component form.

### Contributions and provenance

- `solar::Contribution<Tag, Declarations...>` is the generic contribution
  group.
- `solar::Contributions<Groups...>` is the optional generic multi-group form.
- `solar::contribution_source<Tag, Component>` is the subsystem-owned reserved
  alias adapter.
- Built-in declaration headers own adapters for `Parameters`,
  `ParameterChanges`, `Events`, `Metrics`, `Messages`, `Subscriptions`, `Jobs`,
  Remote aliases, and Health `Checks`.
- `ApplicationOwner`, `BuiltinOwner`, typed origins, `OwnerView`, and
  `OriginKind` preserve semantic provenance.

### Catalog construction and use

- `DirectDeclarations<Tag, ...>` and `DirectCatalogs<...>` represent direct
  application declarations before Stage 03 Blueprint normalization.
- `collect_catalog_t` merges direct and component-contributed declarations.
- `collect_catalog_set_t` collects a finite candidate tag set and validates
  generic extension tags.
- `CatalogEntry` retains tag, declaration, owner, origin, dense local ID, and
  component-owner local ID.
- `Catalog<Tag, Entries...>` provides membership, typed entry/descriptor lookup,
  deterministic size/order, static descriptor spans, and bounded local/stable
  numeric lookup.
- `CatalogSet<Catalogs...>::Of<Tag>` is the generic multi-catalog surface used
  by Stage 03 System.
- `CatalogReference<Tag, Declaration>` and
  `resolve_catalog_reference_t` resolve typed cross-catalog references.
- `filter_catalog_t` returns complete entries, preserving owner and origin.

Representative authored shape:

```cpp
struct Controller
{
    static constexpr solar::component::Descriptor descriptor{
        .name = "controller",
    };

    using Metrics = solar::metrics::Contribute<LoopDuration>;
    using Events = solar::events::Contribute<DeadlineMissed>;
};
```

## 4. Runtime Ownership

| Owner | Resource | Capacity | Synchronization | Lifetime |
| --- | --- | --- | --- | --- |
| `Catalog<Tag, ...>` | immutable descriptor-view array | exactly catalog size | none | inline static program lifetime, only retained when referenced |
| `Catalog<Tag, ...>` | optional stable-ID lookup values | exactly catalog size | none | inline static program lifetime, only retained with catalog runtime use |

There is no heap use, runtime registration, constructor, boot hook, mutable
global state, thread, stack, workqueue, timer, poll object, mutex, or lock.
Local lookup is indexed and bounded. Stable-ID lookup is a bounded linear scan
over the exact static catalog size.

Multi-translation-unit tests prove one canonical descriptor array address.

## 5. Compile-Time Behavior

Collection order is deterministic:

1. direct declarations in authored order;
2. components in effective component order;
3. conventional contribution entries in alias order;
4. generic contribution entries in group order;
5. explicit expansion outputs in trait-declared order.

Collection first creates pending entries carrying owner and origin, performs
finite expansion with cycle detection, then assigns dense local IDs. Component
owner IDs are the indices of the supplied effective component list.

Catalog validation covers:

- missing, wrong-kind, unnamed, empty-name, and catalog-invalid descriptors;
- typed stable-ID domains and declaration-specific required IDs;
- duplicate types, names, and stable IDs;
- duplicate direct/contributed, conventional/generic, and conflicting-owner
  registrations;
- malformed reserved aliases and generic groups;
- unregistered extension tags;
- absent catalog dependencies and dependency cycles;
- shared identity-domain collisions across catalog kinds;
- local-ID representational capacity;
- strict compile-time lookup of unregistered declarations.

Catalog kinds own alias probing in their contribution headers. The collector
contains no built-in subsystem branch.

## 6. Error And Availability Behavior

Architecture errors are focused compile-time failures with stable
`SOLAR_DIAGNOSTIC_*` tokens.

Runtime numeric lookup returns:

```cpp
solar::Result<std::reference_wrapper<const DescriptorView>,
              solar::catalog::LookupError>
```

Implemented errors are `UnknownLocalId`, `UnknownStableId`, `Unavailable`, and
`MalformedIdentifier`; the current concrete catalog lookup paths produce the
first two. Unavailable catalogs remain compile-time failures through
`CatalogSet::Of<Tag>` until bound relaxed runtime frontends land.

## 7. Zephyr And Kconfig Integration

Catalog mechanics use no Zephyr runtime API. The test builds use C++23, full
libcpp, no exceptions, and no RTTI.

`CONFIG_SOLAR_DESCRIPTOR_STRINGS=n` strips names and descriptions from
materialized runtime views while compile-time authored descriptors remain
available for validation. This does not alter declaration types, stable IDs,
catalog order, local IDs, ownership, or lookup.

Native binary evidence:

| Variant | text | data | bss | total |
| --- | ---: | ---: | ---: | ---: |
| descriptor strings retained | 43,952 B | 1,285 B | 6,403 B | 51,640 B |
| descriptor strings stripped | 44,163 B | 901 B | 6,403 B | 51,467 B |

The stripped ELF contains none of the fixture's `alpha.*` descriptor strings.
These are complete Ztest image sizes, not Solar-only attribution.

## 8. Files Landed

### Generic catalog foundation

- `include/solar/catalog.hpp`
- `include/solar/catalog/identity.hpp`
- `include/solar/catalog/descriptor.hpp`
- `include/solar/catalog/contribution.hpp`
- `include/solar/catalog/provenance.hpp`
- `include/solar/catalog/catalog.hpp`
- `include/solar/catalog/collection.hpp`
- `include/solar/catalog/builtins.hpp`
- `include/solar/component.hpp`

### Subsystem-owned contribution declarations

- `include/solar/parameters/contribution.hpp`
- `include/solar/events/contribution.hpp`
- `include/solar/metrics/contribution.hpp`
- `include/solar/bus/contribution.hpp`
- `include/solar/execution/contribution.hpp`
- `include/solar/remote/contribution.hpp`
- `include/solar/health/contribution.hpp`

### Tests

- `tests/host/catalog_fixture.hpp`
- `tests/host/catalog.cpp`
- `tests/host/catalog_tu.cpp`
- `tests/host/catalog_link_removal.cpp`
- 17 catalog compile-failure fixtures under `tests/compile_fail/fixtures/`
- `tests/zephyr/catalog/`

The active `solar.hpp` aggregate now includes the catalog foundation.

## 9. Tests And Evidence

| Command | Result | Evidence |
| --- | --- | --- |
| host configure/build plus `ctest --test-dir build/host --output-on-failure` | 25/25 pass | Stage 00-01 regressions, catalog runtime/constexpr behavior, link removal, and 17 focused catalog failures |
| public-header standalone syntax loop with `c++ -std=c++23 -fsyntax-only` | pass | every catalog and contribution header is self-contained |
| host fixture compile with `-Wall -Wextra -Wpedantic` | pass, no warnings | warning-clean generic extension and multi-TU use |
| `west twister -T tests/zephyr -p native_sim/native/64 --inline-logs --outdir build/twister-stage02-final` | 6/6 configurations, 10/10 cases pass, no warnings | all prior native variants plus retained/stripped catalog behavior |
| `west build -d build/catalog-teensy40` after a pristine Teensy configure | pass | ARM GCC 14.3 target compilation and link |
| Teensy catalog Ztest image | FLASH 45,440 B; RAM 5,760 B | target image resource record |
| `strings` scan of host link-removal executable | no fixture descriptor strings | unreferenced runtime arrays and descriptor text are link-removable |
| retained/stripped native ELF `strings` scan | strings present/absent as configured | Kconfig metadata stripping works |
| `git diff --check` | pass | patch hygiene |

The representative host catalog includes 64 validated entries. Separate tests
cover empty catalogs, two extension subsystem tags, component catalogs,
independent and shared identity domains, cross-catalog references, explicit
expansion, and canonical arrays across translation units.

## 10. Local Decisions

### 10.1 Pending entries before local-ID assignment

Problem: expansion and merged contribution sources can change final catalog
length and order after ownership has already been established.

Options considered: assign and renumber IDs during each transformation; flatten
to ownerless declaration lists; or use a provenance-carrying pending entry.

Decision: collect `PendingCatalogEntry` values, expand them while preserving
owner/origin, then finalize once into `CatalogEntry` with dense IDs.

Physical implementation: `catalog/collection.hpp`.

Evidence: deterministic ID, owner, expansion-origin, direct, conventional, and
generic ordering assertions in `tests/host/catalog.cpp`.

Reversal path: replace the internal pending representation while preserving the
observable order and finalized `CatalogEntry` contract.

### 10.2 Component owner IDs come from component order

Problem: immutable runtime owner views need a bounded component reference before
the Stage 03 System surface exists.

Decision: contribution collection receives the effective component list and
uses each component's index as its component-catalog local ID. Direct entries
use application ownership.

Why: Stage 03 builds the component catalog from that same normalized order, so
no hash, runtime map, or root object is needed.

Physical implementation: `ComponentContributionEntries` and
`CatalogEntry::owner_view`.

Reversal path: Stage 03 can pass an explicit owner-ID mapping if effective
component ordering ever diverges, without changing descriptor consumers.

### 10.3 Bounded linear stable-ID lookup

Problem: stable lookup needs no heap and should not retain an unnecessary hash
table for small firmware catalogs.

Decision: retain one optional numeric ID per materialized entry and perform a
bounded linear scan. Dense local lookup remains direct indexed access.

Physical implementation: `Catalog::find(StableId)` in `catalog/catalog.hpp`.

Evidence: host and Zephyr success/unknown lookup tests.

Reversal path: catalog traits may later select a constexpr sorted index or
perfect-hash artifact while preserving the public typed `Result` contract.

### 10.4 Subsystem-owned reserved aliases

Problem: placing every built-in adapter in one collector-adjacent file would
mechanically work but blur subsystem ownership and invite a central probe list.

Decision: each subsystem contribution header defines its tags, aliases, and
`contribution_source` specializations. `catalog/builtins.hpp` only aggregates
those headers.

Evidence: fake `alpha` and `beta` subsystems add adapters without changing any
generic collector file; built-in aliases are compile-time asserted.

Reversal path: move an adapter with its subsystem aggregate without changing
the collector or component syntax.

## 11. Specification Refinement

Observed contract: `02-identity-contributions-and-catalogs.md` retained a
transitional `T::Name` descriptor adapter.

Evidence: the later accepted hard-migration policy in the implementation
roadmap and tracker explicitly forbids deprecated aliases and compatibility
adapters; Stage 00 removed the old architecture.

Accepted change: descriptor resolution is explicit trait specialization, then
`T::descriptor`, then a focused compile error. There is no `T::Name` adapter.

Specifications updated:
`02-identity-contributions-and-catalogs.md`, Sections 5.8, 25.1, and 28.

Verification added: missing descriptors fail with
`SOLAR_DIAGNOSTIC_MISSING_DESCRIPTOR`; ordinary component and external-trait
paths compile and run.

## 12. Firmware Impact And Deferred Work

No firmware migration is required at this gate. The catalog fixture compiles
for Teensy, but the application System does not exist until Stage 03 and the
firmware integration gate remains Stage 06.

Accepted later work:

- Blueprint section classification, effective candidate tags, Kconfig-disabled
  contribution validation, System catalog exposure, and application bindings
  land in Stage 03.
- Subsystem descriptor fields, stable-ID requirements, capacity Kconfig, and
  runtime record stores land with their owning subsystems.
- Generated stable-ID manifest tooling remains deferred by specification.
- Cross-catalog subsystem adapters may materialize source local IDs in their
  own descriptor views using the landed typed reference resolver.

## 13. Documentation Handoff

Public documentation should distinguish authored descriptors from enriched
entries and mutable records; explain direct versus contributed ownership;
document strong local/stable identity domains; show conventional aliases as the
normal path and generic Contributions as an extension; and explain that
catalogs are immutable, boot-independent, allocation-free metadata.

`tests/host/catalog_fixture.hpp` and `tests/host/catalog.cpp` are the executable
reference examples for third-party traits, custom subsystem tags, expansion,
cross-catalog references, filtering, views, and lookup.

## 14. Closure Statement

Stage 02 is complete. Two fake subsystem kinds collect through the same generic
machinery with no central collector modification; identity, ownership,
provenance, validation, immutable views, and numeric lookup are verified on
host, native Zephyr, and Teensy. Stage 03 is now unblocked.
