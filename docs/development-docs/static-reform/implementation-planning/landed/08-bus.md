# Stage 08: Typed Bus

Status: landed

Landed date: 2026-07-16

Implementation repository/branch: `/workspaces/solar`, `static_reform`

Relevant commits or change identifiers: uncommitted reform working tree

## 1. Objective

Stage 08 permanently replaces the former Channel architecture with a typed,
compile-time application Bus. Messages and routes are part of the effective
System blueprint, while each asynchronous route owns exact bounded storage and
delegates execution to an explicit Stage 07 target.

The landed Bus provides:

- message and subscription catalogs from root and component contributions;
- compact component-local `Messages` and `Subscriptions` aliases;
- central `To` routes and explicit handler/tagged `Route` declarations;
- inline, ISR-inline, queued, latest-value, and coalesced delivery;
- reject, drop-newest, drop-oldest, and bounded-wait overflow behavior;
- drain and cancel-pending stop behavior;
- ordinary, no-wait, and ISR-safe emission;
- strict and relaxed global frontends over one canonical System-owned state;
- focused message/subscription views, route records, and emission errors;
- Kconfig capability, defaults, payload ceilings, catalog ceilings, and route
  capacity ceilings;
- no heap, runtime registration, hidden worker, or Bus-owned global queue.

## 2. Specification Coverage

| Specification | Sections implemented | Notes |
| --- | --- | --- |
| `04-bus.md` | complete Stage 08 Bus contract | Message architecture, route topology, delivery, concurrency, ISR behavior, overflow, shutdown, records, policy precedence, and Kconfig are landed. |
| `01-system-blueprint-and-binding.md` | Bus section, demand-derived facility, application protocol composition | Bus catalogs and state derive from the effective bound System. |
| `02-identity-contributions-and-catalogs.md` | conventional aliases, semantic ownership, provenance, duplicate validation | Component-local subscriptions are owner-bound during generic catalog normalization. |
| `03-lifecycle-kernel-and-configuration.md` | Bus facility lifecycle and containment boundary | Admission opens during lifecycle start and closes before component teardown. |
| `09-tasks-and-executors.md` | generated subsystem work registrations | Every asynchronous route contributes one typed Stage 07 registration to its selected executor. |

Point-to-point service mailboxes, retained history, Remote exposure,
observability side effects, dynamic routes, and runtime payload polymorphism
remain intentionally outside this stage.

## 3. Public Surface Landed

The public aggregate is:

```cpp
#include <solar/bus.hpp>
```

Messages are ordinary value types with Bus descriptors:

```cpp
struct VelocityCommand
{
    float left{};
    float right{};

    static constexpr solar::bus::Descriptor descriptor{
        .name = "drive.velocity-command",
        .version = 1,
    };
};
```

The compact component-local path is:

```cpp
using ControlQueue = solar::execution::WorkQueue<
    "control-work",
    solar::execution::StackSize<2048>,
    solar::execution::Priority<2>>;

struct Planner
{
    static constexpr solar::component::Descriptor descriptor{.name = "planner"};
    using Messages = solar::bus::Messages<VelocityCommand>;
};

struct Drive
{
    static constexpr solar::component::Descriptor descriptor{.name = "drive"};
    using Subscriptions = solar::bus::Subscriptions<
        solar::bus::On<VelocityCommand,
                       solar::bus::delivery::Latest<ControlQueue>>>;

    static solar::Status handle(const VelocityCommand& command);
};
```

Central routes can be authored in the Blueprint:

```cpp
using Robot = solar::System<solar::Blueprint<
    solar::Facilities<Planner, Drive>,
    solar::Executors<ControlQueue>,
    solar::Bus<
        solar::bus::Subscriptions<
            solar::bus::To<EmergencyStop,
                           Safety,
                           solar::bus::delivery::InlineIsr>>>>>;
```

The global API includes:

- `emit(message)` and `emit<Message>()`;
- `try_emit(message)` and `try_emit<Message>()`;
- `try_emit_isr(message)` and `try_emit_isr<Message>()`;
- `messages()` and `message<Message>()`;
- `subscriptions()`;
- `record<Subscriber, Message, RouteTag>()`;
- the same operations through `bus::Of<Application>` for alternate bindings.

Handlers may return `void`, `Status`, or `Result<void>`. Inline fan-out
attempts every matching route and returns a focused aggregate error if one or
more handlers fail.

Delivery declarations are:

```cpp
solar::bus::delivery::Inline
solar::bus::delivery::InlineIsr
solar::bus::delivery::Queued<Executor, Capacity, Overflow, Stop>
solar::bus::delivery::Latest<Executor, Stop>
solar::bus::delivery::Coalesced<Executor, Stop>
```

`Queued<Executor>` uses configured defaults. Explicit C++ route policy takes
precedence over System Bus configuration, which takes precedence over Kconfig.

## 4. Runtime Ownership

| Owner | Storage/resource | Capacity | Synchronization | Lifetime |
| --- | --- | --- | --- | --- |
| generated Bus facility | lifecycle admission state and route tuple | one facility only when the effective topology demands Bus | route-local synchronization; no global hot-path lock | one static typed System slot |
| inline route | coherent route record only | no payload storage | record spinlock and in-flight atomic | System lifetime |
| queued route | exact byte-copy payload ring, indices, free-slot semaphore, record | resolved compile-time capacity | route spinlock, semaphore, atomics | System lifetime |
| latest route | one exact payload slot and pending/in-flight state | exactly one payload | route spinlock and atomics | System lifetime |
| coalesced route | pending signal and record | one logical signal, no payload | atomics and record spinlock | System lifetime |
| generated route execution | exact Stage 07 native-coalescing registration | one registration per asynchronous route | Stage 07 work synchronization | System lifetime |
| relaxed frontend | non-owning operation binding for ordinary/try emission | one per application/message/operation | atomic binding | program lifetime |

Asynchronous payloads must be trivially copyable and fit configured size and
alignment ceilings. Storage is exact and static; no route stores a reference to
producer-owned data.

The representative test topology has 14 messages, 17 routes, 10 asynchronous
routes, 11 total execution registrations, and exactly 188 bytes of Bus payload
storage. Its complete test ELF measured 226,061 B text, 8,157 B data, and
28,943 B BSS. The image includes Ztest, the explicit 2 KiB control workqueue,
all Bus fixtures, records, and prior Solar foundations.

Bus allocates no heap memory and owns no thread or stack. Named workqueues are
explicit application components; the system workqueue remains Zephyr-owned.

## 5. Compile-Time Behavior

The effective Bus topology combines:

- root `solar::Bus<...>` message and subscription sections;
- component-local `using Messages = bus::Messages<...>`;
- component-local `using Subscriptions = bus::Subscriptions<...>`;
- central `bus::To<...>` routes;
- explicit tagged `bus::Route<...>` declarations.

Normalization binds `On` and component-local `Route` declarations to their
semantic owner, retains contribution origin, rejects duplicate logical routes,
and derives subscriber and executor dependencies. A demanded Bus topology
selects one generated built-in facility. Every asynchronous route contributes
one generated Stage 07 registration, so no second scheduler exists.

Strict mode rejects unregistered message emission and route queries at compile
time. Relaxed mode preserves the same successful spelling and canonical state,
but returns `Reason::NotRegistered` for an unregistered operation. Both modes
are exercised by the same runtime fixture.

Stable Bus compile-fail contracts cover:

- unregistered messages and strict operations;
- duplicate logical subscriptions;
- invalid handler signatures;
- nontrivial, oversized, or over-aligned asynchronous payloads;
- zero or excessive route capacity;
- payload-bearing coalesced delivery;
- absent or invalid executors;
- required subscribers;
- incompatible ISR topology;
- message catalog ceilings;
- declaring a Bus topology while the capability is disabled.

## 6. Error And Availability Behavior

Emission returns `Result<void, bus::Error>`. The error includes operation,
message and route identity where available, attempted/accepted/rejected/dropped
route counts, Solar status, reason, and native error.

Implemented runtime reasons distinguish:

- `NotReady` before Bus start and after stop;
- `Disabled` when the optional capability is excluded;
- `NotRegistered` in relaxed mode;
- `InvalidContext` for an operation used from the wrong context;
- `RouteRejected` when bounded admission rejects;
- `RouteTimedOut` when bounded wait expires;
- `ExecutorUnavailable` when route work cannot be submitted;
- `InlineHandlerFailed` after complete inline fan-out;
- `InternalInvariant` for impossible internal state.

Route records retain considered, accepted, delivered, replacement, coalescing,
drop, rejection, timeout, handler failure, executor-unavailable, cancellation,
pending, high-water, in-flight, timing, admission, drain, and quiescence facts.
First and latest handler failure statuses are retained without requiring one
broad System snapshot.

Zero-subscriber emission is successful unless `RequireSubscriber<Message>` is
configured. A failed inline handler does not starve later routes. Deferred
handler failure is recorded asynchronously and does not retroactively change a
successful admission result.

## 7. Zephyr Integration

The Bus uses:

- Stage 04/05 spinlock and semaphore wrappers over Zephyr kernel primitives;
- Stage 07 native-coalescing work registrations for asynchronous routes;
- explicit application `WorkQueue` executors or Zephyr's system workqueue;
- `k_is_in_isr` through the Kernel context API;
- monotonic Zephyr ticks for route timestamps;
- `irq_offload` in tests for real ISR-context emission.

`try_emit_isr` is always non-waiting. Every route for that message must be
ISR-compatible: `InlineIsr`, non-waiting queued delivery, latest, or coalesced.
Ordinary `Inline` and `WaitFor` routes make the complete ISR topology invalid.

The Bus Kconfig surface is:

- `CONFIG_SOLAR_BUS`;
- `CONFIG_SOLAR_BUS_MAX_MESSAGES`;
- `CONFIG_SOLAR_BUS_MAX_SUBSCRIPTIONS`;
- `CONFIG_SOLAR_BUS_MAX_ASYNC_PAYLOAD_BYTES`;
- `CONFIG_SOLAR_BUS_MAX_ASYNC_PAYLOAD_ALIGNMENT`;
- `CONFIG_SOLAR_BUS_MAX_ROUTE_CAPACITY`;
- `CONFIG_SOLAR_BUS_DEFAULT_QUEUE_CAPACITY`;
- `CONFIG_SOLAR_BUS_DEFAULT_OVERFLOW_*`;
- `CONFIG_SOLAR_BUS_DEFAULT_STOP_*`;
- `CONFIG_SOLAR_BUS_STOP_TIMEOUT_MS`.

Bus depends on Stage 07 Execution in Kconfig. When disabled, the aggregate
header remains directly includable, runtime calls report `Disabled`, and an
effective topology that requires Bus fails System validation.

## 8. Files Changed

### Added

- `include/solar/bus.hpp`
- `include/solar/bus/api.hpp`
- `include/solar/bus/catalog.hpp`
- `include/solar/bus/contribution.hpp`
- `include/solar/bus/delivery.hpp`
- `include/solar/bus/facility.hpp`
- `include/solar/bus/policy.hpp`
- `include/solar/bus/protocol.hpp`
- `include/solar/bus/runtime.hpp`
- `include/solar/bus/subscription.hpp`
- `include/solar/bus/types.hpp`
- `tests/zephyr/bus/`
- `tests/zephyr/bus_availability/`
- `tests/zephyr/bus_compile_fail/`
- `tests/zephyr/bus_disabled/`
- `tests/zephyr/bus_disabled_compile_fail/`
- `tests/zephyr/bus_shutdown/`
- `tests/zephyr/check_bus_headers.py`

### Reshaped

- `include/solar/catalog/catalog.hpp`
- `include/solar/catalog/contribution.hpp`
- `include/solar/lifecycle/protocol.hpp`
- `include/solar/execution/protocol.hpp`
- `include/solar/system/blueprint.hpp`
- `include/solar/system/system.hpp`
- `include/solar/solar.hpp`
- `zephyr/Kconfig`

### Removed

All former Channel headers and service-channel architecture remain removed.
No Channel alias, adapter, or parallel compatibility runtime was introduced.

## 9. Tests And Evidence

| Command | Platform/configuration | Result | What it proves |
| --- | --- | --- | --- |
| `cmake --build build/host -j 2 && ctest --test-dir build/host --output-on-failure` | host C++23 | 47/47 pass | all Stage 00-03 host and compile-fail regressions remain green |
| `west twister -T tests/zephyr -p native_sim/native/64 --inline-logs --outdir build/twister-stage08-final-clean --clobber-output --warnings-as-errors -j 1` | Zephyr 4.4, all Stage 00-08 configurations | 25/25 configurations and 118/118 cases pass, no warnings | complete post-fix native regression matrix |
| `./build/stage08-bus/zephyr/zephyr.exe` | representative relaxed Bus image | 13/13 pass | normalization, fan-out, overflow, concurrent producers, ISR, reentrancy, latest/coalesced lost-wakeup behavior, and handler errors |
| strict Bus Twister variant | `native_sim/native/64` | 13/13 pass | strict and relaxed API/state equivalence |
| availability fixture | strict and relaxed | 2/2 configurations pass | preboot/running/post-stop state |
| disabled fixture | `CONFIG_SOLAR_BUS=n` | pass | direct include and explicit disabled runtime behavior |
| shutdown fixture | named queue with pending work | pass | drain delivers accepted work and cancel-pending discards it coherently |
| 15 selected `bus_compile_fail` Zephyr builds with token matching | strict mode and reduced ceilings | 15/15 expected failures observed | topology, policy, payload, target, ISR, and binding diagnostics |
| disabled-topology expected-failure build | `CONFIG_SOLAR_BUS=n` | expected disabled-built-in diagnostic observed | a disabled required Bus cannot silently enter the effective System |
| `python3 tests/zephyr/check_bus_headers.py --compile-commands build/stage08-bus/compile_commands.json --include-root include` | real Zephyr C++23 command plus `-Werror` | 11/11 headers pass | public header isolation |
| `clang-format --dry-run --Werror ...` and `git diff --check` | Bus sources and working tree | pass | formatting and whitespace closure |
| `size build/stage08-bus/zephyr/zephyr.elf` | representative Bus test image | text 226,061 B; data 8,157 B; BSS 28,943 B; total 263,161 B | full bounded test-image baseline |
| undefined-symbol audit with `nm -u -C` | representative Bus ELF | no allocation symbols | no unresolved `malloc`, `calloc`, `realloc`, `operator new`, or `operator delete` dependency |
| focused source search | Solar include/source/tests/Kconfig/CMake | no Channel references | hard Channel removal remains complete |

The first full Twister run exposed one disabled-build include leak: the minimal
Bus facility declaration still included the full Execution workqueue policy.
The dependency was guarded by `CONFIG_SOLAR_BUS`, the focused disabled case
passed, and the complete 25-configuration matrix was rerun cleanly.

No firmware build is required at the Stage 08 roadmap gate.

## 10. Specification Refinements

None. The implementation required private representational choices but no
accepted Bus semantics, ownership boundary, dependency direction, or public
API was amended.

## 11. Firmware And Host Impact

Firmware migration remains deferred to the roadmap integration gate. The host
foundation continues to build because Bus exposes only minimal architecture
types outside its Zephyr-enabled runtime branch.

Solar source no longer contains Channel declarations. Application migration
will use typed messages and subscriptions directly when firmware is moved onto
the integrated architecture.

## 12. Known Limits And Deferred Work

Accepted limits are:

- payloads are values and asynchronous payloads must be trivially copyable;
- no retained message history;
- no dynamic route or runtime registration;
- no point-to-point service mailbox;
- no Bus-owned thread or executor;
- no automatic observability, logging, metrics, or Remote export side effects;
- no arbitrary payload allocator or variable-length runtime envelope;
- no in-process reboot reconstruction.

These are later subsystem or optional extension boundaries, not incomplete
Stage 08 requirements.

## 13. Local Implementation Decisions

### Local decision: generic contribution owner binding

Problem: compact `using Subscriptions = bus::Subscriptions<On<...>>` does not
repeat the subscriber type, but catalog normalization must preserve it.

Constraints: ordinary component headers cannot include the root System; the
generic catalog layer must remain subsystem-extensible.

Options considered: require `To` everywhere; add Bus-only preprocessing in the
Blueprint; add a generic contributed-declaration customization point.

Decision: contribution normalization invokes `contributed_declaration<Tag,
Component, Declaration>` before catalog validation.

Why: the common Bus syntax stays compact and the catalog layer gains a reusable
owner-binding hook without learning Bus semantics.

Physical implementation: `catalog/contribution.hpp`,
`bus/subscription.hpp`, `bus/contribution.hpp`, and Bus normalization tests.

Tests/evidence: component and root routes normalize into one catalog with the
expected semantic owners and origins.

Reversal path: a future subsystem-specific pre-normalization pass can replace
the customization while preserving public declarations and catalog entries.

### Local decision: generated facility and generated execution registrations

Problem: Bus needs lifecycle-owned route state and asynchronous execution, but
neither should be authored manually by every application.

Constraints: no hidden default worker, no runtime registry, and one canonical
dependency graph.

Options considered: make every route a component; own a Bus worker; generate
one facility and one Stage 07 registration per asynchronous route.

Decision: effective topology demand selects one typed Bus facility, and its
asynchronous routes contribute generated native-coalescing registrations.

Why: route state remains one facility concern while execution ownership stays
with explicit Stage 07 targets.

Physical implementation: `bus/facility.hpp`, `bus/runtime.hpp`,
`bus/contribution.hpp`, `system/blueprint.hpp`, and `execution/protocol.hpp`.

Tests/evidence: catalog size assertions, executor dependency rejection,
shutdown containment, no-hidden-thread evidence, and full execution regressions.

Reversal path: generated registrations can adopt another accepted Stage 07
registration form without changing messages, subscriptions, or route storage.

### Local decision: direct call-site ISR binding

Problem: eagerly binding every ISR operation in relaxed mode instantiated ISR
topology validation for messages that were never emitted from ISR.

Constraints: an ordinary `Inline` route must not reject an otherwise valid
System; `try_emit_isr<Message>` must still reject an incompatible complete
fan-out topology at compile time.

Options considered: eagerly bind ISR slots; weaken ISR validation to runtime;
resolve the bound System directly at the ISR call site.

Decision: ordinary and try emission use shared strict/relaxed operation slots;
`try_emit_isr` resolves and validates its bound System at the call site.

Why: only intentional ISR use pays the compile-time topology requirement, and
runtime ISR behavior remains non-waiting and statically safe.

Physical implementation: `bus/api.hpp` and `bus/protocol.hpp`.

Tests/evidence: ordinary Inline systems compile, compatible real-ISR emission
passes, and incompatible ISR topology fails with the intended token.

Reversal path: a lazy per-message ISR binding protocol could replace direct
resolution if later frontend machinery can defer instantiation equivalently.

### Local decision: byte-copy route-local payload storage

Problem: producers and route workers run in different contexts, so retaining a
reference or pointer to producer data is unsafe.

Constraints: exact static capacity, no heap, bounded lock time, and no default
construction requirement for queued payload slots.

Options considered: typed object arrays; pointer/reference queues; exact byte
arrays with bit-cast copies.

Decision: asynchronous routes own aligned exact byte arrays and copy trivially
copyable message values with `std::bit_cast`.

Why: payload lifetime is unambiguous, storage is compile-time measurable, and
the route neither allocates nor depends on producer lifetime.

Physical implementation: `bus/facility.hpp` and payload compile-fail fixtures.

Tests/evidence: size/alignment/nontrivial diagnostics, concurrent producers,
latest replacement, and the 188-byte representative storage assertion.

Reversal path: a typed inline-storage wrapper can replace the private byte
representation while retaining the same payload constraints and public API.

### Local decision: explicit default-capacity sentinel

Problem: using zero to mean “take the configured default” made an explicitly
authored zero-capacity queue indistinguishable from omission.

Constraints: `Queued<Executor>` must remain compact and `Queued<Executor, 0>`
must be rejected.

Options considered: retain the ambiguous zero; use a policy type in the second
template position; reserve an internal maximum-value sentinel.

Decision: omitted capacity uses `bus::default_capacity`, an internal
`size_t(-1)` sentinel, while explicit zero remains zero and fails validation.

Why: the common spelling remains unchanged and policy intent is preserved.

Physical implementation: `bus/delivery.hpp`, `bus/policy.hpp`, and capacity
compile-fail/precedence tests.

Tests/evidence: explicit zero and excessive capacities fail; Kconfig, System
configuration, and explicit route capacities resolve in order.

Reversal path: a future named `UseDefaultCapacity` template parameter can
replace the sentinel without changing resolved delivery behavior.

### Local decision: route-local lock partitioning

Problem: multiple thread and ISR producers can contend while records must
remain coherent and bounded queues need slot accounting.

Constraints: no global Bus lock, no waiting in ISR, and correct multi-field
records.

Options considered: one facility lock; lock-free per-delivery algorithms;
route-local spinlocks plus a queued-route semaphore.

Decision: each route owns its admission/storage spinlock and coherent record
lock; queued routes additionally own a free-slot semaphore for bounded wait.

Why: unrelated routes do not contend, ISR paths remain non-waiting, and
`WaitFor` uses the Zephyr primitive intended for bounded thread waiting.

Physical implementation: `bus/facility.hpp` and Kernel spinlock/semaphore
wrappers.

Tests/evidence: two concurrent producers fill one 32-slot route without loss,
real ISR fan-out succeeds, all overflow modes pass, and records retain a
coherent high-water mark.

Reversal path: individual route implementations can adopt measured lock-free
algorithms without changing route declarations, records, or facility ownership.

### Local decision: disabled feature dependency isolation

Problem: the first full matrix showed the disabled Solar build reaching an
Execution Kconfig constant through an unconditional Bus facility include.

Constraints: `solar/solar.hpp` and System architecture must compile when Solar
or Bus is disabled; enabled Bus still needs the full Zephyr runtime.

Options considered: define fallback Execution constants; include the full
runtime unconditionally; guard full Bus runtime dependencies by capability.

Decision: only the minimal Bus architecture declaration is visible outside
`__ZEPHYR__ && CONFIG_SOLAR_BUS`; Execution and Kernel runtime headers are
included only inside that capability boundary.

Why: Kconfig remains the sole source of feature configuration and disabled
features have no accidental dependency footprint.

Physical implementation: `bus/facility.hpp`.

Tests/evidence: the focused disabled-foundation rerun and complete clean
25-configuration Twister matrix both pass with warnings-as-errors.

Reversal path: splitting minimal architecture and runtime facility declarations
into separate private headers would preserve the same boundary if preferred.

## 14. Documentation Handoff

The public documentation pass should explain:

- how to declare value messages and compact component contributions;
- when to use local `On`, central `To`, or explicit tagged `Route`;
- inline versus queued versus latest versus coalesced semantics;
- choosing explicit application workqueues and the system workqueue;
- overflow, stop, and Kconfig/C++ policy precedence;
- payload lifetime, trivial-copy, size, and alignment requirements;
- ordinary, no-wait, and ISR emission constraints;
- strict versus relaxed unregistered behavior;
- zero-subscriber behavior and `RequireSubscriber`;
- asynchronous handler failure and focused route records;
- lifecycle admission, drain, cancel, and executor containment.

`tests/zephyr/bus/src/main.cpp` and the focused shutdown/availability fixtures
are the primary executable documentation seeds.

## 15. Closure Statement

Stage 08 is complete. Solar now has one bounded, typed, Zephyr-native
application Bus with compile-time topology, exact route-owned storage,
explicit Execution integration, strict/relaxed frontend parity, focused
records, stable diagnostics, and no Channel compatibility architecture.

The exact-tree host and Zephyr regressions are green. Stage 09 Parameters is
unblocked and can reuse the same catalog, System state, application protocol,
Execution target, and lifecycle foundations without depending on Bus.
