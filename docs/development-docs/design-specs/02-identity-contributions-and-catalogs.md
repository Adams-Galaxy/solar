# Identity, Descriptors, Contributions, And Catalogs

Date: 2026-07-15

Status: accepted design

Owning phase: Phase 2

Depends on:

- `00-design-conventions.md`
- `00a-modern-cpp-result-and-status.md`
- `01-system-blueprint-and-binding.md`

## 1. Purpose

This specification defines the compile-time information model shared by Solar
components and subsystems.

It establishes:

- C++ type identity as the primary local identity;
- compact user-authored `Descriptor` metadata;
- tag-aware descriptor customization;
- typed build-local and stable external identifiers;
- one generic contribution language;
- automatic preservation of semantic owner and registration origin;
- immutable, validated compile-time catalogs;
- bounded descriptor views for runtime inspection;
- cross-catalog references without catalog ownership cycles;
- one extensible `System::catalog` surface;
- strict validation during effective-blueprint formation.

The common application path must remain small:

```cpp
struct DriveKp
{
    using Value = float;

    static constexpr solar::parameters::Descriptor descriptor{
        .name = "drive.pid.kp",
    };

    static constexpr Value default_value = 1.2f;
};

auto kp = solar::parameters::get<DriveKp>();
```

Catalog machinery exists to validate, inspect, and connect the complete
firmware architecture. It must not become ceremony required at ordinary call
sites.

## 2. Non-Goals

This specification does not define:

- the full metadata payload of every subsystem descriptor;
- parameter value storage or synchronization;
- event record storage or delivery;
- metric instrument storage;
- bus dispatch and subscription execution;
- logging record formats and sink behavior;
- Remote protocol encoding;
- lifecycle record fields;
- runtime-mutable registration;
- compiler-reflection-based discovery;
- automatic discovery of arbitrary unregistered nested aliases;
- automatic wire IDs derived from compiler type names;
- a universal all-system runtime snapshot.

Later subsystem specifications extend the common model with their own
descriptor fields, policy, storage, records, and runtime operations.

## 3. Architectural Decisions

### 3.1 Types identify declarations

The concrete C++ type is authoritative for compile-time membership, lookup,
dependency, contribution, and uniqueness.

```cpp
DriveKp
ControlDeadlineMissed
Navigation
```

A name, local integer, stable integer, or descriptor object is metadata about
that declaration. None replaces its C++ type inside application code.

### 3.2 Authored metadata is called `Descriptor`

The immutable metadata written by a declaration author is named `Descriptor`:

```cpp
static constexpr solar::component::Descriptor descriptor{
    .name = "navigation",
};
```

Solar does not expose `DescriptorSpec` as public vocabulary.

### 3.3 Catalog enrichment does not mutate the descriptor

The authored descriptor remains the semantic metadata of its declaration.
Solar adds catalog facts through a `CatalogEntry`, including:

- local ID;
- semantic owner;
- registration origin;
- catalog kind;
- resolved cross-catalog references where applicable.

This avoids treating user-authored metadata and system-derived facts as one
large aggregate.

### 3.4 Conventional aliases are the common contribution API

A component normally contributes subsystem declarations through concise,
subsystem-owned aliases:

```cpp
using Metrics = solar::metrics::Contribute<...>;
using Events = solar::events::Contribute<...>;
using Parameters = solar::parameters::Contribute<...>;
```

Each subsystem owns the adapter that recognizes its conventional alias. The
central collector remains generic and normalizes every recognized alias into
the same contribution representation.

An optional generic `Contributions` alias remains available for extension and
generated use, but common components do not need to compose conventional
aliases into it.

### 3.5 Ownership survives collection

Collection never flattens declarations into an ownerless type list. The
component that contributes a declaration remains its semantic owner in the
final catalog.

### 3.6 Catalogs describe; facilities store

Catalogs own no mutable runtime state. They describe the validated firmware
vocabulary. Facilities and components own mutable values and records.

### 3.7 Local and external identity stay separate

Dense local IDs optimize one firmware build. Stable IDs preserve contracts
across builds. Human-readable names support people and tooling. These identity
layers are never implicitly interchangeable.

## 4. Common User Experience

### 4.1 Minimal component

```cpp
#pragma once

#include <solar/device.hpp>

struct Imu
{
    static constexpr solar::component::Descriptor descriptor{
        .name = "imu",
    };

    static solar::Result<void> init();
    static solar::Result<Sample> sample();
};
```

Only the name is required for a normal component. Description and other
metadata remain optional.

### 4.2 Component with direct dependencies

```cpp
#include "devices/imu.hpp"
#include "devices/motors.hpp"

#include <solar/service.hpp>

struct Navigation
{
    static constexpr solar::component::Descriptor descriptor{
        .name = "navigation",
        .description = "Robot navigation and motion control",
    };

    using Dependencies = solar::Dependencies<
        Imu,
        LeftMotor,
        RightMotor>;

    static solar::Result<void> init();
    static solar::Result<void> start();
    static void run(solar::StopToken stop);
    static solar::Result<void> stop();
};
```

Descriptor and dependency declarations do not provide access. Navigation calls
its included dependency types directly.

### 4.3 Internal parameter

```cpp
struct DriveKp
{
    using Value = float;

    static constexpr solar::parameters::Descriptor descriptor{
        .name = "drive.pid.kp",
    };

    static constexpr Value default_value = 1.2f;
};
```

### 4.4 Persistent parameter

```cpp
struct DriveKi
{
    using Value = float;

    static constexpr solar::parameters::Descriptor descriptor{
        .name = "drive.pid.ki",
        .stable_id = solar::parameters::Id{0x81A41E2C},
    };

    static constexpr Value default_value = 0.1f;
};
```

Stable identity is required because persistence must recognize the parameter
across firmware builds and source refactors.

### 4.5 Event and metric declarations

```cpp
struct ControlDeadlineMissed
{
    static constexpr solar::events::Descriptor descriptor{
        .name = "navigation.control.deadline_missed",
        .stable_id = solar::events::Id{0x2001},
    };

    std::uint32_t elapsed_us;
    std::uint32_t budget_us;
};

struct DeadlineMisses
{
    using Value = std::uint32_t;

    static constexpr solar::metrics::Descriptor descriptor{
        .name = "navigation.control.deadline_misses",
    };
};
```

The event requires stable identity because it is structured observability data.
The internal metric may use only build-local identity until a later policy
exposes it externally.

### 4.6 Component contributions

```cpp
struct Navigation
{
    static constexpr solar::component::Descriptor descriptor{
        .name = "navigation",
    };

    using Events = solar::events::Contribute<
        ControlDeadlineMissed>;

    using Metrics = solar::metrics::Contribute<
        DeadlineMisses>;
};
```

One contribution kind is equally direct:

```cpp
struct RemoteTransport
{
    using Metrics = solar::metrics::Contribute<
        FramesReceived,
        FramesTransmitted,
        FramesDropped>;
};
```

### 4.7 Optional generic contributions

`Contributions` is retained for generic component templates, generated code,
third-party catalog kinds, and experimental extensions:

```cpp
struct GeneratedAdapter
{
    using Contributions = solar::Contributions<
        custom::Contribute<Foo>,
        experimental::Contribute<Bar>>;
};
```

Conventional aliases and `Contributions` may coexist. They are normalized into
one stream, and duplicate declarations remain compile-time errors.

### 4.8 Ordinary runtime use

```cpp
auto kp = solar::parameters::get<DriveKp>();
auto changed = solar::parameters::set<DriveKp>(1.4f);

solar::events::observe<ControlDeadlineMissed>({
    .elapsed_us = elapsed,
    .budget_us = budget,
});

solar::metrics::inc<DeadlineMisses>();
```

Ordinary operations do not mention a catalog, owner wrapper, facility type, or
system object.

## 5. Descriptor Model

### 5.1 Descriptor meaning

A descriptor is immutable metadata authored with or for one declaration type.
It may contain:

- human-readable name;
- description;
- optional stable external ID;
- schema or declaration version;
- subsystem-specific metadata;
- explicit declaration policy that is intrinsic metadata.

A descriptor must not contain:

- mutable values;
- counters;
- lifecycle state;
- timestamps;
- queue depths;
- kernel handles;
- runtime ownership objects;
- build-local catalog IDs authored by the user.

### 5.2 Subsystem-specific descriptor types

Each catalog kind owns its descriptor type:

```cpp
solar::component::Descriptor
solar::parameters::Descriptor
solar::events::Descriptor
solar::metrics::Descriptor
solar::remote::ActionDescriptor
```

The common architecture does not force every subsystem into one oversized
descriptor aggregate. Shared identity fields should use common vocabulary and
strong ID types, while domain metadata remains domain-specific.

### 5.3 Compact construction

Descriptor types should support a one-line minimal form:

```cpp
static constexpr solar::component::Descriptor descriptor{
    .name = "left_motor",
};
```

Subsystems may provide ergonomic definition templates or helpers later:

```cpp
using DeadlineMisses = solar::metrics::Counter<
    "navigation.control.deadline_misses",
    std::uint32_t>;
```

Such helpers must still normalize to the same descriptor protocol.

### 5.4 Required fields

The common component descriptor requires:

- a non-empty name.

Descriptions are optional. Stable IDs are optional unless persistence,
protocol, external exposure, or subsystem policy requires one.

Each subsystem specification defines any additional required metadata.

### 5.5 Names are values, not identity types

Names should be constexpr string metadata, normally represented through
`std::string_view` or a compatible allocation-free view.

Solar no longer requires every declaration to expose:

```cpp
using Name = solar::Name<"...">;
```

`FixedString` remains useful for descriptor helpers and non-type template
parameters, but a `Name` type is not the universal identity model.

### 5.6 Descriptor access

Solar accesses authored descriptors through a tag-aware trait:

```cpp
template<typename CatalogTag, typename T>
struct descriptor_traits;
```

The normal default reads:

```cpp
T::descriptor
```

Conceptually:

```cpp
template<typename CatalogTag, typename T>
    requires requires { T::descriptor; }
struct descriptor_traits<CatalogTag, T>
{
    static constexpr auto descriptor = T::descriptor;
};
```

The exact constrained implementation may differ to improve diagnostics.

### 5.7 External customization

Third-party types that cannot be edited use explicit trait specialization:

```cpp
template<>
struct solar::descriptor_traits<
    solar::component::Tag,
    VendorImu>
{
    static constexpr solar::component::Descriptor descriptor{
        .name = "imu",
        .description = "Board IMU",
    };
};
```

This is an integration escape hatch, not common application syntax.

### 5.8 Descriptor resolution order

Solar resolves descriptor metadata in this order:

```text
explicit descriptor_traits<CatalogTag, T> specialization
    > T::descriptor
    > compile-time error
```

There is no compiler-generated type-name fallback.

There is no transitional `T::Name` adapter. The hard migration policy forbids
compatibility layers for the removed architecture; old declarations must move
to descriptors explicitly.

### 5.9 Tag-aware customization

Descriptor lookup includes the catalog tag because one type may legitimately
appear in more than one catalog role. A payload type, for example, may also be
described as a Remote schema type.

Tag-aware customization prevents one role's metadata from being mistaken for
another role's metadata.

## 6. Identity Model

### 6.1 C++ type identity

Within C++, the declaration type is authoritative:

```cpp
solar::parameters::get<DriveKp>();
solar::metrics::inc<DeadlineMisses>();
```

Catalog membership is type-based. Two different types remain different even if
their names or metadata are otherwise equal; duplicate names are then diagnosed
as a separate catalog conflict.

### 6.2 Typed local IDs

Every normalized catalog entry receives a dense build-local ID:

```cpp
solar::LocalId<solar::parameters::Tag>
solar::LocalId<solar::events::Tag>
```

Properties:

- assigned by Solar, never authored by the user;
- dense from zero within one catalog;
- strongly typed by catalog kind;
- suitable for arrays, records, and bounded lookup;
- invalid values are explicit and cannot alias a valid index;
- may change whenever the effective blueprint changes;
- prohibited as a persistence or wire contract.

The representation should use the narrowest configured unsigned type that can
represent the maximum catalog capacity, normally `std::uint16_t` unless a
subsystem has a stronger reason.

### 6.3 Local ID ordering

Local ID assignment is deterministic:

1. direct blueprint declarations in their declared order;
2. component contributions in effective component catalog order;
3. entries within each contribution in declared order;
4. derived entries in deterministic rule order.

Section order remains irrelevant because Phase 1 normalizes section kinds
before catalog construction.

Catalog order is not an external compatibility guarantee.

### 6.4 Stable IDs

Stable external identity uses a strong domain type:

```cpp
solar::StableId<solar::parameters::IdentityDomain>
solar::StableId<solar::events::IdentityDomain>
```

Subsystems may expose shorter aliases:

```cpp
solar::parameters::Id
solar::events::Id
```

Stable IDs are:

- explicit numeric constants; or
- generated constants from a checked, version-controlled manifest.

The generated result is still explicit firmware input. It is not recomputed
silently from compiler strings or current source names on every build.

### 6.5 When stable IDs are required

Stable IDs are required for at least:

- persistent parameters;
- structured observability events;
- Remote schema types;
- Remote Data and Actions;
- Remote Topics and Streams;
- externally visible bus message schemas;
- persistent structured logs or diagnostic records.

Later specs may require them for additional declarations.

Internal-only components, metrics, jobs, subscriptions, and log sources may use
only type identity and local IDs unless exposed or persisted.

### 6.6 Forbidden ID derivation

The following must not become external identity:

- hashes of compiler type-name strings;
- hashes of mangled names;
- source-location hashes;
- current catalog indices;
- unmanifested hashes of human-readable names;
- combinations such as `hash(name) ^ version`.

Name hashing may be used as an internal implementation accelerator only when
the original catalog still validates collisions and the hash is never exposed
as stable identity.

### 6.7 Stable ID and version are separate

A declaration's stable ID identifies the semantic entity. Its schema version
identifies the current representation or contract version.

Compatible version evolution normally preserves the stable ID. A deliberate
breaking replacement may receive a new stable ID.

The Remote and persistence specs will define compatibility and migration rules,
but must not implicitly change identity whenever the version changes.

### 6.8 Identity domains

Every catalog kind declares an identity domain through catalog traits. Stable
ID uniqueness is enforced across that domain.

Separate catalog kinds may deliberately share an identity domain when a wire
protocol requires one global endpoint namespace. Phase 10 uses that generic
capability while fixing distinct typed domains for Remote Schemas, Data,
Actions, Topics, and Streams. Data capability operations share their parent
Data identity plus an operation kind.

## 7. Name And Uniqueness Rules

### 7.1 Human-readable names

Names support:

- inspection;
- logs and diagnostics;
- generated documentation;
- shell and tooling lookup;
- manifests;
- optional external protocol metadata.

A name does not replace the C++ type or stable ID.

### 7.2 No automatic owner prefixing

Solar does not rewrite names by automatically prepending owner names.

```text
navigation + control.duration
```

must not silently become the external identity:

```text
navigation.control.duration
```

Applications may choose qualified names explicitly. Inspection displays owner
and declaration name as separate fields.

This prevents owner renaming from rewriting every declaration it owns.

### 7.3 Default uniqueness scopes

| Declaration kind | Type/name uniqueness scope | Stable ID scope |
| --- | --- | --- |
| Components | complete component catalog | component identity domain if used |
| Parameters | parameter catalog | parameter identity domain |
| Parameter change hooks | change-registration catalog and key | normally local only |
| Events | event catalog | event identity domain |
| Metrics | metric catalog | metric identity domain if externally identified |
| Bus message types | bus message catalog | bus schema identity domain if exposed |
| Bus subscriptions | subscription catalog and registration key | normally local only |
| Executors | complete component catalog | normally local only |
| Jobs/tasks | execution catalog | execution identity domain if exposed |
| Log sources | log-source catalog | log-source identity domain if exported |
| Log categories | owning log source | normally local or source-qualified |
| Remote schema types | Remote type catalog | Remote type identity domain |
| Remote Data | Remote Data catalog | Data identity domain |
| Remote Actions | Remote Action catalog | Action identity domain |
| Remote topics | Remote topic catalog | topic identity domain |
| Remote streams | Remote stream catalog | stream identity domain |

Phase 10 fixes these as distinct typed external identity domains. Data
capabilities share their parent Data ID plus an operation kind rather than
receiving unrelated generated endpoint IDs.

### 7.4 Subscription uniqueness

Bus subscriptions are leaf registrations. At minimum:

- the concrete registration type must be unique;
- an exact duplicate subscription key is rejected;
- subsystem policy defines whether one handler may register multiple distinct
  delivery policies for one message type.

The bus specification defines the complete registration key.

### 7.5 Rename behavior

Renaming human-readable metadata does not change explicit stable identity.

If a protocol uses the name itself as an external lookup key, that name carries
an additional documented stability contract. Renaming it is then a protocol
change even though the numeric stable ID remains unchanged.

Generated manifests should detect unexpected rename, ID reuse, and deletion so
the change is deliberate.

## 8. Contribution Model

### 8.1 Generic contribution group

The generic form is conceptually:

```cpp
template<typename CatalogTag, typename... Declarations>
struct Contribution
{
    using Tag = CatalogTag;
    using Entries = solar::TypeList<Declarations...>;
};
```

Each subsystem provides a compact alias:

```cpp
namespace solar::metrics
{

template<typename... Metrics>
using Contribute = solar::Contribution<Tag, Metrics...>;

} // namespace solar::metrics
```

### 8.2 Contribution collection

A component normally exposes conventional aliases defined by its subsystems:

```cpp
using Metrics = solar::metrics::Contribute<A, B, C>;
using Events = solar::events::Contribute<D>;
```

The component may additionally or alternatively expose the generic extension
form:

```cpp
using Contributions = solar::Contributions<Groups...>;
```

One generic group may be assigned directly:

```cpp
using Contributions = custom::Contribute<X, Y>;
```

If no recognized conventional alias or generic alias exists, the component
contributes nothing.

### 8.3 Subsystem-owned extraction adapters

The central collector queries one generic customization point:

```cpp
template<
    typename CatalogTag,
    typename Component,
    typename = void>
struct contribution_source
{
    using type = solar::Contribution<CatalogTag>;
};
```

Each subsystem specializes extraction for its reserved alias. Conceptually,
metrics supplies:

```cpp
template<typename Component>
struct contribution_source<
    solar::metrics::Tag,
    Component,
    std::void_t<typename Component::Metrics>>
{
    using type = typename Component::Metrics;
};
```

The metrics header owns this adapter. Events, parameters, execution, bus, and
Remote define their own adapters in their own subsystem headers.

Adding a subsystem does not add another branch to the central collector or
another positional member to `System`.

### 8.4 Candidate catalog tags

C++23 cannot enumerate arbitrary nested aliases. Solar therefore collects over
a finite compile-time set of candidate catalog tags consisting of:

- all Solar built-in catalog tags;
- tags introduced by blueprint sections;
- explicitly registered extension tags.

Built-in tags remain candidates even when their Kconfig capability is disabled.
This allows Solar to diagnose an intentional contribution to a disabled
subsystem instead of silently ignoring it.

Third-party catalog kinds that exist only through component aliases must be
introduced through blueprint extension metadata or another explicit extension
tag declaration. This is compile-time catalog participation, not runtime
registration.

### 8.5 Reserved alias contract

Conventional alias names are part of their subsystem's component protocol.
Initial expected vocabulary includes forms such as:

```cpp
Metrics
Events
Parameters
ParameterChanges
Messages
Subscriptions
Jobs
RemoteData
RemoteActions
RemoteTopics
RemoteStreams
RemoteLinks
```

The owning subsystem specification fixes its exact aliases. If a component
declares a reserved alias, the alias must normalize to the expected catalog tag
and contribution shape. An incorrectly shaped reserved alias is a focused
compile-time error, not an ignored member.

### 8.6 Generic contribution normalization

Solar separately reads the optional `T::Contributions` alias and groups its
entries by catalog tag. Conventional and generic sources are merged before
owner wrapping and validation.

The generic alias is useful for:

- third-party and experimental catalog kinds;
- generic component templates;
- generated components;
- components whose contribution kind has no conventional alias.

It is not required merely to combine ordinary `Metrics`, `Events`, or
`Parameters` aliases.

### 8.7 Semantic meaning

A contribution states:

> This component semantically owns or provides these declarations as part of
> the firmware vocabulary.

It does not state:

- component dependency;
- runtime access;
- event subscription;
- execution ownership;
- Remote exposure;
- lifecycle ordering;
- runtime storage ownership.

### 8.8 Contribution roots

Components are the normal contribution roots. A declaration contributed by a
component does not recursively contribute additional declarations merely
because it has its own nested aliases.

This prevents surprising recursive discovery.

Explicit expansion declarations, such as a future metric group, may expand
through the owning catalog's normalization rule. Expansion must be finite,
deterministic, and preserve origin.

### 8.9 Direct blueprint declarations

Subsystem sections may declare entries directly:

```cpp
solar::Parameters<DriveKp, DriveKi, DriveKd>
```

Direct entries receive an internal application-owner marker and blueprint
origin automatically. Users do not spell `ApplicationOwner`.

Direct declarations are useful for application-wide vocabulary that has no
natural component owner.

### 8.10 No silent deduplication

The same declaration contributed twice is an error, including:

- contribution by two components;
- direct declaration plus component contribution;
- repeated declaration inside one group;
- two expansion paths producing the same declaration.
- the same declaration supplied through a conventional alias and the generic
  `Contributions` alias.

Solar reports the conflicting owners or origins. It does not silently choose
one owner or discard one declaration.

### 8.11 Referencing is not contributing

A component that updates, observes, handles, or exposes another owner's
declaration references it. It does not contribute it again.

```cpp
Navigation owns DeadlineMisses
Remote exposes DeadlineMisses
```

Remote owns the exposure declaration, while its catalog entry references the
metric entry owned by Navigation.

## 9. Catalog Entry And Provenance

### 9.1 Compile-time entry

Collection normalizes declarations into a form conceptually equivalent to:

```cpp
template<
    typename CatalogTag,
    typename DeclarationT,
    typename OwnerT,
    typename OriginT,
    auto LocalIdValue>
struct CatalogEntry
{
    using Tag = CatalogTag;
    using Declaration = DeclarationT;
    using Owner = OwnerT;
    using Origin = OriginT;

    static constexpr LocalId<CatalogTag> local_id{LocalIdValue};
};
```

The exact implementation may separate pre-ID and finalized entries to simplify
normalization.

### 9.2 Origin categories

Initial origin categories are:

- direct blueprint declaration;
- component contribution;
- Solar built-in declaration;
- derived declaration;
- generated manifest declaration.

Compile-time provenance may carry source types and derivation rules.

### 9.3 Semantic owner

The owner identifies who is responsible for the declaration's meaning.

Normal owners are:

- a component type;
- an internal application-owner marker for direct declarations;
- a Solar built-in component for built-in vocabulary.

Ownership is not inferred from who happens to call the declaration's API.

### 9.4 Derived origin

Derived entries retain their source:

```cpp
DerivedOrigin<
    EventMetricAdapter,
    ControlDeadlineMissed>
```

A derived entry may have its own semantic owner while preserving a reference to
the source declaration that caused it to exist.

### 9.5 Runtime owner view

Runtime descriptor views cannot expose arbitrary C++ types directly. They use a
bounded owner reference, conceptually:

```cpp
struct OwnerView
{
    OwnerKind kind;
    component::Id component;
};
```

`OwnerKind` distinguishes component, application, and other explicitly defined
owner classes. Component-owned entries reference the component's local catalog
ID.

## 10. Catalog Kind Extension

### 10.1 Catalog traits

Each catalog kind defines behavior through a tag specialization:

```cpp
template<>
struct solar::catalog_traits<solar::parameters::Tag>
{
    using Descriptor = solar::parameters::Descriptor;
    using DescriptorView = solar::parameters::DescriptorView;
    using IdentityDomain = solar::parameters::IdentityDomain;

    template<typename Entry>
    static consteval auto make_view();
};
```

The exact member names may be adjusted during implementation, but every catalog
kind must declare:

- its tag;
- authored descriptor protocol;
- runtime view type if materialized;
- identity domain;
- validation rules;
- expansion rules if any;
- catalog dependencies;
- descriptor-view materialization.

### 10.2 Generic catalog

Solar provides a generic immutable catalog:

```cpp
solar::Catalog<Tag, Entries...>
```

A catalog provides:

- compile-time size;
- declaration membership;
- entry lookup by declaration type;
- descriptor lookup;
- owner and origin lookup;
- typed local-ID lookup;
- deterministic iteration order;
- immutable runtime descriptor views when requested.

### 10.3 Catalog set

The system owns one compile-time set:

```cpp
solar::CatalogSet<Catalogs...>
```

New catalog kinds enter through blueprint sections, component contributions, or
derived normalization. Adding a catalog kind does not add another positional
`System` template argument or bespoke collector.

### 10.4 Generic collection

Conceptually:

```cpp
using Metrics = solar::collect_contributions_t<
    EffectiveComponents,
    solar::metrics::Tag>;
```

The actual normalization also merges direct declarations and derived entries,
then validates and assigns local IDs.

## 11. System Catalog Surface

### 11.1 Explicit system access

The named system exposes one catalog namespace-like type:

```cpp
using Parameters =
    RobotSystem::catalog::Of<solar::parameters::Tag>;

static_assert(Parameters::contains<DriveKp>);
static_assert(Parameters::size == 3);

constexpr auto entry = Parameters::entry<DriveKp>();
constexpr auto descriptor = Parameters::descriptor<DriveKp>();
```

Because `RobotSystem` is concrete, ordinary application and test code does not
need dependent-name `::template` syntax.

### 11.2 Generic extension surface

`Of<Tag>` is the authoritative extension mechanism. `System` does not require a
new nested alias for every future subsystem.

Solar may offer convenience aliases for core catalogs where they materially
improve readability, but generic infrastructure must use catalog tags.

### 11.3 Bound global access

Global bound APIs expose immutable descriptor views:

```cpp
solar::catalog::components();
solar::catalog::descriptors<solar::parameters::Tag>();
```

Subsystem namespaces provide the common convenience form:

```cpp
solar::parameters::descriptors();
solar::events::descriptors();
solar::metrics::descriptors();
```

These APIs resolve through the Phase 1 application binding.

### 11.4 Compile-time catalog lookup

```cpp
Parameters::descriptor<NotRegistered>()
```

is a compile-time error. Catalog APIs do not create descriptors or entries on
demand. This compile-time query behavior is independent of relaxed runtime
frontend binding.

### 11.5 No universal runtime descriptor union

Solar does not force all subsystem descriptors into one universal tagged union
or common base class.

Generic tooling may enumerate catalog kinds at compile time. Runtime tooling
uses focused descriptor spans or an explicit inspection adapter.

## 12. Descriptor Views

### 12.1 Immutable bounded views

Runtime-facing descriptor enumeration returns an immutable span:

```cpp
std::span<const solar::parameters::DescriptorView>
```

The backing array has static storage and fixed compile-time size. Enumeration
allocates nothing.

### 12.2 View shape

A subsystem descriptor view normally contains:

- typed or erased local ID appropriate to the API;
- authored descriptor metadata;
- bounded owner view;
- origin category;
- subsystem-specific immutable metadata;
- resolved local references needed by inspection.

Conceptually:

```cpp
struct ParameterDescriptorView
{
    parameters::LocalId local_id;
    parameters::Descriptor descriptor;
    OwnerView owner;
    OriginKind origin;
    ValueKind value_kind;
};
```

The exact field payload belongs to the subsystem specification.

### 12.3 Materialization cost

Compile-time catalog types always exist when the subsystem is present. Runtime
descriptor arrays should be `constexpr` and link-removable when no runtime
consumer references them.

Kconfig may control optional metadata such as long descriptions or rich schema
text. It must not alter C++ type identity or silently change stable IDs.

### 12.4 Descriptor lifetime

Descriptor views and their string views refer only to static-lifetime data.
They never reference temporary formatting buffers or component objects.

## 13. Runtime Records

### 13.1 Records remain subsystem-owned

Catalogs do not own runtime records.

```cpp
solar::parameters::record<DriveKp>();
solar::metrics::record<DeadlineMisses>();
solar::lifecycle::record<Navigation>();
```

resolve through the owning facility or lifecycle storage.

### 13.2 Identity linkage

A runtime record identifies its declaration with the owning catalog's typed
local ID or another explicit typed reference. The ID resolves back to immutable
descriptor metadata through the catalog.

Records must not duplicate mutable descriptor ownership or store unstable raw
type-name hashes.

### 13.3 Bounded queries

Subsystem record enumeration returns one of:

- immutable views with explicitly synchronized lifetime;
- bounded copies;
- caller-buffer output;
- paged iteration over fixed storage.

The subsystem specification chooses based on concurrency and ISR constraints.
Catalog design does not require dynamic allocation.

## 14. Cross-Catalog References

### 14.1 Type references during declaration

Cross-subsystem declarations refer to source declarations by type:

```cpp
solar::remote::ExposeParameter<DriveKp>
solar::metrics::FromEvent<ControlDeadlineMissed, DeadlineMisses>
```

The source type must already be registered through the blueprint or a component
contribution.

### 14.2 Typed reference model

The compile-time reference is conceptually:

```cpp
solar::CatalogReference<
    solar::parameters::Tag,
    DriveKp>
```

After validation, runtime descriptor materialization may replace the type with a
typed local ID.

### 14.3 Catalog dependencies

Each catalog kind declares which other catalog kinds must be complete before
its references can resolve.

Examples:

- Remote parameter exposures depend on the parameter catalog;
- event-to-metric adapters depend on event and metric catalogs;
- Remote topics depend on the Remote schema-type catalog.

### 14.4 Dependency DAG

Solar builds a compile-time dependency DAG of catalog kinds. It validates base
catalogs first, then dependent catalogs.

Catalog dependency cycles are rejected with a focused diagnostic. A catalog
must not embed another complete catalog merely to access its descriptors.

### 14.5 Preserving source provenance

A cross-catalog derived entry retains both:

- its own semantic owner and origin;
- a typed reference to the source entry.

Transformation must not erase where the source declaration came from.

## 15. Filtering, Transformation, And Expansion

### 15.1 Entry-preserving filtering

Filtering operates on complete catalog entries:

```cpp
solar::filter_catalog_t<Catalog, Predicate>
```

The result retains declaration, owner, and origin. A filter must not return a
bare ownerless type list unless explicitly requested for a narrow internal
algorithm.

### 15.2 Provenance-preserving transformation

Transformation either:

- preserves the original `CatalogEntry`; or
- produces a new derived entry with an explicit derivation rule and source.

It must not silently reassign semantic ownership.

### 15.3 Explicit expansion

Catalog kinds may recognize explicit group declarations:

```cpp
solar::metrics::Group<DriveControlMetrics, A, B, C>
```

Expansion occurs through catalog traits, preserves the contributing component,
and records group origin if useful for inspection.

Expansion does not recursively scan arbitrary declaration types for additional
contributions.

### 15.4 Finite normalization

Every expansion and derived-entry rule must terminate in a finite compile-time
catalog. Repeated expansion of the same source or a derivation cycle is an
error.

## 16. Remote And Inspection Boundaries

### 16.1 Consumers, not owners

Remote and Inspection consume the system `CatalogSet`. They do not:

- define application catalog membership;
- own component or subsystem descriptors;
- flatten away semantic owners;
- maintain mutable copies of canonical catalogs;
- assign replacement local IDs.

### 16.2 Remote manifests

Remote may generate wire descriptor tables and manifests from selected catalog
entries. Those are derived artifacts.

A generated manifest may provide checked stable ID constants, but the manifest
must be version controlled or otherwise contractually stable. Regenerating a
build must not silently assign new IDs because source ordering changed.

### 16.3 Inspection formatting

Inspection may combine owner and declaration names for presentation:

```text
navigation / navigation.control.deadline_misses
```

This formatting does not rewrite the declaration's canonical name or stable
identity.

### 16.4 Focused surfaces

Inspection obtains focused descriptor spans and focused runtime records. It
does not request a vague universal `snapshot()`.

## 17. Validation

### 17.1 Validation timing

Catalog validation occurs during Phase 1 effective-blueprint normalization and
before runtime storage or lifecycle coordination is used.

The sequence is:

1. collect direct declarations;
2. collect conventional and generic component contribution groups;
3. wrap declarations with owner and origin;
4. perform explicit finite expansion;
5. validate declaration descriptors;
6. validate type, name, and stable-ID uniqueness;
7. assign deterministic local IDs;
8. resolve cross-catalog references in dependency order;
9. validate derived entries and effective policies;
10. materialize immutable descriptor views where required.

### 17.2 Required compile-time diagnostics

Solar must diagnose:

- missing descriptor metadata;
- descriptor of the wrong catalog kind;
- empty required name;
- duplicate declaration type;
- duplicate name in its uniqueness scope;
- duplicate stable ID in its identity domain;
- malformed or reserved stable ID;
- missing stable ID when policy requires one;
- one declaration claimed by conflicting semantic owners;
- duplicate direct and contributed registration;
- unknown contribution group;
- contribution group under the wrong tag;
- malformed reserved conventional alias;
- contribution to a Kconfig-disabled subsystem;
- contribution under an unregistered extension tag;
- duplicate registration through conventional and generic sources;
- unsupported or recursive expansion;
- cross-catalog reference to an unregistered declaration;
- catalog dependency cycle;
- local-ID capacity overflow;
- invalid schema version or identity-policy combination.

### 17.3 Focused diagnostics

Diagnostics should name relevant types and roles:

```text
DriveKp is registered both directly by the application and contributed by
DriveController
```

```text
ControlDeadlineMissed requires a stable event ID
```

```text
Remote exposure GetDriveKp references unregistered parameter DriveKp
```

Validation should use small concepts and assertion boundaries rather than one
large catalog-valid assertion.

## 18. Capacity And Storage

### 18.1 Compile-time capacity

Catalog size is known at compile time. A subsystem may impose a Kconfig maximum
for memory, protocol, or ID-width reasons.

Exceeding the configured maximum is a compile-time error.

### 18.2 Local ID width

Catalog traits or shared Kconfig select local-ID representation. The default
should remain compact, normally 16 bits.

No catalog silently truncates its size into the configured ID representation.

### 18.3 Runtime descriptor storage

Materialized descriptor arrays have exactly the validated catalog size. They do
not reserve a dynamic maximum or use heap storage.

Optional long metadata may be removed by Kconfig if the subsystem documents the
inspection impact.

### 18.4 No runtime registration overflow

Because membership is static, catalogs have no runtime registration overflow or
backpressure behavior. Runtime record stores define their own capacity and
overflow policies in later specifications.

## 19. Lifecycle And Initialization

Catalog types and bindings perform no runtime initialization.

Catalog validation occurs at compile time. Immutable descriptor data requires
no boot hook.

Facilities derived from non-empty catalogs participate in lifecycle according
to Phase 1 and the later lifecycle specification. The catalog itself is not a
component and receives no lifecycle hooks.

Descriptor views must be safe to inspect before boot when they contain only
compile-time metadata. Runtime record queries retain their subsystem's
pre-initialization behavior.

## 20. Concurrency And ISR Behavior

Compile-time catalogs and immutable descriptor arrays require no locking.

Descriptor spans may be read concurrently from thread or ISR context when they
contain only immutable static data. APIs that format, allocate, page through
mutable records, or resolve runtime state have separate subsystem constraints.

Catalog lookup must not acquire a global mutex. Runtime facilities synchronize
their own mutable state.

## 21. Error And Result Behavior

Architecture and catalog mistakes are compile-time errors.

Compile-time descriptor lookup for a registered type is total. Lookup for an
unregistered type is ill-formed with a focused diagnostic.

Runtime lookup by local or stable numeric ID is fallible:

```cpp
solar::Result<
    std::reference_wrapper<const ParameterDescriptorView>,
    solar::catalog::LookupError>
```

The exact return type may use pointers, references, indices, or `expected`
according to lifetime needs. It must distinguish at least:

- unknown local ID;
- unknown stable ID;
- unavailable catalog;
- malformed external identifier where applicable.

No lookup returns an unbounded owning allocation.

## 22. Kconfig And C++ Policy Boundary

Kconfig may select:

- subsystem capability availability;
- maximum catalog capacities;
- local-ID representation defaults;
- inclusion of descriptions and rich runtime metadata;
- manifest verification support;
- diagnostics retained in production builds.

C++ declarations select:

- catalog membership;
- semantic owner through contribution location;
- names and descriptions;
- explicit stable IDs;
- schema versions;
- cross-catalog references;
- declaration-specific identity policy.

Kconfig must not assign application stable IDs, name application C++ types, or
change semantic ownership.

## 23. Include Direction

Descriptor and contribution declarations obey the Phase 1 include rules.

Component headers include only:

- the Solar descriptor/contribution headers they use;
- headers for direct typed application dependencies.

They do not include the application root.

Subsystem contribution aliases must depend only on generic catalog vocabulary,
not on the completed `System` type. Catalog collection occurs after the root has
assembled all component declarations.

Cross-catalog declarations reference types through normal headers. They do not
include generated catalogs or the application root.

## 24. Migration From Current Solar

### 24.1 Component names

Current:

```cpp
struct Navigation
{
    using Name = solar::Name<"navigation">;
};
```

Target:

```cpp
struct Navigation
{
    static constexpr solar::component::Descriptor descriptor{
        .name = "navigation",
    };
};
```

### 24.2 Name-derived IDs

Current events, metrics, and Remote declarations derive IDs with FNV hashes of
names. Migration must classify each ID:

- internal only: replace with dense typed local ID;
- externally stable: assign an explicit or manifest-controlled stable ID;
- schema version: store separately from identity.

Existing hashed IDs may be preserved temporarily as explicit manifest values
where wire compatibility matters, but they cease to be recomputed identity.

### 24.3 Contribution collectors

Current:

```cpp
using Metrics = solar::metrics::List<A, B>;
using Events = solar::events::List<C>;
using RemoteMethods = solar::remote::Methods<D>;
```

Target:

```cpp
using Metrics = solar::metrics::Contribute<A, B>;
using Events = solar::events::Contribute<C>;
using RemoteActions = solar::remote::ContributeActions<D>;
```

The implementation replaces `MetricsOf`, `EventsOf`, the current
`RemoteMethodsOf`, and their bespoke central collectors with subsystem-owned
`contribution_source` adapters and generic grouping, owner wrapping, and
collection. Phase 10 additionally introduces `RemoteData`, `RemoteTopics`,
`RemoteStreams`, and `RemoteLinks` through the same protocol.

Generic extension code may instead use:

```cpp
using Contributions = solar::Contributions<
    custom::Contribute<X>,
    experimental::Contribute<Y>>;
```

### 24.4 Ownership preservation

Existing collection returns raw lists and loses owner identity. Migration wraps
entries with owner and origin before subsystem validation or descriptor
materialization.

### 24.5 Runtime descriptors

Existing runtime structures such as `ComponentDescriptor`, metric snapshots,
event records, and Remote descriptor arrays must be separated into:

- immutable authored descriptor metadata;
- immutable catalog entry views;
- mutable subsystem records.

Compatibility constructors that accept only a name are transitional and should
be removed after migration.

### 24.6 Catalog surface

Existing `System` aliases such as `MetricsCatalog` may remain temporary
compatibility aliases. New code targets:

```cpp
System::catalog::Of<Tag>
```

and focused global subsystem descriptor APIs.

## 25. Testing Obligations

### 25.1 Compile-pass tests

- minimal one-line component descriptor;
- descriptor with description;
- descriptor supplied through external trait specialization;
- direct blueprint declarations;
- one conventional contribution alias;
- multiple conventional contribution aliases;
- optional generic `Contributions` alias;
- conventional and generic contribution sources on one component;
- subsystem-owned contribution-source extension;
- explicit third-party candidate catalog tag;
- empty conventional aliases;
- contribution to an enabled subsystem;
- owner preservation;
- deterministic local IDs;
- typed local IDs from different catalogs cannot be mixed;
- explicit stable IDs;
- manifest-provided stable IDs;
- separate identity domains;
- deliberately shared identity domain;
- cross-catalog reference resolution;
- entry-preserving filtering;
- derived-origin preservation;
- immutable descriptor-span enumeration;
- descriptor inspection before boot;
- alternate application binding catalogs.

### 25.2 Compile-fail tests

- missing descriptor;
- empty required name;
- descriptor under the wrong tag;
- duplicate type;
- duplicate name;
- duplicate stable ID;
- direct and contributed duplicate;
- conflicting owners;
- malformed contribution group;
- reserved conventional alias with the wrong contribution tag;
- reserved conventional alias with a malformed contribution shape;
- contribution to a Kconfig-disabled subsystem;
- unregistered third-party contribution tag;
- duplicate registration through conventional and generic aliases;
- missing required stable ID;
- use of local ID as an external ID where concepts can reject it;
- unregistered cross-catalog reference;
- catalog dependency cycle;
- recursive expansion;
- local-ID capacity overflow;
- strict lookup of an unregistered declaration.

### 25.3 Runtime tests

- descriptor views contain expected names and metadata;
- owner views resolve to the correct component descriptor;
- direct declarations report application ownership;
- stable-ID lookup succeeds and unknown ID is reported;
- local-ID lookup is bounded;
- descriptor spans have static lifetime;
- global and explicit system catalog surfaces address identical data;
- catalogs require no boot-time initialization;
- concurrent immutable descriptor reads need no lock.

### 25.4 Build and size tests

- unused runtime descriptor arrays are link-removable;
- disabling rich descriptions reduces retained metadata;
- catalogs use no heap allocation;
- local-ID representation matches configuration;
- generated catalogs do not duplicate arrays across translation units;
- representative catalog sizes remain acceptable for Zephyr builds;
- C++23 constexpr validation compiles under supported Zephyr SDK toolchains.

## 26. Deferred Capabilities

The following remain deliberately deferred:

- C++26 reflection-assisted descriptor generation;
- automatic documentation generation format;
- stable-ID manifest tooling and command-line workflow;
- rename aliases and compatibility tombstones;
- dynamic plugin catalogs;
- runtime registration;
- heterogeneous universal descriptor serialization;
- catalog diff tooling between firmware versions;
- host-side schema compatibility checking;
- compile-time source-location metadata;
- automatic source-code discovery of API call sites;
- full unit and dimensional-analysis metadata.

The design leaves room for these without making them requirements of the first
implementation.

## 27. Rejected Alternatives

### 27.1 `DescriptorSpec` as public vocabulary

Rejected as unnecessary ceremony. The authored metadata type is simply
`Descriptor`.

### 27.2 One universal descriptor aggregate

Rejected because component, parameter, metric, event, bus, logging, and Remote
metadata have different contracts. Shared identity vocabulary does not require
one oversized structure.

### 27.3 Names as C++ identity types

Rejected as the universal model. Types already provide compile-time identity;
names are metadata.

### 27.4 Compiler type-name hashes

Rejected because compiler spelling, mangling, build options, and refactors are
not stable protocol contracts.

### 27.5 Implicit name-derived wire IDs

Rejected because renaming silently breaks persistence and protocols, and hash
collisions become external compatibility failures.

### 27.6 Name plus version as identity

Rejected because compatible schema evolution should not automatically create a
different semantic endpoint.

### 27.7 Subsystem-specific central collectors

Rejected because every new subsystem would require another detector and merge
path in the framework core.

### 27.8 Central hard-coded alias probing

Rejected because an ever-growing central list of `MetricsOf`, `EventsOf`, and
similar detectors would couple the framework core to every subsystem.

Conventional aliases are accepted through subsystem-owned
`contribution_source<Tag, Component>` adapters over a finite candidate tag set.
Arbitrary unregistered alias discovery remains impossible without reflection.

### 27.9 Silent contribution deduplication

Rejected because it hides conflicting ownership and accidental repeated
registration.

### 27.10 Recursive contribution discovery

Rejected because catalog membership becomes difficult to predict. Components
are contribution roots; explicit catalog expansion is finite and visible.

### 27.11 Owner-prefixed automatic names

Rejected because owner renaming would rewrite declaration names and possibly
external contracts.

### 27.12 Catalogs owning runtime state

Rejected because immutable architecture and mutable canonical facts have
different ownership, synchronization, and lifecycle requirements.

### 27.13 Remote-owned catalogs

Rejected because Remote is an adapter and protocol boundary, not the owner of
application vocabulary.

### 27.14 Runtime registries for static declarations

Rejected because membership is known at compile time and runtime mutation would
add initialization order, synchronization, capacity, and failure complexity.

### 27.15 One universal runtime snapshot

Rejected because descriptors and mutable records belong to focused subsystem
surfaces with different consistency rules.

## 28. Accepted Decisions

1. Concrete C++ types provide primary compile-time identity.
2. Authored immutable metadata is named `Descriptor`.
3. Descriptor types are subsystem-specific and compact.
4. A one-line named component descriptor is the common form.
5. `descriptor_traits<CatalogTag, T>` is the generic customization point.
6. `T::descriptor` is the normal descriptor source.
7. Trait specialization supports third-party types as an escape hatch.
8. Removed `T::Name` declarations receive no transitional compatibility
   adapter.
9. Names are metadata and lookup keys, not universal type identities.
10. Catalog enrichment lives in `CatalogEntry`, not a completed descriptor.
11. Every catalog entry preserves declaration, semantic owner, and origin.
12. Direct blueprint entries receive application ownership automatically.
13. Conventional subsystem aliases are the normal component contribution API.
14. Each subsystem owns its `contribution_source<Tag, Component>` adapter.
15. The central collector normalizes conventional aliases generically by tag.
16. A finite candidate tag set includes built-ins, blueprint kinds, and explicit
    extension kinds.
17. Disabled built-in tags remain candidates so intentional use is diagnosed.
18. Reserved aliases with an invalid shape are compile-time errors.
19. `Contributions` remains an optional generic extension alias.
20. Conventional and generic contribution sources may coexist.
21. Duplicate registration and conflicting ownership are compile-time errors.
22. Components are normal contribution roots; expansion is explicit and finite.
23. Local IDs are dense, deterministic, build-local, and typed by catalog.
24. Stable IDs are explicit or manifest-controlled and typed by identity domain.
25. Compiler and unmanifested name hashes cannot become external identity.
26. Stable identity and schema version are separate.
27. Catalog tags declare identity domains and may deliberately share one.
28. Names are not automatically prefixed by semantic owner.
29. Catalog extension is trait-based and does not require positional `System`
    growth.
30. `System::catalog::Of<Tag>` is the generic explicit-system query surface.
31. Bound and subsystem namespace APIs expose focused descriptor spans.
32. Descriptor views are immutable, bounded, allocation-free, and static-lived.
33. Mutable records remain owned and queried by their subsystem.
34. Cross-catalog references use declaration types and resolve through a
    compile-time catalog dependency DAG.
35. Filtering and transformation preserve ownership and provenance.
36. Remote and Inspection consume catalogs without owning them.

## 29. Open Questions

There are no blocking open questions for Phase 3.

Later subsystem specifications must decide:

- their exact descriptor fields and compact helper templates;
- their exact conventional component contribution aliases;
- which declarations require stable IDs beyond the shared minimum;
- their exact runtime descriptor-view fields;
- their catalog capacity Kconfig defaults;
- how generated stable-ID manifests are maintained and checked;
- schema compatibility and rename policy details.

These decisions extend the common model without changing its ownership,
identity, contribution, or catalog foundations.
