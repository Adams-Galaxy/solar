# Stage 09: Runtime Parameters

Status: landed

Landed date: 2026-07-16

Implementation repository/branch: `/workspaces/solar`, `static_reform`

Relevant commits or change identifiers: uncommitted reform working tree

## 1. Objective

Stage 09 lands Solar's typed runtime configuration subsystem. Parameters are
compile-time declarations in the effective System blueprint, while one
demand-derived facility owns their exact runtime values, revisions, records,
change state, and persistence facts.

The landed subsystem provides:

- root and component-local parameter catalogs;
- component-local change-hook registration;
- typed defaults, validation, access, exposure, storage, and persistence policy;
- mutex, immutable, and always-lock-free atomic value cells;
- ordinary, bounded no-wait, and atomic ISR read paths;
- coherent typed snapshots and all-or-none RAM transactions;
- synchronous post-commit hooks and coalesced startup notification;
- volatile, immediate, deferred, manual, and transactional persistence;
- stable bounded persistence envelopes, migration, fallback, and reset behavior;
- fake-store and Zephyr settings adapters;
- typed parameter, change, descriptor, update, transaction, persistence, and
  error records;
- strict and relaxed global frontends over one canonical System-owned state;
- Kconfig capability switches, policy defaults, and hard resource ceilings;
- no heap, dynamic registration, hidden thread, or Parameters-owned workqueue.

## 2. Specification Coverage

| Specification | Sections implemented | Notes |
| --- | --- | --- |
| `05-parameters.md` | complete Stage 09 parameter contract | Declarations, policies, storage, validation, transactions, hooks, persistence, records, and lifecycle are landed. |
| `01-system-blueprint-and-binding.md` | Parameters sections, bound frontend, demand-derived built-in | Parameters operate against the effective bound System and support `Of<Application>` for alternate bindings. |
| `02-identity-contributions-and-catalogs.md` | `Parameters` and `ParameterChanges` aliases, semantic ownership, duplicate validation | Change declarations are bound to their component owner during generic contribution normalization. |
| `03-lifecycle-kernel-and-configuration.md` | facility lifecycle, dependency derivation, Kconfig precedence | Persistent values load before component initialization; mutation and change delivery activate in lifecycle order. |
| `09-tasks-and-executors.md` | generated deferred-persistence work | Deferred persistence contributes one typed delayable registration to an explicit Stage 07 target. |

Automatic Bus, Event, Metric, Log, or Remote side effects remain intentionally
outside Parameters. Those subsystems may build explicit adapters over the
records and typed API in their own stages.

## 3. Public Surface Landed

The public aggregate is:

```cpp
#include <solar/parameters.hpp>
```

The common declaration path requires only a value, descriptor, and default:

```cpp
struct DriveKp
{
    using Value = float;

    static constexpr solar::parameters::Descriptor descriptor{
        .name = "drive.kp",
        .units = "",
        .stable_id = solar::parameters::Id{0x1001},
        .version = 1,
    };

    static constexpr Value default_value = 1.2f;
    using Validation = solar::parameters::Range<0.0f, 10.0f>;
    using Persistence = solar::parameters::Deferred<
        solar::parameters::settings::ZephyrStore,
        solar::parameters::delay::Seconds<2>>;
};
```

Components contribute compact aliases and optional hooks:

```cpp
struct DriveController
{
    static constexpr solar::component::Descriptor descriptor{
        .name = "drive-controller",
    };

    using Parameters = solar::parameters::Parameters<DriveKp, DriveKi>;
    using ParameterChanges = solar::parameters::Changes<DriveKp>;

    static solar::Status changed(
        const solar::parameters::Change<DriveKp>& change);
};
```

Application-owned declarations and subsystem configuration use separate
Blueprint sections:

```cpp
using Robot = solar::System<solar::Blueprint<
    solar::Facilities<DriveController>,
    solar::Parameters<RobotNumber>,
    solar::parameters::Configuration<
        solar::parameters::PersistenceExecutor<StorageQueue>,
        solar::parameters::PersistenceGroups<TuningGroup>>>>;
```

The normal global API includes:

- `get<Parameter>()`, `try_get<Parameter>()`, and `get_isr<Parameter>()`;
- `set<Parameter>(value)`, `try_set<Parameter>(value)`, and privileged set;
- `reset<Parameter>()`;
- `snapshot<...>()` and `try_snapshot<...>()`;
- `set_all(assign<Parameter>(value), ...)` and `try_set_all(...)`;
- `save<Parameter>()`, `try_save<Parameter>()`, `save_all()`, and
  `try_save_all()`;
- `flush()` and `try_flush()`;
- `record<Parameter>()` and `change_record<Observer, Parameter>()`;
- `descriptors()` and `descriptor<Parameter>()`;
- equivalent operations through `parameters::Of<Application>`.

Set operations return the effective value, revision, adjustment, persistence,
and hook outcome. Equal values suppress revisions, persistence work, and
change callbacks.

## 4. Policy Model

Validation policies are `AcceptAny`, rejecting or clamping `Range`, `OneOf`,
and typed `Custom` validators. Custom validators may reject or return a
normalized value. Declared defaults are checked against the effective
validator at compile time.

Access policies are `ReadWrite`, `ReadOnly`, and `Privileged<Authority>`.
External exposure is independently described by `LocalOnly`,
`ExternallyReadable`, and `ExternallyWritable<Permission>`; exposure metadata
does not itself create a Remote endpoint.

Storage policies are:

- `storage::Mutex` for the general copyable-value path;
- `storage::Atomic` for always-lock-free scalar values and ISR reads;
- `storage::Immutable` for boot-loaded read-only values.

Persistence policies are `Volatile`, `Immediate<Store>`, `Deferred<Store,
Delay>`, `Manual<Store>`, and `Transactional<Group>`. Policy precedence follows
the shared rule: declaration policy, then typed System configuration, then
Kconfig default.

## 5. Runtime Ownership

| Owner | Storage/resource | Capacity | Synchronization | Lifetime |
| --- | --- | --- | --- | --- |
| generated Parameters facility | readiness, mutation admission, write and persistence gates | one facility only when demanded | mutexes and atomics | one static typed System slot |
| parameter slot | exact `Value`, revisions, persistence state, and record facts | one per effective parameter | selected mutex/atomic/immutable cell plus record spinlock | System lifetime |
| change leaf | invocation/coalescing state and record | one per effective change route | spinlock and atomics | System lifetime |
| persistence group | dirty revision, due time, and group facts | one per registered group | facility persistence serialization | System lifetime |
| deferred work | one typed delayable Stage 07 registration | zero or one for the complete facility | Stage 07 work synchronization | System lifetime |
| store adapter | backend-specific state | store-defined and statically bounded by contract | adapter-defined | adapter-defined |
| relaxed frontend | non-owning operation bindings | one per application/parameter/operation | atomic binding | program lifetime |

Parameters owns no thread, stack, heap, or workqueue. When deferred persistence
uses `execution::SystemWorkQueue`, the queue remains Zephyr-owned. An explicit
named workqueue remains an application component and a derived facility
dependency.

The representative 21-parameter runtime test image measured 274,408 B text,
8,861 B data, and 28,856 B BSS. This image includes Ztest, all prior Solar
foundations, persistence fixtures, concurrency threads, and records. Its exact
parameter slots are visible as static symbols; the largest tested slot is 248
bytes. The linked ELF has no undefined `malloc`, `calloc`, `realloc`, global
`new`, or global `delete` symbols.

## 6. Concurrency And Transactions

Ordinary reads return values, never references to mutable canonical storage.
`try_get`, `try_set`, `try_snapshot`, `try_set_all`, `try_save`, `try_save_all`,
and `try_flush` do not wait for contended Solar locks. They return a typed
`WouldBlock` reason.

`get_isr` is available only for explicitly atomic, always-lock-free values and
requires actual ISR context. General mutation is not ISR-safe.

Snapshots acquire one facility write boundary and return a typed value tuple,
so readers never observe a partially committed multi-parameter transaction.
`set_all` validates every assignment and prepares immediate durable state
before committing any RAM value. Duplicate assignments, unregistered members,
and invalid policy combinations fail at compile time. Runtime validation or
immediate persistence failure leaves the complete RAM transaction unchanged.

Post-commit hooks run synchronously in deterministic catalog order after the
facility lock is released. A hook failure is recorded and reported but does
not roll back committed parameter state. During boot, loaded changes coalesce
and activate before the application reaches `Running`.

## 7. Persistence And Zephyr Integration

Persistent declarations require a stable ID, bounded codec, and versioned
record envelope. Built-in scalar codecs use deterministic little-endian
encoding. The adapter contract is a small static API over bounded spans:

```cpp
initialize()
load(key, output)
save(key, input)
erase(key)
```

Current-version records load before component initialization. Missing records
use the declared default. Corrupt, unsupported, or backend-failed loads either
use the default and retain the failure fact or fail boot according to the
effective load policy. Typed migration may translate an older payload;
migrated state remains dirty until an explicit or policy-driven rewrite.

Immediate persistence completes before RAM commit. Deferred changes restart a
quiet-period deadline and use one native-coalescing delayable registration.
Manual values remain dirty until saved. Transactional groups encode and write
one group image, so their durable boundary matches their RAM boundary.

Persistence sweeps are deterministic and attempt every independent dirty
parameter/group even after an earlier backend failure. The first typed error is
returned while every failed slot remains dirty and every later successful slot
advances its persisted revision. A completed older write cannot clear a newer
dirty revision.

The Zephyr settings adapter uses:

- `settings_load_one`, `settings_save_one`, and `settings_delete`;
- stable keys shaped as `<namespace>/<p|g>/<16-hex-digit-id>`;
- optional `settings_subsys_init()` ownership controlled by Kconfig;
- the configured namespace, defaulting to `solar`.

Solar does not own a settings backend. Board/application configuration enables
and configures the appropriate Zephyr settings storage.

## 8. Lifecycle And Shutdown

The demand-derived Parameters facility is inserted before components that
contribute parameters or change hooks. Store and explicit executor dependencies
are derived into the generated graph without changing authored component
types.

Facility initialization validates static architecture, initializes each unique
store once, loads persistent values, and exposes them to later component
`init()` hooks. Facility start opens mutation, activates coalesced startup
hooks, and schedules any deferred persistence.

Stop first closes mutation. The effective stop policy either flushes deferred
state or leaves it dirty after cancelling pending work. The Kconfig timeout
bounds waiting for already-running deferred registration work. Store adapter
operations are synchronous by contract, so a backend that can block must own
and enforce its device/operation timeout; Solar cannot preempt an adapter call
already executing in the caller's context.

## 9. Compile-Time Behavior

The effective architecture combines:

- root `solar::Parameters<...>` declarations;
- component-local `using Parameters = parameters::Parameters<...>`;
- component-local `using ParameterChanges = parameters::Changes<...>`;
- typed `parameters::Configuration<...>` policy;
- registered persistence groups and their member declarations.

Catalog normalization retains owner and origin, rejects duplicate declarations
and logical change routes, derives generated component dependencies, and
selects the built-in only when declarations, hooks, or typed configuration
demand it.

Strict mode rejects unregistered operations at compile time. Relaxed mode keeps
the same successful spelling and canonical state, but an unregistered operation
returns `Reason::NotRegistered`. Both modes, alternate application binding, and
the disabled capability are independently exercised.

Stable compile-fail contracts cover:

- invalid defaults and custom validators;
- read-only and privileged ordinary mutation;
- non-lock-free atomic storage;
- persistence use while the capability is disabled;
- missing stable identity or codec;
- duplicate transaction assignments;
- unregistered or malformed change hooks;
- duplicate logical change routes;
- absent groups and mismatched group stores;
- impossible independent immediate transactional durability;
- strict unregistered access;
- catalog ceilings;
- requiring the built-in while Parameters is disabled.

## 10. Kconfig Surface

The landed Parameters controls are:

- `CONFIG_SOLAR_PARAMETERS`;
- `CONFIG_SOLAR_PARAMETERS_MAX_PARAMETERS`;
- `CONFIG_SOLAR_PARAMETERS_MAX_CHANGE_HOOKS`;
- `CONFIG_SOLAR_PARAMETERS_MAX_VALUE_BYTES`;
- `CONFIG_SOLAR_PARAMETERS_PERSISTENCE`;
- `CONFIG_SOLAR_PARAMETERS_ZEPHYR_SETTINGS`;
- `CONFIG_SOLAR_PARAMETERS_SETTINGS_NAMESPACE`;
- `CONFIG_SOLAR_PARAMETERS_SETTINGS_INITIALIZE`;
- `CONFIG_SOLAR_PARAMETERS_MIGRATION`;
- `CONFIG_SOLAR_PARAMETERS_MAX_ENCODED_RECORD_BYTES`;
- `CONFIG_SOLAR_PARAMETERS_MAX_GROUP_RECORD_BYTES`;
- `CONFIG_SOLAR_PARAMETERS_DEFERRED_SYSTEM_WORKQUEUE_DEFAULT`;
- `CONFIG_SOLAR_PARAMETERS_DEFAULT_LOAD_FAILURE` choice;
- `CONFIG_SOLAR_PARAMETERS_DEFAULT_STOP` choice;
- `CONFIG_SOLAR_PARAMETERS_PERSISTENCE_TIMEOUT_MS`.

When Parameters is disabled, public aggregate and subsystem headers remain
directly includable, relaxed runtime calls report `Disabled`, and a System that
requires the built-in fails architecture validation. Persistence-specific raw
Kconfig symbols do not leak into the disabled parser path.

## 11. Files Changed

### Added

- `include/solar/parameters.hpp`
- `include/solar/parameters/api.hpp`
- `include/solar/parameters/catalog.hpp`
- `include/solar/parameters/change.hpp`
- `include/solar/parameters/codec.hpp`
- `include/solar/parameters/contribution.hpp`
- `include/solar/parameters/declaration.hpp`
- `include/solar/parameters/facility.hpp`
- `include/solar/parameters/persistence.hpp`
- `include/solar/parameters/policy.hpp`
- `include/solar/parameters/protocol.hpp`
- `include/solar/parameters/runtime.hpp`
- `include/solar/parameters/settings.hpp`
- `include/solar/parameters/types.hpp`
- `tests/zephyr/parameters/`
- `tests/zephyr/parameters_availability/`
- `tests/zephyr/parameters_compile_fail/`
- `tests/zephyr/parameters_disabled/`
- `tests/zephyr/parameters_disabled_compile_fail/`
- `tests/zephyr/parameters_policy/`
- `tests/zephyr/check_parameters_compile_fail.py`
- `tests/zephyr/check_parameters_headers.py`

### Reshaped

- `include/solar/events/contribution.hpp`
- `include/solar/solar.hpp`
- `include/solar/system/blueprint.hpp`
- `include/solar/system/sections.hpp`
- `include/solar/system/system.hpp`
- `zephyr/Kconfig`

## 12. Verification Evidence

Focused Stage 09 evidence:

- main Parameters runtime fixture: 15/15 cases pass;
- persistence-disabled availability fixture: 1/1 configuration and case pass;
- public-header isolation: 14/14 enabled and 14/14 disabled headers pass;
- compile-fail diagnostics: 18/18 contracts pass;
- strict, relaxed, alternate-binding, settings, and disabled variants pass;
- `clang-format --dry-run --Werror` passes for all Stage 09 C++ sources;
- `git diff --check` passes;
- allocation-symbol audit finds no undefined allocation entry points.

Regression checkpoints:

- host CMake/CTest: 47/47 tests pass;
- all Zephyr `native_sim/native/64` suites: 34/34 configurations and 169/169
  cases pass with warnings treated as errors.

No firmware build is required by the Stage 09 gate. Firmware migration remains
a later integration stage after the subsystem sequence is complete.

## 13. Implementation Decisions

### 13.1 Transactional groups default to manual commit

The design permits a group `Commit` policy but does not require one in the
compact declaration. An omitted `Commit` resolves to `Manual<Group::Store>`.
This avoids surprise boot-time or every-write durability while preserving a
short common declaration. Adding an explicit `Immediate` or `Deferred` commit
is local to the group type.

### 13.2 Synchronous adapters own backend operation timeouts

The store contract is deliberately small, static, and synchronous. Solar bounds
waiting for competing deferred work with
`CONFIG_SOLAR_PARAMETERS_PERSISTENCE_TIMEOUT_MS`, but cannot safely cancel a
backend call already executing. Device-specific timeout behavior belongs in the
adapter. Replacing the adapter with a future asynchronous contract is isolated
to `persistence.hpp`, runtime persistence helpers, and store implementations.

### 13.3 Migration does not force an immediate rewrite

A successful migration commits the translated value and records
`MigrationPending`, but does not unconditionally write during boot. This avoids
surprise boot latency and flash wear. Normal save, deferred, or group policy can
rewrite it afterward. Changing this decision is localized to the persistent
load completion path.

### 13.4 Persistence sweeps continue after independent failures

`save_all`, `flush`, and deferred sweeps retain the first typed error but still
attempt later independent dirty entries. This preserves maximum progress and
leaves exact per-slot failure facts. A regression injects a failure for one
stable ID and proves a later dirty parameter is persisted.

### 13.5 Disabled Parameters remains a complete inert architecture

Public subsystem headers define a zero-state architecture and typed disabled
runtime stubs when capability code is excluded. This keeps direct includes and
relaxed prototyping ergonomic without allocating state. Requiring the facility
through a real System remains a compile-time error.

## 14. Reversal Or Replacement Path

- Validation/access/storage policy resolution is concentrated in
  `parameters/policy.hpp` and `parameters/facility.hpp`.
- Canonical slot and persistence state is concentrated in
  `parameters/facility.hpp`; operations are concentrated in
  `parameters/runtime.hpp`.
- Frontend spelling and strict/relaxed dispatch are isolated in
  `parameters/api.hpp` and `parameters/protocol.hpp`.
- Persistence wire format and adapter contracts are isolated in
  `parameters/persistence.hpp`; Zephyr settings is isolated in
  `parameters/settings.hpp`.
- Contribution and blueprint integration are isolated in
  `parameters/contribution.hpp`, `system/sections.hpp`, and
  `system/blueprint.hpp`.
- Deferred execution is one generated Stage 07 declaration and can be moved to
  another target or replaced without changing parameter declarations.

These boundaries allow later Remote, Event, Metric, or inspection adapters to
consume Parameters without becoming part of its canonical state or commit
path.
