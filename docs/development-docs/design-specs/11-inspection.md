# Inspection And Unified Query Surfaces

Date: 2026-07-15

Status: accepted design

Owning phase: Phase 11

Depends on:

- `00-design-conventions.md`
- `00a-modern-cpp-result-and-status.md`
- `01-system-blueprint-and-binding.md`
- `02-identity-contributions-and-catalogs.md`
- `03-lifecycle-kernel-and-configuration.md`
- `04-bus.md`
- `05-parameters.md`
- `06-events.md`
- `07-metrics.md`
- `08-logging.md`
- `09-tasks-and-executors.md`
- `10-remote.md`

## 1. Purpose

This specification defines Inspection as Solar's optional generic discovery,
query, paging, and formatting layer over facts already owned by the accepted
subsystems.

Inspection exists for consumers that cannot name every concrete descriptor or
record at compile time, especially:

- Remote runtime introspection;
- generated and interactive host development tools;
- bounded diagnostic capture;
- generic integration and architecture tests;
- firmware manifests and compatibility checks;
- future local diagnostic renderers.

Inspection does not replace the ordinary typed subsystem APIs. Firmware that
knows what it is querying continues to use the owning namespace directly:

```cpp
auto kp = solar::parameters::record<DriveKp>();
auto loop_time = solar::metrics::get<LoopTime>();
auto navigation = solar::lifecycle::record<Navigation>();
auto executors = solar::execution::executors();
```

Generic consumers use the central Inspection API:

```cpp
auto collections = solar::inspection::collections();

using Values = solar::inspection::MetricValues;

std::array<Values::Record, 16> destination{};
auto page = solar::inspection::query<Values>(
    Values::Query{.page = {.limit = destination.size()}},
    destination);
```

The defining ownership rule is:

> Subsystems own facts. Inspection adapts, discovers, filters, pages, and
> formats those facts.

## 2. Non-Goals

Inspection is not:

- a universal all-system runtime snapshot;
- a replacement for `solar::metrics`, `solar::parameters`, or another owning
  subsystem API;
- a second registry of descriptors or records;
- canonical storage for any inspected value;
- a reason to add `inspection` namespaces beneath every subsystem;
- a common base class for all records;
- one universal record variant or tagged union;
- a dynamic object reflection system;
- a universal string query language;
- an SQL-like filter engine;
- a mutation, command, or control API;
- a guarantee of one point-in-time view across multiple subsystems;
- a background sampling service;
- an owner of a thread, work queue, or hidden executor;
- an excuse to hold subsystem locks while formatting or transmitting;
- automatically exposed to untrusted Remote sessions;
- mandatory for applications that use only typed subsystem queries.

Clearing histories, resetting counters, updating parameters, stopping services,
or triggering behavior remains the responsibility of direct subsystem APIs or
explicit Remote Actions.

## 3. Why Inspection Exists

### 3.1 Typed APIs are insufficient for generic consumers

Solar's typed APIs are intentionally strict and ergonomic:

```cpp
solar::parameters::get<DriveKp>();
solar::events::records::event<FrameDropped>();
solar::metrics::get<ControlLoopTime>();
```

A host tool connected to an unfamiliar firmware image cannot compile those
types into itself. It needs to discover which query surfaces exist, obtain
their schemas, and request bounded pages through stable identifiers.

Without Inspection, Remote, diagnostic exporters, tests, and host tooling would
each create a separate generic adaptation layer. Those layers would eventually
disagree about identity, paging, freshness, and source errors.

### 3.2 Concrete uses

Inspection supports the following genuine workflows.

#### Remote development tooling

A host connects, discovers available collections, and constructs parameter
editors, metric tables, lifecycle views, execution views, and diagnostic pages
from the firmware's effective descriptors.

#### Bounded diagnostic capture

A tool reads selected lifecycle failures, executor records, event-loss counts,
logging records, parameter values, and Remote link records as separate focused
pages. The host combines those pages into a report. Firmware never constructs
one giant snapshot.

#### Generic protocol requests

Remote can support `list`, `describe`, and `query` operations over explicitly
exposed Inspection collections instead of adding a bespoke protocol endpoint
for every diagnostic table.

#### Architecture tests

Tests can enumerate effective components, ownership, contributions,
subscriptions, executor registrations, and Remote exposure declarations without
hard-coding every application type.

#### Firmware manifests

The immutable Inspection collection catalog can contribute to a generated
manifest describing which generic runtime queries a build supports.

### 3.3 Inclusion test

An application that does not need generic runtime discovery may disable
Inspection. Every direct subsystem query remains available. This keeps the
feature's cost and purpose honest.

## 4. Architectural Position

Inspection is a central optional adapter layer:

```text
          canonical subsystem APIs and storage
     graph lifecycle bus parameters events metrics
       logging execution kernel Remote records
                         |
                         v
              +---------------------+
              |     Inspection      |
              | collection catalog  |
              | provider adapters   |
              | paging / formatting |
              +----------+----------+
                         |
               +---------+---------+
               |                   |
          local tooling       Remote adapter
```

There is no reverse ownership arrow. Inspection cannot update a canonical
record, replace a subsystem query, or require an owning subsystem to store an
Inspection-specific copy.

### 4.1 Passive facility

Inspection is a Kconfig-selected built-in facility in Solar's architectural
catalog, but it is passive and stateless:

- it owns no thread;
- it owns no work queue;
- it performs no periodic sampling;
- it owns no duplicate runtime records;
- it requires no runtime object;
- it has no mandatory lifecycle work;
- it performs work only when a caller invokes a query or formatter.

If represented in the effective component graph, it appears as an automatically
included built-in facility. Its absence or presence remains visible through the
graph and build manifest.

### 4.2 No subsystem namespace expansion

This design deliberately rejects public surfaces such as:

```cpp
solar::metrics::inspection::Values
solar::events::inspection::History
solar::execution::inspection::Executors
```

Subsystem namespaces expose their canonical APIs only. Generic collection tags
live centrally under `solar::inspection`:

```cpp
solar::inspection::MetricValues
solar::inspection::EventHistory
solar::inspection::Executors
```

The provider adapter connecting a collection to its owning subsystem is an
implementation detail.

## 5. Canonical Fact Inventory

Inspection may adapt the following accepted canonical surfaces. The exact set
present in a build follows Kconfig, the effective blueprint, platform support,
and collection policy.

### 5.1 Graph and catalogs

Canonical owner: the bound system's immutable graph and `CatalogSet`.

Potential collections:

- `Components`;
- `Dependencies`;
- `Ownership`;
- `Contributions`;
- focused immutable subsystem descriptors.

These are immutable compile-time-derived views with static backing storage.

### 5.2 Lifecycle

Canonical owner: Lifecycle storage and orchestration reports.

Potential collections:

- `LifecycleComponents`;
- `BootReport`;
- `StopReport`;
- lifecycle failure records.

Lifecycle records are coherent bounded copies obtained through the accepted
Lifecycle API.

### 5.3 Kernel

Canonical owner: Kernel wrappers and Zephyr-derived diagnostics.

Potential collections:

- Solar-owned thread diagnostics;
- focused synchronization or workqueue diagnostics where the platform supports
  them.

Inspection does not claim that Solar can enumerate every Zephyr kernel object.
Platform-dependent facts remain explicitly unsupported where necessary.

### 5.4 Bus

Canonical owner: Bus catalogs and route records.

Potential collections:

- `BusMessages`;
- `BusSubscriptions`;
- `BusRoutes`.

Inspection does not create a general payload history. Payload history exists
only where an owning subsystem explicitly provides it.

### 5.5 Parameters

Canonical owner: Parameters storage, transactions, change records, and
persistence records.

Potential collections:

- `ParameterDescriptors`;
- `ParameterValues`;
- parameter change and persistence records.

Inspection reads parameters through canonical coherent access. It never
serializes raw references to live parameter storage.

### 5.6 Events

Canonical owner: Events catalogs, sink records, and configured bounded history.

Potential collections:

- `EventDescriptors`;
- `EventRecords`;
- `EventSinks`;
- `EventHistory`.

### 5.7 Metrics

Canonical owner: Metrics values, record adapters, policy state, and operational
records.

Potential collections:

- `MetricDescriptors`;
- `MetricValues`;
- `MetricRecords`.

Typed metric reads remain available directly through `solar::metrics`.

### 5.8 Logging

Canonical owner: Logging source, domain, sink, facility, and bounded history
records.

Potential collections:

- `LogSources`;
- `LogDomains`;
- `LogSinks`;
- `LoggingState`;
- `LogHistory`.

### 5.9 Execution

Canonical owner: Execution registration and runtime storage.

Potential collections:

- `Tasks`;
- `Registrations`;
- `Executors`;
- `ExecutionTargets`;
- `Services`.

Solar execution facts belong to `solar::execution`, not `solar::kernel`.

### 5.10 Remote

Canonical owner: Remote catalogs and runtime service records.

Potential collections:

- `RemoteSchemas`;
- `RemoteData`;
- `RemoteActions`;
- `RemoteTopics`;
- `RemoteStreams`;
- `RemoteLinks`;
- bounded session and acquisition records.

Remote sessions remain subject to authorization and privacy policy even when
the local Inspection collection exists.

## 6. Collection Model

### 6.1 Definition

A **collection** is one typed, source-owned query surface adapted for generic
discovery and bounded access.

A collection declares:

- its stable identity and human-readable name;
- its owning subsystem;
- its record type and schema;
- its typed query and filter structure;
- its supported consistency modes;
- its synchronization and execution-context requirements;
- its query cost class;
- its page and record limits;
- its freshness and cursor behavior;
- whether formatting and Remote export are supported.

Illustrative public shape:

```cpp
namespace solar::inspection {

struct MetricValues {
    using Record = metrics::MetricViewRecord;

    struct Query {
        PageRequest page{};
        std::optional<OwnerView> owner{};
        std::optional<metrics::Kind> kind{};
    };

    static constexpr CollectionDescriptor descriptor();
};

} // namespace solar::inspection
```

The public collection tag contains no storage and need not expose its provider
implementation.

### 6.2 Provider adapter

Solar specializes an internal provider contract for each available collection:

```cpp
template <>
struct detail::InspectionProvider<inspection::MetricValues> {
    static auto query(
        inspection::MetricValues::Query const& request,
        std::span<inspection::MetricValues::Record> destination)
        -> Result<PageResult>;
};
```

That implementation delegates to canonical Metrics APIs. It does not read
Metrics private storage directly unless Metrics intentionally exposes a focused
internal adapter boundary.

### 6.3 Collection concept

The implementation should define a focused concept resembling:

```cpp
template <typename T>
concept InspectionCollection = requires {
    typename T::Record;
    typename T::Query;
    { T::descriptor() } -> std::same_as<CollectionDescriptor>;
};
```

Further constraints ensure bounded records, valid schemas, a registered
provider, stable identity, and valid result semantics.

### 6.4 User-defined collections

Applications may define focused custom collections when their data is not
already represented by a Solar subsystem. A custom collection must:

- use an explicit stable identity;
- identify its owner and origin;
- define bounded record and query types;
- provide an observational, side-effect-free provider;
- declare synchronization and cost metadata;
- contribute through the accepted catalog mechanism;
- opt into Remote exposure separately.

Custom collections must not be used to bypass a canonical Solar subsystem. A
parameter remains a parameter; it should not be mirrored into a custom
Inspection store.

The exact contribution alias may be finalized during implementation alongside
the Phase 2 catalog machinery. It must not require editing Solar's central
collection list.

## 7. Collection Catalog And Discovery

### 7.1 Immutable catalog

```cpp
std::span<CollectionDescriptorView const>
solar::inspection::collections();
```

The returned view has static backing storage derived from the effective bound
system. It allocates nothing and requires no runtime lock.

Each descriptor view includes at least:

- collection local ID;
- optional externally stable ID;
- name;
- owning subsystem tag;
- owner and origin metadata;
- record schema reference;
- query schema reference;
- supported operations;
- supported consistency levels;
- synchronization class;
- execution-context requirement;
- cost class;
- configured maximum page size;
- availability policy;
- formatting capabilities;
- Remote-export capability, not authorization state.

### 7.2 Typed description

```cpp
constexpr auto descriptor =
    solar::inspection::describe<solar::inspection::MetricValues>();
```

For a known bound system, requesting an unregistered collection is a focused
compile-time error where the call is necessarily compile-time bound.

### 7.3 Dynamic lookup

```cpp
auto descriptor = solar::inspection::find(collection_id);
```

Dynamic lookup returns `NotFound` for an unknown ID. It does not manufacture a
placeholder collection.

### 7.4 Generated visitation

Generic in-firmware tooling may dispatch a runtime collection ID through
generated typed visitation:

```cpp
auto result = solar::inspection::visit(
    collection_id,
    [&]<solar::inspection::InspectionCollection Collection>() {
        return inspect_collection<Collection>();
    });
```

Visitation is generated from the effective collection catalog. It does not use
RTTI, dynamic allocation, a virtual base class, or a universal record variant.

### 7.5 Explicit-system access

Tests and libraries that require an unbound explicit system use:

```cpp
using Inspect = solar::inspection::Of<TestApplication>;

auto collections = Inspect::collections();
auto page = Inspect::query<solar::inspection::MetricValues>(query, buffer);
```

The global API delegates to `Of<DefaultApplication>` through the Phase 1
binding, which resolves the selected System type.

## 8. Query API

### 8.1 Typed query

The primary generic query is:

```cpp
template <InspectionCollection Collection>
Result<PageResult> query(
    Collection::Query const& request,
    std::span<Collection::Record> destination);
```

The destination is caller owned. Inspection neither allocates a result vector
nor returns references into mutable subsystem storage.

### 8.2 Single-record access

Collections with stable subject identities may offer a focused single-record
query:

```cpp
auto value = solar::inspection::get<MetricValues>(metric_id);
```

This is generic tooling convenience, not a replacement for:

```cpp
solar::metrics::get<LoopTime>();
```

Single-record support is a declared collection capability, not a universal
assumption.

### 8.3 No universal filter

Every collection defines a compact typed query structure. Common reusable
building blocks may include:

- `PageRequest`;
- owner filter;
- subject filter;
- severity or category filter;
- time or sequence bounds;
- enabled/disabled state filter.

Solar does not define a runtime string expression language. Unsupported filters
are rejected by the type system for typed callers and by schema validation for
dynamic Remote callers.

### 8.4 Read-only guarantee

Inspection providers must be observational. A query must not:

- clear or acknowledge records;
- reset counters;
- update parameters;
- start an acquisition with application-visible side effects;
- invoke a user Action;
- change subscriptions;
- force a lifecycle transition;
- alter source policy.

A diagnostic read may perform the minimum platform operation required to
obtain a documented diagnostic value. Such cost must be declared.

## 9. Paging And Cursors

### 9.1 Caller-owned pages

```cpp
struct PageRequest {
    Cursor cursor{};
    std::size_t limit{};
};

struct PageResult {
    std::size_t written{};
    Cursor next{};
    bool has_more{};
    SourceRevision revision{};
    PageConsistency consistency{};
    Freshness freshness{};
    LossInfo loss{};
};
```

The exact integer widths are implementation decisions constrained by Kconfig
capacity limits.

### 9.2 Cursor properties

A cursor is:

- opaque to callers;
- fixed size;
- allocation free;
- associated with one collection;
- generation or revision aware where the source can change;
- invalidated explicitly when its source history is overwritten or its
  generation changes.

Applications must not persist cursors across reboot unless a collection
explicitly defines stable persistence semantics.

### 9.3 Descriptor views

Immutable descriptor collections may return a static span directly where no
paging is needed:

```cpp
auto descriptors = solar::parameters::descriptors();
```

Inspection may still page the same descriptor set for a dynamic consumer with a
bounded transport frame. It references the canonical static descriptors rather
than copying them into a second registry.

### 9.4 History sources

History collections preserve their owning subsystem's sequence, loss, and
stale-cursor semantics. Inspection does not translate overwritten history into
an empty successful page.

## 10. Consistency And Synchronization

### 10.1 No global consistency claim

Inspection never promises one coherent instant across collections or
subsystems. A diagnostic bundle is a series of independently timestamped and
revisioned queries.

### 10.2 Consistency levels

Collections declare one or more of:

- `PerRecord`: every returned record is internally coherent, but the source may
  change between records;
- `StablePage`: records in one page share a validated source revision or were
  copied under a bounded source lock;
- `PointInTime`: the owning subsystem provides a genuine transaction or
  snapshot facility for the requested set.

`PerRecord` is the normal default. `PointInTime` is never inferred merely
because a provider can lock a mutex.

### 10.3 Synchronization classes

Collection descriptors expose a synchronization class such as:

- `None` for immutable static views;
- `Atomic` for one atomic read or coherent atomic aggregate;
- `MutexCopy` for a bounded copy under the source mutex;
- `SpinCopy` for a strictly bounded platform-safe copy;
- `SourceDefined` for native diagnostics with documented semantics.

These values describe the provider's observation boundary. Callers do not
acquire source locks themselves.

### 10.4 Lock discipline

Providers follow this sequence:

1. validate the request and destination capacity;
2. acquire the canonical source synchronization primitive if required;
3. copy a bounded number of coherent records into caller-owned storage;
4. release the source synchronization primitive;
5. return paging and freshness metadata;
6. format, encode, or transmit only after the lock is released.

No formatter or Remote link may run while a canonical subsystem mutex or
spinlock is held.

### 10.5 Execution context

Every collection declares whether it is callable from:

- ordinary thread context;
- cooperative thread context;
- ISR context;
- a restricted no-block context.

Most runtime collection queries are thread-only. Immutable descriptor lookup
may be available in any context. A thread-only query attempted from an invalid
context returns a focused context error or is rejected by a narrower API.

## 11. Availability, Errors, And Freshness

### 11.1 Distinct states

Inspection preserves these distinctions:

- `Disabled`: the feature or collection was intentionally excluded by Kconfig
  or effective policy;
- `Unsupported`: the selected platform or provider cannot supply the fact;
- `Unavailable`: the capability exists but is not currently ready;
- `Busy`: a coherent observation could not presently be obtained;
- `NotFound`: the collection or subject identity is unknown;
- `StaleCursor`: the source changed or bounded history advanced beyond the
  cursor;
- `SourceFailed`: the canonical provider reported an operational failure.

These reasons use Solar's Phase 0 `Result<T>` and structured error model.

### 11.2 Catalog absence and direct queries

A Kconfig-disabled built-in collection is omitted from the effective runtime
collection catalog. Dynamic lookup therefore cannot discover or query it.

Code that explicitly names a known but disabled collection receives a focused
compile-time availability diagnostic where possible. A runtime-only explicit
path may return `Disabled`.

An arbitrary unknown dynamic ID returns `NotFound`, not `Disabled`.

### 11.3 Stale values

Staleness is normally metadata on a successfully observed value:

```cpp
enum class Freshness {
    Current,
    Stale,
    Mixed,
    Unknown,
};
```

A stale metric or cached diagnostic can still be useful. `StaleCursor` is
reserved for a cursor that can no longer continue correctly.

### 11.4 Partial pages

A provider may return a successful partial page when the request reached its
destination capacity or configured time budget. It must report `has_more` and
the continuation cursor.

A page containing heterogeneous freshness reports `Freshness::Mixed` or
per-record freshness where its schema supports that detail.

## 12. Query Cost Metadata

Generic tooling must be able to distinguish a static descriptor read from an
expensive native diagnostic operation.

Each collection descriptor declares at least:

- complexity class, such as `Constant`, `LinearPage`, `HistoryRead`, or
  `NativeDiagnostic`;
- maximum records per page;
- maximum encoded record size where known;
- whether the query may block;
- synchronization class;
- permitted execution context;
- supported consistency modes;
- whether reading can be comparatively expensive;
- whether values may be stale or cached.

These declarations are descriptive contracts, not scheduler policy. Remote and
local tools may use them for admission, warnings, rate limits, and default page
sizes.

Inspection must not hide an expensive provider behind an API that appears to
be a trivial immutable span.

## 13. Identity, Ownership, And Correlation

### 13.1 Reuse canonical identity

Inspection reuses Phase 2 catalog identity and ownership metadata. It does not
assign competing IDs to parameters, metrics, events, components, or services.

Generic records may carry compact references resembling:

```cpp
struct SubjectRef {
    CatalogKind catalog{};
    LocalId id{};
};

struct RecordOrigin {
    SubjectRef subject{};
    OwnerView owner{};
    OriginKind origin{};
};
```

The exact shape follows the accepted Phase 2 identity types.

### 13.2 Do not duplicate descriptors

A runtime record references its immutable descriptor by local identity. It does
not repeat the descriptor name, unit, description, owner name, and schema in
every record unless a transport formatter intentionally expands them.

### 13.3 Contribution correlation

Inspection may expose an immutable ownership index mapping a component to the
descriptor and collection references it contributed. This supports questions
such as:

```text
Which metrics, events, parameters, tasks, and Remote endpoints belong to
Navigation?
```

The result is a bounded list of references. Inspection does not return one
mixed `records_for<Navigation>()` union. The caller follows each reference to
the owning collection.

### 13.4 Runtime correlation

Event IDs, log correlation IDs, Remote request IDs, lifecycle operation IDs,
and source sequence numbers retain their owning subsystem semantics. Inspection
passes them through where present and does not invent a universal correlation
identifier.

## 14. Formatting And Encoding

### 14.1 Formatting is an adapter

Inspection may format a coherent copied record through bounded writers:

```cpp
auto result = solar::inspection::format_text(record, writer);
auto result = solar::inspection::encode_cbor(record, writer);
```

Formatting is separate from querying. Providers return typed records; writers
consume those records after source synchronization is released.

### 14.2 Bounded writers

Writers are caller owned and report:

- bytes or characters written;
- required or attempted size where practical;
- explicit truncation;
- schema or encoding failure;
- unsupported formatting.

Inspection does not allocate an unbounded string.

### 14.3 Text and structured forms

The baseline useful forms are:

- concise local text formatting for diagnostics;
- canonical structured encoding shared with Remote schemas;
- deterministic CBOR where runtime structured export is enabled.

JSON and rich presentation normally belong to generated host tooling. Firmware
may add them later as optional bounded formatters, but they are not core
requirements.

### 14.4 No formatting under locks

Record names, units, descriptions, and owner names may be joined from immutable
catalog views during formatting. No mutable source lock remains held during
that operation.

## 15. Remote Integration

### 15.1 Shared canonical adapters

Remote runtime introspection and local Inspection consume the same collection
descriptors, provider adapters, record schemas, and source results.

Remote must not maintain a second inspection registry or copy canonical records
into Remote-owned permanent storage.

### 15.2 Separate responsibilities

Inspection owns:

- collection discovery;
- typed provider dispatch;
- bounded query behavior;
- local paging semantics;
- source freshness and error preservation;
- optional record formatting and structured encoding adapters.

Remote owns:

- external stable wire identity;
- authorization and exposure policy;
- session state;
- protocol request validation;
- CBOR envelope and framing;
- transport backpressure;
- per-session cursors or cursor tokens;
- external error mapping;
- rate and admission policy.

### 15.3 Exposure remains explicit

An Inspection collection being locally available does not automatically expose
it through Remote. The effective Remote catalog or a deliberate development
convenience pack selects allowed collections and grants.

Sensitive records may be locally inspectable while absent from Remote, or
restricted to an administrative grant.

### 15.4 Manifest relationship

The generated Remote manifest may reference Inspection collection schemas and
identities. Both derive from the same effective catalogs. Their IDs need not be
numerically identical when the wire protocol requires a separate stable ID,
but the mapping is generated, explicit, and unambiguous.

### 15.5 Cursor mapping

Remote may wrap a local opaque cursor with session generation and authorization
state. It must preserve stale-cursor and source-loss semantics. A Remote cursor
must not outlive its session unless an endpoint explicitly defines stable
resume behavior.

## 16. Configuration And Blueprint Integration

### 16.1 Kconfig boundary

Kconfig controls build-wide defaults and inclusion, including:

- whether generic Inspection is compiled;
- maximum runtime page size;
- dynamic collection discovery;
- generated visitation support;
- local text formatting;
- structured CBOR encoding;
- optional ownership indexes;
- optional expensive platform diagnostics;
- default Remote introspection availability.

Exact symbols are defined during implementation and should follow the existing
Solar Kconfig hierarchy.

### 16.2 Type-policy boundary

Blueprint and declaration policy may:

- include user-defined collections;
- narrow page limits;
- select consistency policy where a provider supports alternatives;
- permit or prohibit expensive local diagnostics;
- select Remote exposure separately;
- customize externally stable identity.

Policy precedence follows Phase 0:

```text
declaration > blueprint subsystem policy > Kconfig default
```

### 16.3 Automatic built-in collection inclusion

When Inspection and an owning built-in subsystem are enabled, that subsystem's
standard collection adapters are included automatically. Users do not repeat
them in the system blueprint.

Automatic inclusion remains visible in the effective collection catalog and
manifest.

### 16.4 Disabled Inspection

Disabling Inspection removes:

- generic collection discovery;
- dynamic visitation;
- Inspection paging adapters;
- Inspection formatting and encoding support;
- Remote's generic introspection endpoint.

It does not remove:

- subsystem canonical descriptors;
- typed subsystem queries;
- subsystem runtime records;
- explicit Remote Data, Actions, Topics, or Streams that use direct adapters.

## 17. Capacity And Allocation

Core Inspection performs no dynamic allocation after boot.

Capacity is bounded by:

- compile-time collection count;
- static immutable descriptor arrays;
- caller-owned record spans;
- fixed-size cursors;
- configured maximum page size;
- caller-owned formatting buffers;
- owning subsystem history and record capacities.

Inspection adds no hidden record cache. A provider that requires a temporary
copy uses caller-owned destination storage or a small explicitly bounded local
value justified by the record contract.

## 18. Validation And Diagnostics

The implementation must diagnose at least:

- duplicate collection type;
- duplicate local or stable collection identity;
- collection without a registered provider;
- provider record type mismatch;
- invalid or unbounded record schema;
- invalid query schema;
- collection contributed while Inspection is disabled, according to selected
  policy;
- collection referencing an absent canonical subsystem;
- unsupported consistency declaration;
- invalid synchronization or execution-context declaration;
- page limit larger than representable cursor or result capacity;
- Remote exposure without an encodable schema;
- user collection without owner or origin metadata;
- provider attempting to expose mutable borrowed storage.

Diagnostics should name the collection, provider, owning subsystem, and failed
constraint.

## 19. Migration From Current Solar

The current `solar::facilities::Inspection` class is a prototype and should be
removed rather than expanded.

Its current responsibilities migrate as follows:

- component counts come from canonical graph and catalog APIs;
- lifecycle state comes from `solar::lifecycle`;
- logging statistics come from `solar::log`;
- service and executor facts come from `solar::execution`;
- low-level thread facts come from `solar::kernel`;
- generic enumeration and formatting move to `solar::inspection` collection
  adapters.

Current broad `Snapshot` names in prototype Metrics or Kernel code must be
replaced by the focused records accepted in their owning specifications.

Migration must not preserve a context object, runtime Inspection singleton, or
duplicate storage merely for source compatibility.

## 20. Testing Obligations

Implementation requires:

### Compile-time tests

- effective collection inclusion follows Kconfig and blueprint policy;
- duplicate identities fail clearly;
- provider and record mismatches fail clearly;
- unregistered typed collections fail clearly;
- user-defined collections contribute without editing Solar internals;
- disabled Inspection leaves direct subsystem APIs intact.

### Runtime unit tests

- immutable collection discovery is deterministic;
- typed and explicit-system queries reach the same canonical source;
- destination capacity is respected;
- continuation cursors enumerate without duplication in a stable source;
- stale cursors are detected;
- partial pages report continuation correctly;
- disabled, unsupported, unavailable, busy, stale, and failed remain distinct;
- formatting reports truncation;
- no formatter runs under a mocked source lock;
- ownership references resolve to canonical descriptors;
- no query mutates source state.

### Concurrency tests

- mutex-protected sources yield coherent records;
- per-record pages tolerate concurrent source updates without data races;
- stable-page providers detect revision changes;
- history overwrite produces explicit cursor loss;
- slow formatting does not retain canonical locks;
- concurrent local and Remote queries obey source synchronization.

### Remote integration tests

- Remote and local Inspection enumerate the same selected collections;
- Remote exposure policy can hide a locally available collection;
- schema and identity mappings are deterministic;
- session cursor wrapping preserves stale-source behavior;
- malformed dynamic filters fail before provider invocation;
- transport backpressure cannot block the source while its lock is held.

### Resource tests

- disabled Inspection has no generic runtime storage cost;
- page buffers remain caller owned;
- generated visitation contains only effective collections;
- configured maximum stack and encoded record sizes are enforced.

## 21. Deferred Capabilities

The following are deliberately deferred while preserving extension points:

- optional JSON formatting in firmware;
- persisted cursors across reboot;
- host-requested joins across collections;
- richer query planning or compound predicates;
- coroutine-based asynchronous local inspection;
- schema reflection generated from future standard C++ reflection;
- capture orchestration that assembles several collection pages into a named
  diagnostic package;
- compressed diagnostic export;
- platform-specific inspection plugins loaded outside the static system graph.

Any future compound capture remains a coordinator over focused queries. It must
not become canonical all-system snapshot storage.

## 22. Rejected Alternatives

### 22.1 Subsystem `inspection` namespaces

Rejected because they duplicate already accepted canonical query surfaces and
spread generic-tooling ceremony throughout every subsystem.

### 22.2 Universal runtime snapshot

Rejected because it conflates ownership, consistency, availability, cost, and
freshness across unrelated sources.

### 22.3 Universal record variant

Rejected because every new subsystem would enlarge a central type and force
unrelated code to understand all record categories.

### 22.4 Dynamic polymorphic providers

Rejected as the baseline because Solar has a compile-time bound system graph.
Generated static dispatch is deterministic, allocation free, and easier to
validate.

### 22.5 Independent Inspection storage

Rejected because it creates duplicate truth, synchronization ambiguity, and
hidden memory cost.

### 22.6 Universal text filters

Rejected because they add parser cost, defer errors to runtime, and poorly
represent subsystem-specific query semantics.

### 22.7 Automatic Remote exposure

Rejected because local observability does not imply safe external disclosure.

### 22.8 Mandatory Inspection

Rejected because applications using only typed queries should not pay for
generic discovery and dispatch.

## 23. Final Decisions

1. Inspection is an optional generic discovery, query, paging, and formatting
   layer.
2. Existing subsystem APIs remain canonical and are the normal firmware path.
3. Public subsystem namespaces do not gain nested `inspection` namespaces.
4. Generic collection tags live centrally under `solar::inspection`.
5. Collection provider adapters delegate to canonical subsystem APIs.
6. Inspection owns no canonical facts and no duplicate runtime storage.
7. Inspection is passive, stateless, and owns no thread or hidden executor.
8. Built-in collection adapters follow Kconfig and effective subsystem
   inclusion automatically.
9. User-defined collections contribute through the catalog architecture.
10. Collection discovery uses immutable static descriptor views.
11. Known callers retain typed records and typed query structures.
12. Dynamic dispatch uses generated visitation or bounded encoding adapters.
13. There is no universal runtime record union or common object hierarchy.
14. Runtime pages use caller-owned bounded buffers.
15. Filters are typed and collection specific.
16. Cursors are fixed-size, opaque, collection-bound, and revision aware.
17. `PerRecord`, `StablePage`, and genuine `PointInTime` consistency remain
    distinct.
18. Inspection never claims cross-subsystem point-in-time consistency.
19. Query synchronization and execution-context requirements are explicit.
20. Query cost and page limits are descriptor metadata.
21. Formatting and encoding happen after source locks are released.
22. Disabled, unsupported, unavailable, busy, stale, and failed are distinct.
23. Stale data may be a successful value with explicit freshness metadata.
24. Inspection reuses canonical catalog identity, ownership, and provenance.
25. Component contribution correlation returns references, not mixed records.
26. Existing subsystem correlation and sequence identities pass through.
27. Inspection is observational and exposes no mutation commands.
28. Remote and local Inspection share collection descriptors and providers.
29. Remote retains authorization, wire identity, sessions, framing, and
    transport policy.
30. Local collection availability never implies automatic Remote exposure.
31. Disabling Inspection leaves all direct subsystem APIs intact.
32. The current object-shaped `facilities::Inspection` prototype is replaced.
33. A universal all-system runtime snapshot remains prohibited.

## 24. Open Questions

There are no blocking architectural questions for Phase 12.

Implementation may refine without changing this contract:

- exact collection identity widths;
- exact names of common page and cursor helper types;
- exact contribution alias for user-defined collections;
- exact generated visitation implementation;
- exact Kconfig symbol names and default capacities;
- whether collection tags are structs or compact inline tokens backed by types;
- exact text writer concept;
- exact structured schema reuse boundary with Remote;
- exact optional ownership-index representation;
- exact compile-time behavior for explicitly naming a disabled collection;
- additional focused platform diagnostics supported by individual boards.
