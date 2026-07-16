# Runtime Parameters

Date: 2026-07-15

Status: accepted design

Owning phase: Phase 5

Depends on:

- `00-design-conventions.md`
- `00a-modern-cpp-result-and-status.md`
- `01-system-blueprint-and-binding.md`
- `02-identity-contributions-and-catalogs.md`
- `03-lifecycle-kernel-and-configuration.md`
- `04-bus.md`

## 1. Purpose

This specification defines Solar's runtime parameter subsystem.

Parameters are globally identifiable, validated, optionally persistent runtime
configuration values. They allow firmware behavior to be tuned while retaining
static identity, bounded storage, deterministic lifecycle, and explicit access
policy.

It establishes:

- canonical parameter vocabulary and namespace;
- compact typed declarations and contributions;
- value, descriptor, default, validation, and identity contracts;
- typed get, set, try-set, reset, save, and inspection APIs;
- local access and external-exposure boundaries;
- static storage, synchronization, revisions, and records;
- coherent snapshots and atomic RAM transactions;
- volatile, immediate, deferred, manual, and transactional persistence;
- Zephyr settings integration and stable storage records;
- load fallback, migration, dirty-state, and failure behavior;
- synchronous post-commit change hooks;
- lifecycle and dependency rules;
- explicit Remote and observability boundaries.

The normal path remains concise:

```cpp
auto kp = solar::parameters::get<DriveKp>();
auto update = solar::parameters::set<DriveKp>(1.4f);
```

No parameter facility object or system reference appears in application code.

## 2. Non-Goals

Parameters are not:

- arbitrary private component state;
- sensor samples or telemetry streams;
- metric counters;
- commands or behavior messages;
- logs or observability event records;
- large mutable buffers;
- general-purpose shared memory;
- a compile-time configuration replacement;
- a Kconfig replacement;
- automatically Remote-exposed values;
- a runtime registry;
- a dynamic property bag;
- a persistence database for unrelated application objects.

Frequently changing operational state belongs to its owning component, a typed
queue, the bus, or metrics according to its semantics.

## 3. Canonical Vocabulary

### 3.1 Parameter

A **parameter** is a registered runtime configuration value with:

- one C++ declaration type;
- one value type;
- one declared default;
- immutable descriptor metadata;
- validation policy;
- access policy;
- synchronization policy;
- persistence policy;
- canonical runtime state owned by the parameter facility.

### 3.2 Naming decision

The architecture and C++ API use **parameter** and `solar::parameters`.

“System Variables” may be used as a product, shell, or Remote UI label. It does
not replace the canonical API vocabulary or namespace.

### 3.3 Runtime configuration boundary

Runtime parameters are distinct from:

- Kconfig build capability and hard ceilings;
- typed blueprint architecture and policy;
- ordinary component-owned runtime state.

Changing a parameter does not rebuild the system graph, create a component,
change static storage size, or enable code excluded by Kconfig.

## 4. Parameter Declaration

### 4.1 Common declaration

```cpp
struct DriveKp
{
    using Value = float;

    static constexpr solar::parameters::Descriptor descriptor{
        .name = "drive.pid.kp",
        .description = "Drive velocity proportional gain",
        .units = "",
    };

    static constexpr Value default_value = 1.2f;

    using Validation = solar::parameters::Range<
        0.0f,
        10.0f,
        solar::parameters::Reject>;

    using Persistence = solar::parameters::Deferred<
        SettingsStore,
        solar::parameters::delay::Seconds<2>>;
};
```

Only `Value`, `descriptor`, and `default_value` are required. Missing optional
policies resolve through parameter configuration and Kconfig defaults.

### 4.2 Descriptor customization

The normal authored source is:

```cpp
static constexpr solar::parameters::Descriptor descriptor{...};
```

The generic Phase 2 customization point is:

```cpp
solar::descriptor_traits<solar::parameters::parameter_tag, Parameter>
```

Trait specialization supports third-party declaration types.

### 4.3 Descriptor fields

The authored descriptor initially supports:

- stable human-readable name;
- optional description;
- optional units;
- optional display metadata that does not affect storage semantics;
- optional explicit stable ID under Phase 2 rules;
- schema version when persistence or external schema requires it.

Validation, access, persistence, storage, and exposure remain typed policy and
catalog enrichment. They do not become an unstructured descriptor flag bag.

### 4.4 Metadata retention

Description and display metadata may be removable through Kconfig for builds
that do not include inspection metadata. Name and identity information required
by persistence or explicit external protocols cannot be stripped while those
capabilities remain enabled.

## 5. Value Contract

### 5.1 Base requirements

A parameter `Value` must be:

- a complete object type;
- copy constructible;
- copy assignable or replaceable by the selected storage policy;
- destructible;
- equality comparable, or accompanied by an explicit equivalence policy;
- safely returnable by value;
- bounded without hidden dynamic allocation under the normal Solar contract.

References and `void` are invalid parameter value types.

### 5.2 Default value

`default_value` must be a valid constant expression of `Value`.

The effective validator must accept or normalize it to the same value at
compile time. An invalid declared default is an architecture error, not a boot
fallback case.

### 5.3 Borrowed values

Views, spans, string views, raw pointers, and other non-owning values are not
valid canonical parameter storage unless an explicit storage policy proves a
static lifetime contract.

The default design requires owned value semantics.

### 5.4 Persistence and exposure requirements

A volatile local parameter needs only the base value contract.

A persistent or externally exposed parameter additionally requires:

- a deterministic codec;
- a stable schema definition;
- bounded encoded size;
- stable identity;
- explicit versioning when its schema may evolve.

Copyability does not imply persistence or wire encodability.

## 6. Parameter Contributions

### 6.1 Conventional component alias

```cpp
struct DriveController
{
    using Parameters = solar::parameters::Parameters<
        DriveKp,
        DriveKi,
        DriveKd>;
};
```

The parameter subsystem owns
`contribution_source<parameters::parameter_tag, Component>`. The generic Phase
2 collector preserves semantic owner and registration origin.

### 6.2 Root declarations

Application-owned parameters use the accepted root section:

```cpp
solar::Parameters<
    RobotName,
    LoggingVerbosity>
```

### 6.3 Catalog registration

Every typed operation requires effective catalog membership:

```cpp
solar::parameters::get<Unregistered>(); // strict error; relaxed NotRegistered
```

No API creates standalone static storage for an unregistered declaration.

### 6.4 Duplicate behavior

Repeated type registration, duplicate names, conflicting stable IDs, and
conflicting semantic ownership are compile-time errors under Phase 2.

Solar does not merge independently authored parameter declarations because
their defaults or value types happen to match.

## 7. Identity And Versioning

### 7.1 Local identity

The parameter declaration type is the primary local identity. The normalized
catalog assigns a dense typed local ID for records and dispatch.

### 7.2 Stable identity

A persistent parameter requires an explicit or manifest-controlled stable ID
in the parameter identity domain.

External Remote exposure also requires stable identity unless the later Remote
protocol deliberately uses another stable mapping.

Names, compiler type strings, and unmanifested hashes do not silently become
persistence keys.

### 7.3 Schema version

Schema version is separate from stable parameter identity.

Changing the encoded representation increments version while retaining the
same identity when it remains the same conceptual parameter.

Renaming display metadata does not rewrite persistent identity.

## 8. Public Read API

### 8.1 Typed get

```cpp
template<solar::parameters::Parameter P>
solar::Result<typename P::Value, solar::parameters::Error>
get();
```

`get<P>()` returns a value copy. It never returns a mutable or unlocked
reference to canonical storage.

The expected result preserves the Phase 0 contract and represents lifecycle or
synchronization failure honestly.

### 8.2 Non-blocking get

The subsystem may expose:

```cpp
solar::parameters::try_get<P>();
```

It never waits for a mutex. Mutex-backed storage returns `WouldBlock` when it
cannot copy immediately. Atomic-backed storage normally succeeds immediately.

### 8.3 ISR read

```cpp
solar::parameters::get_isr<P>();
```

is available only for an explicitly lock-free atomic parameter with an ISR-safe
facility readiness check. It is a compile-time error for mutex-backed or
non-lock-free values.

The initial design does not permit general parameter mutation from ISR context.

### 8.4 No unchecked global reference

Solar does not provide `ref<P>()`, mutable `operator[]`, or a pointer to the
canonical slot. Such access would bypass synchronization, validation,
revisions, persistence, and change hooks.

## 9. Public Mutation API

### 9.1 Set

```cpp
template<solar::parameters::WritableParameter P>
solar::Result<solar::parameters::Update<P>, solar::parameters::Error>
set(typename P::Value candidate);
```

`set` may wait for parameter synchronization and synchronous persistence
required by the effective policy.

### 9.2 Try set

```cpp
template<solar::parameters::WritableParameter P>
solar::Result<solar::parameters::Update<P>, solar::parameters::Error>
try_set(typename P::Value candidate);
```

`try_set` means strictly non-blocking. It does not mean “set without exception”
or “validate only.”

If slot synchronization, the write gate, immediate storage, or required
executor admission cannot proceed immediately, it returns `WouldBlock` without
changing the runtime value.

### 9.3 Strong failure invariant

An unexpected result from `set`, `try_set`, `reset`, or an atomic multi-value
operation means no runtime parameter value was committed by that operation.

Post-commit observer or deferred-persistence failure is represented in the
successful result and focused records. It does not violate this invariant by
returning an error after the value changed.

### 9.4 Equal assignment

When the normalized candidate equals the current value:

- no revision increments;
- no change hook runs;
- no persistence write is scheduled;
- dirty state is not cleared implicitly;
- the result reports `changed == false`.

An existing dirty value remains dirty until save, flush, reset, or successful
persistence.

## 10. Update Result

The typed successful result has the conceptual shape:

```cpp
template<typename P>
struct Update
{
    using Value = typename P::Value;

    Value effective_value;
    std::uint64_t revision;
    PersistenceState persistence;
    std::size_t change_failures;
    bool changed;
    bool adjusted;
    bool changes_deferred;
};
```

The exact field order is not a wire contract.

`effective_value` makes clamping visible. `adjusted` distinguishes normalization
from direct acceptance. `changes_deferred` identifies a pre-running update whose
change hooks will activate later.

## 11. Parameter Error

`solar::parameters::Error` is a typed structured error containing:

- broad reason;
- parameter local ID where applicable;
- persistence group or store identity where applicable;
- validation detail where representable;
- expected and observed schema versions where applicable;
- native or codec status where applicable.

Initial reasons include:

- not ready;
- not registered at runtime dispatch boundary;
- read only;
- privilege required;
- validation rejected;
- would block;
- persistence unavailable;
- persistence failed;
- codec failed;
- version mismatch;
- corrupt record;
- transaction conflict;
- internal invariant failure.

Typed direct calls reject statically invalid registration and mutability at
compile time. Runtime-dispatched Inspection or Remote operations may return the
corresponding typed reason.

`status_of(parameters::Error)` provides the explicit lossy mapping required by
Phase 0 and lifecycle boundaries.

## 12. Validation And Normalization

### 12.1 Purpose

Validation transforms or rejects a candidate before any canonical state or
persistence changes.

### 12.2 Normalization contract

The conceptual validator operation is:

```cpp
static constexpr solar::Result<Value, ValidationError>
normalize(Value candidate);
```

Returning a value accepts the effective candidate. Returning an error rejects
the operation without mutation.

### 12.3 Built-in validators

Initial forms include:

```cpp
solar::parameters::Range<Minimum, Maximum, Reject>
solar::parameters::Range<Minimum, Maximum, Clamp>
solar::parameters::OneOf<Values...>
solar::parameters::Custom<Validator>
```

Aliases may be shortened during API implementation while preserving these
semantics.

### 12.4 Rejection

Reject policy returns `ValidationRejected`. Current RAM state, revision,
persistence, and hooks remain unchanged.

### 12.5 Clamping

Clamp policy commits the nearest accepted bound and reports
`adjusted == true`. If the effective clamped value equals current state, the
operation is unchanged but remains visibly adjusted in its result.

### 12.6 Floating point

Built-in floating-point range validation rejects non-finite values unless a
policy explicitly permits them. NaN cannot accidentally bypass range checks or
equality semantics.

### 12.7 Custom validators

Custom validation must be deterministic, bounded, non-blocking, and free of
observable side effects. It must not read mutable parameter state or invoke
setters recursively.

### 12.8 Default validation

Solar validates the declared default at compile time. A custom validator used
with a default must support constant evaluation for that check.

## 13. Access Policy

### 13.1 Separate dimensions

Local C++ mutation policy and external exposure eligibility are separate.

Registration never implies external exposure.

### 13.2 Local access

Initial local policies are:

```cpp
solar::parameters::ReadWrite
solar::parameters::ReadOnly
solar::parameters::Privileged<Authority>
```

Default local policy is `ReadWrite`.

### 13.3 Read only

Ordinary typed `set<ReadOnlyParameter>` is a compile-time error.

Facility loading, schema migration, and trusted provisioning use internal
storage operations rather than bypassing policy through the public setter.

Read-only parameters remain configuration values such as factory calibration
or serial identity. They must not become a back door for publishing arbitrary
live component state.

### 13.4 Privileged mutation

Privileged setters require an authority token or explicitly authorized adapter:

```cpp
solar::parameters::set<CalibrationOffset>(value, authority);
```

C++ authority expresses architecture and prevents accidental ordinary calls. It
is not by itself a hostile-code security boundary inside one firmware image.

### 13.5 External eligibility

Initial external eligibility is:

```cpp
solar::parameters::LocalOnly
solar::parameters::ExternallyReadable
solar::parameters::ExternallyWritable<Permission>
```

Default is `LocalOnly`.

Eligibility sets the maximum allowed exposure. A later Remote configuration
must still explicitly expose the parameter and enforce authentication and
permission at runtime.

## 14. Runtime Storage

### 14.1 Slot ownership

The built-in parameter facility owns one type-specialized static slot per
effective parameter.

The slot contains at least:

- current value;
- revision;
- readiness state;
- load source and load outcome;
- persistence state;
- dirty and pending facts;
- stored and current schema versions;
- last persistence error;
- successful and failed write counters;
- synchronization state required by policy.

The declaration type does not own mutable canonical storage directly.

### 14.2 Initial value

Slots are constructed from `default_value`. Public reads remain not ready until
the facility has completed load and validation.

Descriptor queries remain available before boot because they are immutable.

### 14.3 Revisions

Each successful changed runtime commit increments a monotonically increasing
revision. Boot load establishes initial ready state and does not masquerade as
a runtime revision change.

Revision wrap is explicitly defined by the implementation and must not create
undefined behavior. Equality across a full wrap interval is not a durable
change-history guarantee.

### 14.4 Record synchronization

Focused record copies are mutex-protected or derived atomically. They never
expose mutable internal references after synchronization is released.

## 15. Synchronization

### 15.1 Default mutex policy

Default parameter storage is mutex-protected. `get` copies under lock and `set`
serializes through the facility write gate and slot storage.

This supports bounded structures without pretending every target has lock-free
atomics for every scalar.

### 15.2 Atomic policy

An explicit policy may request:

```cpp
using Storage = solar::parameters::Atomic;
```

It is accepted only when `std::atomic<Value>::is_always_lock_free` is true for
the target and the value otherwise satisfies the atomic contract.

Solar does not silently replace an explicitly requested atomic policy with a
mutex.

### 15.3 Write gate

One facility-wide write gate serializes:

- individual changed commits;
- coherent snapshots;
- multi-parameter transactions;
- reset-all operations;
- persistence-group image construction.

Parameters are expected to change relatively infrequently. Strong coherence
and simple lock ordering are preferred over concurrent configuration writers.

### 15.4 Lock ordering

The write gate is acquired before any slot lock. Snapshot and transaction code
uses deterministic parameter catalog order.

No change hook, storage backend call that may re-enter application code, bus
emission, log, or observability event executes while slot locks are held.

### 15.5 Hot paths

A control loop that cannot afford mutex-backed `get` on every iteration should:

- select a validated lock-free atomic parameter where appropriate; or
- cache the value in component-owned state and update the cache through a
  parameter change hook.

The parameter facility does not promise every value type is a zero-cost hot-loop
read.

## 16. Coherent Snapshots

### 16.1 Typed snapshot

```cpp
auto gains = solar::parameters::snapshot<
    DriveKp,
    DriveKi,
    DriveKd>();
```

The result is a typed value object containing one coherent point-in-time copy of
the requested registered parameters.

This is a deliberate and valid use of the word **snapshot** under Phase 0.

### 16.2 Coherence

The facility write gate prevents any parameter commit while snapshot values are
copied. A snapshot cannot contain half of one `set_all` transaction.

### 16.3 Result

Snapshot returns a typed expected result. It fails without a partial public
value when the facility is not ready or a required lock cannot be obtained by a
non-blocking variant.

### 16.4 Structured parameter preference

Values that are always consumed and changed together should normally be one
structured parameter:

```cpp
struct DrivePid
{
    struct Value
    {
        float kp;
        float ki;
        float kd;
    };

    static constexpr Value default_value{
        .kp = 1.2f,
        .ki = 0.1f,
        .kd = 0.02f,
    };
};
```

Snapshots and transactions remain valuable when separate identity and tooling
are genuinely required.

## 17. Atomic RAM Transactions

### 17.1 Explicit value operation

Solar does not use hidden thread-local or global `beginTransaction()` state.

```cpp
auto result = solar::parameters::set_all(
    solar::parameters::assign<DriveKp>(1.4f),
    solar::parameters::assign<DriveKi>(0.2f),
    solar::parameters::assign<DriveKd>(0.01f));
```

`assign<P>(value)` is a typed immutable candidate object. `set_all` owns the
operation lifetime explicitly.

### 17.2 Transaction algorithm

`set_all`:

1. validates registration, access, and duplicate assignments at compile time;
2. normalizes every candidate without mutation;
3. prepares required persistence under the effective policy;
4. acquires the facility write gate;
5. rechecks any runtime precondition that may have changed;
6. commits required immediate or transactional durable state;
7. commits every changed RAM value as one atomic operation;
8. increments affected revisions;
9. releases storage locks;
10. invokes or defers change hooks in deterministic catalog order;
11. returns one typed transaction result.

Any pre-commit error changes no runtime parameter.

### 17.3 Duplicate assignment

Assigning one parameter more than once in one transaction is a compile-time
error. Solar does not choose first-wins or last-wins semantics.

### 17.4 Runtime atomicity

Readers using `snapshot` observe either the complete old set or complete new
set. Individual `get` calls made separately may naturally observe time passing
between calls.

### 17.5 Persistence boundary

RAM atomicity does not automatically imply durable power-loss atomicity.

Transactions involving independently immediate-persisted keys require a store
with an explicit atomic batch capability or a configured transactional group.
Otherwise that combination is rejected rather than falsely documented as
durable.

## 18. Change Hooks

### 18.1 Purpose

Change hooks allow components to update cached configuration, recompute derived
state, or set a lightweight work flag after a parameter commit.

They are deliberately smaller than a second event bus.

### 18.2 Conventional contribution

```cpp
struct DriveController
{
    using ParameterChanges = solar::parameters::Changes<
        DriveKp,
        DriveKi,
        DriveKd>;

    static void changed(
        const solar::parameters::Change<DriveKp>& change);
};
```

The parameter subsystem owns the corresponding Phase 2 contribution adapter.
Change registrations are leaves owned by their component, not lifecycle
components.

### 18.3 Canonical handler

```cpp
static Return changed(const solar::parameters::Change<P>& change);
```

Accepted returns are:

- `void`;
- `solar::Status`;
- `solar::Result<void>`.

The normalization matches bus handlers.

### 18.4 Change value

`Change<P>` contains at least:

- old value copy;
- new value copy;
- new revision;
- update origin;
- whether validation adjusted the candidate;
- whether the change was part of a transaction or reset.

### 18.5 Post-commit behavior

Change hooks run synchronously after the write gate and slot locks are released.
They cannot veto or roll back a committed parameter.

Runtime hook failure is recorded in the parameter and change-registration
records and counted in the successful update result.

### 18.6 Transaction behavior

All transaction values commit before any change hook runs. A hook may therefore
take a coherent snapshot of the new transaction state.

Hooks run in deterministic change-registration catalog order.

### 18.7 Boot-time writes

Persistence loading and migration do not invoke application change hooks.

Public parameter writes during component initialization or start remain valid.
Because not every hook owner is ready yet, each affected change registration
retains one coalesced pending change from its first old value to final new value.

After all components have started, Phase 3 leaf activation invokes those
pending hooks before the system enters `Running`. Failure enters the boot report
as a parameter facility activation failure with a change-registration leaf
reference.

### 18.8 Runtime caching example

```cpp
void DriveController::changed(
    const solar::parameters::Change<DriveKp>&)
{
    gains_dirty_.store(true, std::memory_order_release);
}
```

The control loop can refresh a complete PID snapshot at its next safe boundary
rather than taking a parameter mutex every cycle.

### 18.9 Reentrant mutation

Hooks run without parameter locks and may update another parameter. Such calls
produce ordinary nested change processing.

Unbounded same-parameter recursion remains an application defect. Solar cannot
derive arbitrary handler-body cycles at compile time.

### 18.10 Explicit asynchronous adapters

A change hook may explicitly emit a bus message or submit executor work.

The parameter facility does not automatically use the bus, observability
events, metrics, logging, or Remote for change delivery.

## 19. Persistence Model

### 19.1 Optional capability

Persistence is optional. A firmware containing only volatile parameters does
not require Zephyr settings, a storage backend, codecs, migration scratch, or a
deferred worker.

### 19.2 Initial policies

```cpp
solar::parameters::persistence::Volatile
solar::parameters::persistence::Immediate<Store>
solar::parameters::persistence::Deferred<Store, QuietPeriod>
solar::parameters::persistence::Manual<Store>
solar::parameters::persistence::Transactional<Group>
```

Duration policy types such as `solar::parameters::delay::Milliseconds<N>` and
`solar::parameters::delay::Seconds<N>` keep units explicit without relying on a
`std::chrono::duration` object as a non-type template argument.

Public aliases may omit the nested `persistence` namespace when unambiguous,
but specifications retain the semantic grouping.

### 19.3 Volatile

Volatile parameters begin from their declared default on every boot. Runtime
updates never invoke a storage backend.

### 19.4 Immediate

Immediate policy synchronously persists the normalized candidate before
publishing the RAM change.

If persistence fails, RAM remains unchanged and `set` returns an error. Once
the storage commit succeeds, the bounded RAM publication cannot fail under a
valid architecture.

### 19.5 Deferred

Deferred policy commits RAM immediately, marks the slot dirty, and schedules a
write after the configured quiet period.

Another change restarts the quiet period. Only the newest settled value is
written.

Scheduler or later storage failure does not roll back accepted RAM. The update
returns success with dirty or failed persistence state, and focused records
retain the error.

Before the shared persistence worker is active, changes remain dirty and
coalesce without attempting submission. The quiet period begins when the worker
activates after component start. This is not treated as scheduler failure.

### 19.6 Manual

Manual policy commits RAM and remains dirty until explicit `save<P>()`, group
save, or `save_all()`.

### 19.7 Transactional

Transactional members persist through one declared group image. The group
defines stable identity, version, codec, store, and commit mode.

This is distinct from an arbitrary RAM `set_all` operation.

## 20. Persistence API

### 20.1 Save one

```cpp
solar::parameters::save<DriveKp>();
```

Save persists the current dirty value or transactional group image and clears
dirty state only after success.

Calling save on a clean parameter succeeds without writing.

### 20.2 Save all

```cpp
solar::parameters::save_all();
```

Save-all traverses dirty stores and groups deterministically. It returns a typed
report or error identifying partial persistence failure. It does not roll back
already successful independent writes.

### 20.3 Flush

```cpp
solar::parameters::flush();
```

Flush forces deferred work due now and waits for all parameter-owned persistence
work to finish or reach its bounded timeout.

It does not drain unrelated executor or Zephyr settings work.

### 20.4 Non-blocking persistence

Where needed, `try_save` and `try_flush` use no-wait semantics and preserve dirty
state on `WouldBlock`.

## 21. Transactional Persistence Groups

### 21.1 Group declaration

```cpp
struct DriveTuningGroup
{
    using Members = solar::parameters::Members<
        DriveKp,
        DriveKi,
        DriveKd>;

    using Store = SettingsStore;

    static constexpr auto stable_id = DriveTuningGroupId;
    static constexpr std::uint16_t version = 1;
};
```

The group is registered through parameter configuration rather than mixed into
the parameter declaration catalog.

### 21.2 One durable image

The group encodes all member values into one bounded versioned record and
commits that record through the selected backend's single-record atomicity.

On boot, Solar accepts the complete old image or complete new image. It does not
construct a mixed group from partially updated member keys.

### 21.3 Membership rules

A parameter belongs to at most one transactional persistence group. Group
members cannot also select independent immediate, deferred, or manual stores.

### 21.4 Group updates

Changing one member marks or writes the complete current group image according
to group policy. Equality suppression and deferred quiet periods still apply.

### 21.5 Limits

Group encoded size is compile-time bounded by Kconfig. Large groups that exceed
the ceiling are architecture errors.

## 22. Zephyr Settings Integration

### 22.1 Adapter boundary

The parameter facility uses a typed storage adapter over Zephyr settings. It
does not expose raw settings callbacks or string keys as the parameter API.

### 22.2 Keying

Persistent keys derive from stable parameter or group IDs inside one configured
Solar settings namespace.

Human-readable parameter names may appear in inspection metadata but are not
the canonical storage key.

### 22.3 Record envelope

A persistent record contains at least:

```text
record kind
stable parameter or group ID
schema version
encoded payload size
integrity information
encoded payload
```

Exact binary layout belongs to implementation and compatibility versioning.

### 22.4 Settings initialization

The storage adapter must define whether Zephyr settings initialization and load
are already platform-owned or explicitly requested by Solar. Solar must not
initialize the same global backend inconsistently with other firmware users.

### 22.5 Unknown records

Unknown stable IDs are ignored and counted during initial loading. They are not
interpreted through current names or deleted automatically.

Explicit maintenance policy may later remove obsolete keys.

## 23. Boot Loading

### 23.1 Deterministic sequence

Parameter facility init performs:

1. construct all slots from validated defaults;
2. initialize required store adapters;
3. load records by stable identity;
4. verify kind, size, integrity, and schema version;
5. decode current versions or run explicit migration;
6. validate and normalize loaded values;
7. apply load-failure policy;
8. establish runtime revisions and persistence state;
9. publish focused load records;
10. mark the facility ready for dependent component init.

### 23.2 Missing value

A missing persistent record normally selects the declared default. This is not
corruption and does not fail boot.

Policy decides whether a missing default should later be persisted. Default
behavior avoids an unnecessary boot write.

### 23.3 Invalid or corrupt value

Initial load policies are:

```cpp
solar::parameters::load::UseDefaultAndReport
solar::parameters::load::FailBoot
```

Default is `UseDefaultAndReport` unless a declaration or blueprint selects a
stricter policy.

Fallback is never silent. The parameter record identifies the stable ID,
failure reason, observed version, and selected default.

### 23.4 Boot failure

Strict load failure maps the rich parameter error to `Status` and fails the
parameter facility init hook. Phase 3 boot reporting identifies the facility
and affected parameter or group leaf.

### 23.5 Dirty fallback

Policy decides whether fallback-to-default is clean, dirty for later repair, or
requires explicit operator save. Solar does not repeatedly write flash at every
boot without a declared repair policy.

## 24. Schema Migration

### 24.1 Version-specific migration

A persistent parameter or group may declare bounded migrators from recognized
older versions.

The migrator receives encoded old data and produces the current typed value or
group image through `Result`.

### 24.2 Validation after migration

Migrated values pass through current validation before becoming canonical.

Migration success does not bypass new constraints.

### 24.3 Unknown newer version

Solar never interprets an unknown newer schema as current layout. It applies the
configured default-or-fail policy and preserves the detailed mismatch record.

### 24.4 Migration persistence

Successful migration may mark the value dirty so the current schema is written
later. Immediate rewrite during boot is policy-controlled to avoid unexpected
flash wear and boot latency.

### 24.5 Rename behavior

Renaming a parameter while retaining stable identity requires no value
migration. Changing conceptual identity requires a new stable ID and optional
explicit import migrator.

## 25. Reset Semantics

### 25.1 Reset one

```cpp
solar::parameters::reset<DriveKp>();
```

Reset validates the declared default, applies the same write synchronization,
commits the default when changed, increments revision, and invokes change hooks.

### 25.2 Persistent override removal

Reset logically removes the stored override so future boot resolves to the
declared default.

- immediate policy performs deletion before RAM publication;
- deferred policy marks deletion pending;
- manual policy remains dirty until save;
- transactional policy commits or marks the complete group image.

### 25.3 Reset all

`reset_all()` is a privileged facility operation. It uses one atomic RAM
transaction for eligible parameters and returns a report for persistence across
independent stores.

Read-only factory values are excluded unless the supplied authority explicitly
permits them.

### 25.4 Factory reset naming

A product-level “factory reset” may include parameters plus unrelated storage.
That application operation composes subsystem reset APIs. It is not a synonym
for unconditionally erasing every Solar-owned byte.

## 26. Dirty State And Wear

### 26.1 Persistence states

Focused state distinguishes at least:

- volatile;
- clean;
- dirty;
- scheduled;
- writing;
- failed;
- reset pending;
- migration pending.

### 26.2 Wear controls

Solar reduces avoidable writes through:

- equality suppression;
- deferred quiet periods;
- group batching;
- explicit manual save;
- no automatic persistence of missing defaults;
- no automatic rewrite loop after repeated failure;
- one shared dirty-set worker rather than per-parameter polling.

### 26.3 Shared worker

Deferred persistence uses one parameter-facility execution registration on an
effective shared executor. It does not create one thread, stack, timer, or work
queue per parameter.

Phase 9 finalizes the executor adapter while preserving this ownership.

### 26.4 Failure behavior

Failed deferred or manual writes leave current RAM intact and dirty. Successful
later save clears dirty state only for the value revision actually written.

If a concurrent update occurs during a write, the newer revision remains dirty.

## 27. Lifecycle

### 27.1 Facility inclusion

The built-in parameter facility is included when:

- `CONFIG_SOLAR_PARAMETERS` enables it; and
- the effective blueprint has parameters, parameter change hooks, or explicit
  parameter configuration requiring it.

Intentional parameter registration while disabled is a compile-time
normalization error. A relaxed call whose subsystem is disabled returns
`Disabled` when no contradictory registration exists.

### 27.2 Implicit application dependency

When present, the parameter facility is an implicit dependency of ordinary
application components. This guarantees persistent values are ready before
component `init()` without requiring application headers to name an internal
Solar facility.

The facility itself depends on its storage adapters and any executor required
for deferred persistence. Those bootstrap dependencies are exempt from the
reverse generated edge and must not use parameters while bringing parameter
storage online.

Blueprint validation detects any resulting cycle and identifies the bootstrap
component involved.

### 27.3 Availability

Immutable descriptors are queryable before boot.

Typed `get`, `set`, `try_set`, reset, and snapshot operations become available
after parameter facility init completes. They remain available during later
component initialization and start.

New mutation closes when system stopping begins. Reads remain available while
the facility is initialized so component stop hooks may inspect configuration.

### 27.4 Start and change activation

The facility start operation prepares deferred execution. After all components
have started, parameter change leaf activation delivers coalesced pre-running
changes before the system enters `Running`.

### 27.5 Stop

Deferred persistence work is an executor registration. Phase 3 executor
containment first flushes or cancels that registration according to parameter
stop policy before the shared worker is stopped.

The initial stop policies are:

```cpp
solar::parameters::stop::FlushDeferred
solar::parameters::stop::CancelPending
```

Manual persistence is never silently converted to flush-on-stop merely because
deferred policy uses `FlushDeferred`.

The facility stop hook then:

1. rejects new mutation;
2. quiesces change-hook invocation;
3. verifies deferred persistence registration quiescence;
4. records unsaved dirty values;
5. releases executor registrations;
6. preserves storage dependencies until writes are contained.

Shutdown policy must not silently turn every dirty manual parameter into an
automatic write.

### 27.6 Deinit

Deinit clears readiness and releases native adapter state. Static descriptor and
catalog metadata remain inspectable.

### 27.7 Failure attribution

Load, migration, store, pending change activation, and flush failures enter
lifecycle reports through the parameter facility with an optional parameter,
group, or change-registration leaf reference.

## 28. Change Hook Identity And Records

### 28.1 Registration identity

The default logical key is:

```text
parameter type + observing component type + change-route tag
```

One default hook per component and parameter requires no explicit tag. Multiple
intentional hooks require distinct tags and explicit handler routes.

### 28.2 Change record

Each registration records:

- invocation count;
- deferred/coalesced count during startup;
- success and failure counts;
- last normalized status;
- last observed parameter revision;
- currently invoking fact;
- activation failure where applicable.

### 28.3 No queue

Runtime change hooks are synchronous and do not own an asynchronous queue.
Startup retains only one coalesced old-to-final change per registration.

Applications needing every intermediate occurrence must explicitly model those
occurrences on the bus or as observability events.

## 29. Focused Parameter Records

### 29.1 Descriptor queries

```cpp
solar::parameters::descriptors();
solar::parameters::descriptor<DriveKp>();
```

The descriptor view includes authored metadata plus catalog owner, origin,
local ID, stable ID, schema version, value schema, validation metadata where
describable, and effective policy summaries.

### 29.2 Runtime record query

```cpp
solar::parameters::record<DriveKp>();
```

The typed record includes:

- current value copy where requested by the typed API;
- revision;
- readiness;
- load source and outcome;
- validation adjustment facts;
- persistence state;
- dirty and pending revisions;
- current and stored versions;
- last persistence error;
- read, update, rejection, save, and failure counts;
- last update origin;
- change-hook failure count.

### 29.3 Generic inspection

Phase 11 may provide type-erased descriptor and encoded-value views for shell or
inspection tooling. That view uses registered codecs and never returns an
untyped pointer to canonical storage.

### 29.4 No universal snapshot

Parameter snapshots remain typed, explicit point-in-time copies. They do not
become a universal system snapshot containing unrelated subsystems.

## 30. Change Origins

The canonical record and `Change<P>` distinguish origins such as:

- local set;
- local transaction;
- reset;
- privileged provisioning;
- explicit external adapter;
- migration or boot load in records only.

Boot load and migration update source records but do not invoke runtime change
hooks.

External adapters cannot forge an ordinary local origin merely to bypass
access, auditing, or permission behavior.

## 31. Remote Boundary

Remote consumes the parameter catalog and validated APIs. It does not own
parameter values, slots, persistence, validation, or dirty state.

Remote exposure requires all of:

- external eligibility on the parameter;
- explicit Remote exposure registration;
- a stable external identity mapping;
- an available codec;
- runtime authorization for writes;
- size and rate-limit policy.

Remote reads use the same coherent copy semantics. Remote writes call the same
validation and commit machinery with an external origin.

No Remote adapter may mutate a read-only or local-only parameter through an
internal storage escape hatch.

## 32. Bus, Events, Metrics, And Logging Boundaries

The parameter facility does not automatically:

- emit a bus message on change;
- record an observability event on change or persistence failure;
- increment application metrics;
- write logs;
- publish Remote notifications.

Canonical parameter and change records remain available even when those sibling
subsystems are disabled.

Explicit adapters may translate selected changes or failures after later
subsystem specifications define their bounded behavior.

## 33. Configuration

### 33.1 Kconfig ownership

Kconfig owns build capability and hard ceilings such as:

- parameter subsystem inclusion;
- Zephyr settings adapter inclusion;
- persistence and migration support;
- deferred worker capability;
- maximum encoded parameter record size;
- maximum transactional group record size;
- migration scratch capacity;
- descriptor text retention;
- default load-failure policy;
- default persistence mode;
- default persistence and stop timeouts;
- focused diagnostic record support.

There is no C++ fallback configuration header.

### 33.2 Typed C++ ownership

C++ declarations own:

- parameter membership;
- value and default;
- descriptor and stable identity;
- schema version and codec;
- validation and normalization;
- local access and external eligibility;
- storage synchronization policy;
- persistence mode and store;
- transactional group membership;
- change-hook registrations.

### 33.3 Precedence

```text
explicit parameter or group policy
    > parameter blueprint configuration
    > Kconfig default
```

No typed policy may re-enable excluded settings, migration, atomic, executor, or
storage capability or exceed a hard size ceiling.

### 33.4 Default policy

The safe baseline is:

- local `ReadWrite`;
- external `LocalOnly`;
- mutex storage;
- no custom validator beyond type validity;
- volatile persistence;
- use-default-and-report load failure.

Projects may change defaults in parameter configuration without mixing policy
types into the parameter catalog.

## 34. Resource Accounting

The effective system computes parameter-owned resources from the catalog:

- one exact typed value slot per parameter;
- one slot mutex unless atomic policy removes it;
- one bounded runtime record per parameter;
- one change record per change registration;
- one pending startup change per change registration;
- one dirty bit and revision state per persistent parameter or group;
- one shared deferred worker registration when required;
- bounded codec and migration scratch from Kconfig.

Volatile parameters do not pay for persistence record buffers or settings work
merely because persistence support is compiled for another parameter.

## 35. Complete Example

```cpp
struct DriveKp
{
    using Value = float;

    static constexpr solar::parameters::Descriptor descriptor{
        .name = "drive.pid.kp",
        .description = "Drive velocity proportional gain",
    };

    static constexpr Value default_value = 1.2f;

    using Validation = solar::parameters::Range<
        0.0f,
        10.0f,
        solar::parameters::Reject>;

    using Persistence = solar::parameters::Deferred<
        SettingsStore,
        solar::parameters::delay::Seconds<2>>;
};

struct DriveKi
{
    using Value = float;

    static constexpr solar::parameters::Descriptor descriptor{
        .name = "drive.pid.ki",
    };

    static constexpr Value default_value = 0.1f;
};

struct DriveController
{
    using Parameters = solar::parameters::Parameters<
        DriveKp,
        DriveKi>;

    using ParameterChanges = solar::parameters::Changes<
        DriveKp,
        DriveKi>;

    static void changed(
        const solar::parameters::Change<DriveKp>& change);

    static void changed(
        const solar::parameters::Change<DriveKi>& change);
};

using RobotBlueprint = solar::Blueprint<
    solar::Facilities<DriveController>,
    solar::parameters::Configuration<
        solar::parameters::DefaultPersistence<
            solar::parameters::persistence::Volatile>>>;

using RobotSystem = solar::System<RobotBlueprint>;

SOLAR_BIND_SYSTEM(RobotSystem);
```

Runtime use:

```cpp
auto update = solar::parameters::set<DriveKp>(1.4f);
if (!update)
{
    return solar::fail(update.error());
}

auto gains = solar::parameters::snapshot<DriveKp, DriveKi>();
```

## 36. Include Direction

Parameter declaration headers include only their value-domain types and Solar
parameter declaration headers.

Component headers include parameter declarations they read or observe and the
Solar contribution headers they require. They never include the application
composition root.

Definitions calling bound global APIs normally live in source files that
include the completed root:

```cpp
// control/drive_controller.cpp
#include "app/system.hpp"

solar::Result<void> DriveController::configure()
{
    return solar::parameters::get<DriveKp>()
        .and_then([](float kp) {
            return Controller::set_kp(kp);
        });
}
```

## 37. Compile-Time Validation

Effective-system validation rejects:

- blueprint policy or adapters referencing an unregistered parameter;
- missing or invalid `Value`;
- missing or non-constant default;
- default rejected or altered by validation;
- unsupported value copy or equality semantics;
- duplicate type, name, local identity, or stable identity;
- persistent parameters without stable identity;
- persistent or exposed values without a codec;
- encoded size beyond Kconfig ceilings;
- direct mutation of a read-only parameter;
- privileged mutation without an authority-capable overload;
- invalid validator signature or error type;
- requested atomic storage that is not always lock-free;
- duplicate assignment in one transaction;
- independently immediate-persisted transaction without atomic batch support;
- one parameter in multiple transactional groups;
- conflicting group and individual persistence policy;
- invalid change-hook signature or return type;
- duplicate change-hook identity;
- persistence requested while support is disabled;
- external exposure eligibility requiring disabled codec capability;
- parameter registration while the subsystem is disabled;
- generated lifecycle dependency cycles.

Diagnostics identify the parameter, owner, policy, and violated capability
instead of failing only inside final slot tuple generation.

## 38. Runtime Failure Behavior

Runtime errors remain possible after valid compilation:

- relaxed frontend use before binding, while disabled, or with an unregistered
  parameter;
- API use before readiness or after mutation closes;
- lock contention in no-wait operations;
- candidate validation rejection;
- storage backend unavailable;
- codec or integrity failure;
- schema mismatch without migration;
- immediate write failure;
- deferred scheduling or later write failure;
- change-hook failure after commit;
- flush or shutdown timeout.

The strong setter invariant distinguishes pre-commit errors from post-commit
operational facts. Focused records preserve both.

## 39. Migration Direction

Solar currently has no canonical runtime parameter subsystem to preserve.

Migration primarily means replacing application patterns such as:

- mutable global tuning variables;
- compile-time constants modified for every test;
- ad hoc Zephyr settings keys;
- service-owned configuration maps;
- Remote-owned parameter values;
- callbacks wired directly into storage code.

The target centralizes canonical configuration state in the parameter facility
while leaving domain behavior and cached operational state in components.

## 40. Verification Requirements

The implementation must eventually cover:

- compact `Parameters` contribution collection;
- owner and origin preservation;
- strict and relaxed registration diagnostics plus duplicate diagnostics;
- valid and invalid value concepts;
- compile-time default validation;
- reject, clamp, enum, and custom normalization;
- typed `get`, `try_get`, `set`, and `try_set`;
- strong no-change-on-error setter behavior;
- equal-value suppression;
- mutex and valid lock-free atomic storage;
- invalid atomic-policy diagnostics;
- coherent snapshots under concurrent writers;
- all-or-none RAM `set_all` transactions;
- duplicate transaction assignment diagnostics;
- synchronous post-commit change hooks;
- startup change coalescing and activation;
- transaction hooks observing complete new state;
- change-hook failure without rollback;
- volatile, immediate, deferred, and manual persistence;
- transactional group persistence;
- dirty revision retained across concurrent write completion;
- missing, corrupt, invalid, and newer-version boot records;
- default fallback and strict boot failure;
- successful and failed migration;
- reset override deletion semantics;
- save, save-all, flush, and no-wait forms;
- write-wear suppression and quiet-period restart;
- readiness during dependent component init;
- mutation closure during stop;
- dependency preservation for uncontained persistence work;
- local, read-only, privileged, and local-only access;
- explicit external exposure enforcement;
- descriptor, record, and snapshot queries;
- Kconfig default and typed override precedence.

Host tests should exercise codecs and migration with golden record bytes.
Zephyr integration tests should exercise settings behavior, power-loss-safe group
selection where supported, executor work, and concurrent kernel access.

## 41. Deferred Capabilities

The following remain deliberate later work:

- richer generated UI field metadata;
- arbitrary-length string and blob storage;
- cross-device or network-backed parameter stores;
- dynamic profile selection;
- live rollback to a prior persistent generation;
- generalized optimistic compare-and-set transactions;
- automatic C++ reflection-derived codecs and schemas;
- host-generated migration tables;
- stable shared-memory views for large immutable values;
- broader ISR mutation support if a safe use case emerges.

Deferred capabilities must preserve static registration, bounded resources,
typed validation, and canonical facility ownership.

## 42. Rejected Alternatives

### 42.1 Call the canonical subsystem system variables

Rejected because parameters is the established architecture vocabulary. System
Variables remains acceptable product-facing terminology.

### 42.2 Parameter declaration owns mutable static state

Rejected because it bypasses one canonical facility, synchronization,
inspection, persistence, and policy normalization.

### 42.3 Return mutable references from get

Rejected because callers could race and bypass validation, revision,
persistence, and hooks.

### 42.4 Infallible get before readiness

Rejected because returning a default while persistence is still loading would
present stale configuration as canonical truth.

### 42.5 Try-set means validation-only

Rejected because all setters validate. `try_` consistently means no waiting.

### 42.6 Return failure after committing RAM

Rejected because callers could retry an operation that already changed state.
Post-commit observer and deferred-storage facts belong in successful results and
records.

### 42.7 Always clamp invalid values

Rejected because safety and tuning parameters often require explicit rejection.
Normalization is declared policy.

### 42.8 One access enum mixing local and Remote policy

Rejected because local mutability, external eligibility, explicit exposure, and
runtime authorization are separate concerns.

### 42.9 Automatic lock-free scalar storage

Rejected because target atomic guarantees vary and configuration writes must
still coordinate with snapshots and transactions.

### 42.10 One mutex per operation with no write gate

Rejected because coherent multi-parameter snapshots and transactions would be
difficult to guarantee without a stable cross-slot ordering boundary.

### 42.11 Hidden begin/commit transaction state

Rejected because it introduces implicit lifetime, nesting, thread-local, and
rollback behavior. Explicit typed assignments are clearer.

### 42.12 Claim durable atomicity across Zephyr settings keys

Rejected because RAM locking does not create a power-loss-safe multi-key store.
Transactional groups use one bounded durable image.

### 42.13 Invoke change hooks while holding parameter locks

Rejected because handlers may read parameters, call components, or request
other work and would create deadlock and latency hazards.

### 42.14 Make parameter changes automatic bus messages

Rejected because parameters and behavior messages remain independently usable,
and not every change needs asynchronous fan-out.

### 42.15 Make parameter changes observability events

Rejected because change behavior and durable diagnostic recording have
different semantics and optionality.

### 42.16 Invoke hooks during persistence loading

Rejected because component owners are not initialized. Runtime pre-running
writes are coalesced and activated only after owners start.

### 42.17 Persist every accepted write immediately

Rejected because it creates avoidable latency and flash wear for interactive
tuning.

### 42.18 Roll back RAM after deferred write failure

Rejected because components may already have observed and acted on the accepted
runtime value.

### 42.19 Use human names as canonical storage keys

Rejected because rename would silently orphan persistent values.

### 42.20 Automatically expose registered parameters through Remote

Rejected because registration is not authorization, schema exposure, or access
policy.

### 42.21 Let Remote own parameter storage

Rejected because Remote is an adapter and protocol boundary, not the owner of
canonical firmware configuration.

### 42.22 Store arbitrary component state as read-only parameters

Rejected because that would turn parameters into a universal state container.
Metrics, component APIs, and focused records retain their own truth.

## 43. Accepted Decisions

1. The canonical subsystem name and namespace are parameters.
2. System Variables is optional product-facing terminology only.
3. Parameters are globally identifiable runtime configuration values.
4. Parameters do not store arbitrary component state.
5. Declaration types provide local identity and declare `Value`.
6. Authored metadata uses `solar::parameters::Descriptor`.
7. Every parameter declares one constant valid default.
8. Validation, persistence, access, and storage remain typed policy.
9. The conventional component contribution alias is `Parameters`.
10. Root-owned declarations use `solar::Parameters`.
11. Contributions preserve semantic owner and registration origin.
12. Typed operations require effective catalog registration.
13. Persistent parameters require stable explicit or manifest identity.
14. Schema version remains separate from stable identity.
15. Values use owned, copyable, bounded semantics.
16. Persistent and exposed values require deterministic codecs.
17. `get` returns `Result<Value, Error>` by value.
18. `try_get` and `try_set` are strictly non-blocking.
19. General ISR mutation is not initially supported.
20. `set`, `try_set`, and reset return typed `Update` values.
21. An unexpected setter result guarantees no runtime value changed.
22. Post-commit failures remain visible through successful results and records.
23. Equal values do not increment revision, notify, or schedule persistence.
24. Validation normalizes or rejects before mutation.
25. Initial validation includes range reject, range clamp, one-of, and custom.
26. Declared defaults are validated at compile time.
27. Local access and external exposure eligibility are separate.
28. Default access is locally read-write and externally local-only.
29. Read-only typed mutation is a compile-time error.
30. Privileged access is explicit and does not replace runtime Remote security.
31. Canonical state lives in type-owned facility slots.
32. Default synchronization is mutex-protected.
33. Atomic policy is explicit and requires target lock-free support.
34. One write gate coordinates writes, snapshots, and transactions.
35. Parameter reads never expose mutable canonical references.
36. Typed snapshots provide coherent point-in-time copies.
37. A structured parameter is preferred for values always used together.
38. `set_all(assign...)` provides explicit all-or-none RAM transactions.
39. Hidden begin/commit transaction state is not used.
40. RAM transaction atomicity does not imply durable atomicity.
41. Change hooks are component-owned leaf registrations.
42. The conventional change contribution alias is `ParameterChanges`.
43. Change handlers accept `void`, `Status`, or `Result<void>`.
44. Change hooks run synchronously after locks are released.
45. Change-hook failure does not roll back committed values.
46. Transactions commit all values before invoking any hook.
47. Pre-running writes coalesce one pending change per registration.
48. Persistence loading and migration do not invoke application hooks.
49. Asynchronous change behavior uses an explicit bus or executor adapter.
50. Persistence is optional and absent for purely volatile systems.
51. Initial persistence modes are volatile, immediate, deferred, manual, and
    transactional.
52. Immediate persistence failure leaves RAM unchanged.
53. Deferred and manual failure leave accepted RAM dirty.
54. Deferred persistence uses one shared facility execution registration.
55. Persistent groups store one bounded versioned durable image.
56. One parameter belongs to at most one persistent group.
57. Zephyr settings is accessed through a typed storage adapter.
58. Persistent keys derive from stable IDs rather than names.
59. Missing records default without being treated as corruption.
60. Invalid load uses explicit default-and-report or fail-boot policy.
61. Unknown newer schemas are never reinterpreted as current values.
62. Reset restores the declared default and removes the persistent override.
63. Dirty state distinguishes current RAM from durable state.
64. Equality, quiet periods, and batching reduce unnecessary writes.
65. The parameter facility is included only when enabled and required.
66. The facility is an implicit dependency of ordinary components when present.
67. Parameters are readable and mutable after facility init during boot.
68. New mutation closes when system stopping begins.
69. Reads remain available until facility deinit.
70. Focused records own load, revision, dirty, and persistence truth.
71. Remote consumes catalogs and validated APIs without owning storage.
72. Registration never implies Remote exposure.
73. Bus, events, metrics, and logging integrations are explicit adapters.
74. Kconfig owns build capability, hard ceilings, and defaults.
75. C++ policy owns value, identity, validation, access, and persistence.
76. Explicit declaration policy overrides blueprint policy, then Kconfig.
77. Runtime storage and persistence require no dynamic allocation.
78. There is no parameter facility object in the user-facing API.
79. Pre-worker deferred writes remain dirty and begin their quiet period when
    shared execution activates.
80. Deferred persistence quiescence participates in Phase 3 executor
    containment before facility stop verification.

## 44. Open Questions

There are no blocking open questions for Phase 6.

Later specifications and implementation must refine without changing this
contract:

- exact Kconfig symbol names and numeric ceilings;
- final shared-executor adapter from Phase 9;
- concrete Zephyr settings envelope and integrity algorithm;
- generated codec and migration tooling;
- exact Remote exposure syntax and permission vocabulary;
- optional bus, event, metric, and logging adapters;
- richer UI metadata and host schema generation;
- future C++ reflection-based value schema derivation.

These are extensions of the accepted identity, storage, validation,
persistence, notification, and lifecycle model.
