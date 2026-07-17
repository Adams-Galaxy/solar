# Stage 10: Observability Events

Status: landed

Landed date: 2026-07-16

Implementation repository/branch: `/workspaces/solar`, `static_reform`

Relevant commits or change identifiers: uncommitted reform working tree

## 1. Objective

Stage 10 lands Solar's typed observability event subsystem. Events are bounded,
structured records of facts that have already happened. They are deliberately
separate from Bus messages, application callbacks, logging text, public
metrics, and Remote transport.

The landed subsystem provides:

- application and component-local event declarations and processor routes;
- typed payload-free and copied-value observations;
- descriptors, severity, domain, source, timestamp, context, correlation, and
  log intent;
- ordinary, bounded no-wait, and ISR observation paths;
- every-occurrence, sampling, rate-limit, and bounded aggregation capture;
- transient, buffered, reserved critical, and persistent retention;
- separate fixed thread and ISR ingress with sequence-ordered processing;
- compact variable-sized ordinary history and protected critical history;
- source-scoped condition and explicit recovery state;
- fixed per-event, processor, and facility accounting;
- component-owned infrastructure processors on shared Solar execution;
- strict and relaxed global frontends over one canonical System-owned state;
- Kconfig capabilities, policy defaults, and hard capacity ceilings;
- no heap, dynamic registration, Event-owned thread, or Event-owned workqueue.

## 2. Specification Coverage

| Specification | Sections implemented | Notes |
| --- | --- | --- |
| `06-events.md` | complete Stage 10 event contract | Identity, capture, ingress, retention, history, processors, recovery, records, lifecycle, and focused queries are landed. |
| `01-system-blueprint-and-binding.md` | Event sections, demand-derived facility, bound frontend | Events use the effective bound System and provide `Of<Application>` for alternate bindings. |
| `02-identity-contributions-and-catalogs.md` | `Events` and `EventProcessors` aliases, owner/origin preservation | Processor declarations are rebound to their contributing component during normalization. |
| `03-lifecycle-kernel-and-configuration.md` | facility lifecycle, generated dependencies, Kconfig precedence | Events starts before dependent components and performs bounded final processing before deinit. |
| `09-tasks-and-executors.md` | one generated on-demand processing registration | Event processing uses Stage 07 execution, normally through Zephyr's system workqueue. |

Metric, Log, persistence-envelope, and Remote integrations remain explicit
adapters. Stage 10 lands the generic processor route those later facilities use
without introducing placeholder implementations or circular dependencies.

## 3. Public Surface Landed

The public aggregate is:

```cpp
#include <solar/events.hpp>
```

The common declaration path is compact:

```cpp
struct FrameDropped
{
    struct Payload
    {
        std::uint32_t bytes;
        std::uint8_t link;
    };

    static constexpr solar::events::Descriptor descriptor{
        .severity = solar::events::Severity::Warning,
        .domain = "remote",
        .name = "remote.frame-dropped",
        .description = "An inbound frame was rejected",
        .stable_id = solar::events::Id{0x2001},
        .version = 1,
    };
};
```

An owner contributes event declarations and optional infrastructure processing:

```cpp
struct Diagnostics
{
    static constexpr solar::component::Descriptor descriptor{
        .name = "diagnostics",
    };

    using Events = solar::events::Events<FrameDropped>;
    using EventRole = solar::events::InfrastructureObserver;
    using EventProcessors = solar::events::Processors<
        solar::events::Process<FrameDropped, Diagnostics>>;

    static solar::Status process(solar::events::RecordView record);
};
```

Application-owned declarations and subsystem policy use separate Blueprint
sections:

```cpp
using Robot = solar::System<solar::Blueprint<
    solar::Facilities<Diagnostics>,
    solar::Events<BootWarning>,
    solar::events::Configuration<
        solar::events::DefaultRetention<solar::events::retention::Buffered>,
        solar::events::ProcessorExecutor<EventQueue>>>>;
```

The normal global API includes:

- `observe<Event>(payload)` and the payload-free overload;
- `try_observe<Event>()` for a single bounded admission attempt;
- `try_observe_isr<Event>()` for ISR-only no-wait capture;
- `observe_from<Source, Event>()`, with matching try and ISR variants;
- `record<Event>()`, `condition<Event>(source)`, `processor_record<...>()`, and
  `facility_record()`;
- `descriptors()` and `descriptor<Event>()`;
- `history::read(...)`, typed history filtering, and `history::latest<Event>()`;
- `decode<Event>(record)`, validating bound event identity, schema version,
  and payload shape without requiring composition-root visibility;
- equivalent operations through `events::Of<Application>`.

Observation returns a typed receipt describing capture, sampling, rate
limiting, aggregation, sequence, timestamp, and occurrence count. Failure
returns a focused `events::Error` with operation, event, source, reason, and
native error facts.

## 4. Capture And Retention Policy

Capture policies are:

- `capture::EveryOccurrence`;
- `capture::SampleEvery<N>`;
- `capture::RateLimited<Interval>`;
- `capture::AggregateCount<Window, KeyExtractor, Capacity>`.

Keyed aggregation owns a fixed table only for declarations that request it.
The extractor declares a trivial equality-comparable `Value` and a static
`get(payload)` function. A full key table returns
`Reason::AggregationKeysFull` and updates known-loss accounting.

Retention policies are:

- `retention::Transient` for processor delivery without retained history;
- `retention::Buffered` for ordinary compact history;
- `retention::Critical<Slots, Exhaustion>` for protected ingress and history;
- `retention::Persistent<Store>` for explicit synchronous store adapters.

Critical capacity is reserved end to end: queued, in-flight, and retained
records all consume the event's reservation. Ordinary ingress and history
cannot consume those slots. Default exhaustion latches and rejects; the
explicit panic policy crosses the Stage 05 fatal boundary.

Persistent stores implement `initialize()` and `write(RecordView)`. Every
unique store initializes once even when multiple event types use it. Stable
identity is required. Durable framing, integrity, codec evolution, batching,
and retry policy remain adapter concerns until a later persistence contract is
accepted.

Policy precedence follows the shared rule:

```text
event declaration policy
    > typed Events configuration
    > Kconfig default
```

## 5. Runtime Ownership

| Owner | Storage/resource | Capacity | Synchronization | Lifetime |
| --- | --- | --- | --- | --- |
| generated Events facility | readiness, admission, global sequence, processor scheduling, facility record | one only when demanded | atomics and a Zephyr spinlock with try acquisition | one static typed System slot |
| thread ingress | complete copied records | `CONFIG_SOLAR_EVENTS_INGRESS_DEPTH` | facility gate | System lifetime |
| ISR ingress | complete copied records | `CONFIG_SOLAR_EVENTS_ISR_INGRESS_DEPTH` | one no-wait gate attempt | System lifetime |
| event state | policy counters, aggregate state, condition table, event record | exact per effective event | facility gate | System lifetime |
| critical event state | protected ingress, in-flight count, and history | declaration reservation | facility gate | System lifetime |
| ordinary history | compact header plus actual payload bytes | `CONFIG_SOLAR_EVENTS_HISTORY_BYTES` | facility gate | System lifetime |
| processor route | offered/accepted/failed record | one per effective route | facility gate | System lifetime |
| processor registration | one native-coalescing on-demand work registration | one for the complete facility | Stage 07 work synchronization | System lifetime |
| relaxed frontend | non-owning operation bindings | one per application/event/mode | atomic binding | program lifetime |

Events owns no heap, thread, stack, timer, poll object, or workqueue. The normal
processor target is Zephyr's existing system workqueue. An explicit executor is
an application component and a generated lifecycle dependency.

The representative 16-event stress image measured 186,711 B text, 9,853 B
data, and 32,269 B BSS. It includes Ztest, all prior Solar foundations, four
producer stacks, persistent and failing processor fixtures, retained history,
and all capture-policy states. The linked ELF has no undefined `malloc`,
`calloc`, `realloc`, global `new`, or global `delete` symbols.

## 6. Concurrency, Ordering, And History

Every material observation receives one monotonically increasing boot-local
sequence while admitted. Thread and ISR ingress are separate FIFO rings. The
processor compares ring heads, including critical ingress, and drains the next
lowest sequence so deferred processing preserves material occurrence order.

Ordinary observation uses Zephyr's native spinlock, which preserves the local
CPU interrupt/preemption rules while held. `try_observe` and
`try_observe_isr` use `k_spin_trylock` and return `WouldBlock` on actual SMP
contention without waiting. On a uniprocessor, interruption cannot observe the
lock held, so a free try operation succeeds immediately. ISR capture never
invokes a processor, store, formatter, metric, logger, or Remote link inline.
No external processor or store executes while the event gate is held.

Ordinary retained history uses a bounded compact byte store containing a small
length prefix, the record header, and only the actual payload bytes. It evicts
oldest complete records. The current implementation compacts remaining bytes
after eviction rather than maintaining wrapped segments; this preserves the
accepted capacity and complete-record semantics with a simpler bounded state
machine. A wrapped implementation can replace it behind the same API if later
timing evidence justifies that change.

Critical history remains separately protected. Public history queries merge
ordinary and critical records by sequence, copy into caller-owned storage, and
report stale cursors and eviction distance. They never return a view into
mutable retained backing storage.

## 7. Processors, Conditions, And Recovery

`Process<Event, Observer, RouteTag>` is infrastructure, not application event
subscription. The contributing component must explicitly declare
`InfrastructureObserver` and a compatible static `process(RecordView)` method.
The observer owns the processor route semantically and appears in the normal
component graph.

Retention is attempted before processors. Every matching route is attempted
even if retention or an earlier route fails. Focused records retain the first
failure and per-route outcomes. A processor callback is a bounded admission
hook: a slow sink must enqueue into its own fixed-capacity executor or storage
and return. The core processor intentionally does not create hidden per-sink
queues.

Condition state is scoped by event and source. A normal occurrence marks that
source active and increments its consecutive count. An event declaring
`using Resolves = FaultEvent` clears only the matching source's fault state;
faults from other sources remain active. Recovery records carry an explicit
recovery flag. No condition or recovery transition invokes application domain
behavior.

## 8. Error And Availability Behavior

Focused runtime reasons cover:

- subsystem not ready or disabled;
- unregistered event, source, processor route, or condition;
- ordinary/critical ingress exhaustion;
- internal gate contention;
- keyed aggregation capacity exhaustion;
- invalid ISR policy or context;
- processing, storage, scheduling, and shutdown failures;
- empty history, stale cursors, and decode mismatch.

Sampling, rate limiting, and aggregation are successful dispositions, not
errors. Successful admission does not claim every deferred processor or store
succeeds later. Post-capture failures are canonical record facts and never
recursively emit another event.

Before lifecycle activation the bound frontend returns `NotReady`. Disabled
Events returns `Disabled` or `NotSupported`, exposes an empty descriptor span,
and contributes no facility. Relaxed unregistered observations return
`Reason::NotRegistered`; strict mode rejects them at compile time.

## 9. Lifecycle And Shutdown

The demand-derived Events facility initializes unique stores, clears bounded
state, binds exact runtime callbacks, and becomes available before components
that contribute events or processors initialize. Start opens capture and
processor scheduling.

Stop first closes producer admission. `stop::Drain` performs a bounded
synchronous drain and materializes pending aggregates. `stop::CancelPending`
discards ingress and aggregate occurrences while updating known-loss facts.
`CONFIG_SOLAR_EVENTS_STOP_TIMEOUT_MS` is checked between complete records and
aggregate flushes.

Processor and store callbacks are synchronous contracts. Solar cannot preempt
one callback already running in the caller or workqueue context, so an adapter
that can block must own and enforce its operation timeout. Deinit clears all
runtime bindings and static state for deterministic reboot tests.

## 10. Compile-Time Behavior

The effective architecture combines:

- root `solar::Events<...>` declarations;
- component-local `using Events = events::Events<...>`;
- component-local `using EventProcessors = events::Processors<...>`;
- typed `events::Configuration<...>` policy;
- component registration and generated executor dependencies.

Catalog normalization preserves semantic owner and origin, rejects duplicate
event declarations and processor routes, and selects the built-in only when
events, processors, or typed configuration demand it. An empty System pays for
no Events facility or execution registration.

Stable compile-fail contracts cover invalid payload shape, size, alignment,
borrowed direct payloads, keyed aggregation extractors and capacity, persistent
capability/store/identity, critical reservation ceilings, processor ownership
and handler shape, unregistered processors/sources/recovery, ISR-incompatible
capture, strict unregistered operations, event ceilings, missing explicit
executor policy, and disabled required facilities.

C++ cannot inspect pointer, span, or string-view members nested inside an
otherwise trivial user struct before reflection. Solar rejects those borrowed
types when they are the direct payload and keeps the declaration contract that
all nested payload state is owned and bounded. C++26 reflection should make
that contract mechanically enforceable later.

## 11. Kconfig And Zephyr Integration

Kconfig owns:

- Events capability inclusion;
- event and processor catalog ceilings;
- thread and ISR ingress depths;
- maximum copied payload size and alignment;
- ordinary history bytes;
- critical reservation and aggregation-key ceilings;
- persistence capability;
- Zephyr system-workqueue default selection;
- default retention and stop policy;
- final-drain timeout.

There is no fallback configuration header. A proposed
`CONFIG_SOLAR_EVENTS_ACCOUNTING` switch was removed during closure because
accounting is canonical Events state and the implementation did not offer real
storage elision. Keeping an inert switch would have made Kconfig dishonest. A
future optional accounting mode must first split minimal policy state from
queryable accounting and prove actual binary/storage removal.

The implementation uses Zephyr's system workqueue through Stage 07 execution,
kernel uptime for timestamps, ISR-context detection, scheduler behavior, and
Ztest/native simulation for concurrency. Public Events headers include the
Zephyr utility macros they consume and compile independently in enabled and
disabled configurations.

## 12. Files Changed

### Added

- `include/solar/events/api.hpp`
- `include/solar/events/contribution.hpp`
- `include/solar/events/declaration.hpp`
- `include/solar/events/policy.hpp`
- `include/solar/events/processor.hpp`
- `include/solar/events/protocol.hpp`
- `include/solar/events/runtime.hpp`
- `include/solar/events/types.hpp`
- `tests/zephyr/events/`
- `tests/zephyr/events_availability/`
- `tests/zephyr/events_disabled/`
- `tests/zephyr/events_policy/`
- `tests/zephyr/events_compile_fail/`
- `tests/zephyr/events_disabled_compile_fail/`
- `tests/zephyr/check_events_compile_fail.py`
- `tests/zephyr/check_events_headers.py`

### Reshaped

- `include/solar/events.hpp`
- `include/solar/solar.hpp`
- `include/solar/system/sections.hpp`
- `include/solar/system/blueprint.hpp`
- `include/solar/system/system.hpp`
- `zephyr/Kconfig`

### Removed

- the old formatting, filter, record, and sink-shaped Events implementation
  removed during the hard-migration repository reset.

## 13. Tests And Evidence

| Command | Platform/configuration | Result | What it proves |
| --- | --- | --- | --- |
| focused Twister Kernel/Events matrix | `native_sim/native/64`, seven configurations | 7/7 configurations and 23/23 cases passed, warnings as errors | Native spinlock integration plus runtime, relaxed/strict, disabled, and policy variants. |
| direct main Events fixture | `native_sim/native/64` | 8/8 cases passed | Capture policies, history, ISR, critical capacity, persistence, recovery, no-wait pressure, concurrency, ordering, and shutdown. |
| Events compile-fail checker | four generated Kconfig compile databases | 20/20 expected diagnostic contracts passed | Invalid architecture fails for the intended stable token. |
| Events header-isolation checker | Events enabled and disabled | 11/11 public headers passed in each mode | Public headers are self-sufficient and exclusion-safe. |
| host CMake/CTest regression | GNU 13.3, C++23 | 47/47 tests passed | Core, catalogs, System binding, LTO, and prior compile-fail contracts remain green. |
| complete Zephyr Twister regression | `native_sim/native/64`, warnings as errors | 40/40 configurations and 182/182 cases passed with no warnings | Stage 00-10 integration. |
| `clang-format --dry-run --Werror` | all Stage 10 C++ headers and fixtures | passed | Stage-owned C++ formatting is clean. |
| `git diff --check` | complete Solar working tree | passed | No whitespace errors. |
| `size` and undefined-allocation-symbol audit | representative 16-event ELF | 186,711 B text, 9,853 B data, 32,269 B BSS; no allocation symbols | Static resource model and no hidden heap. |

The concurrency fixture uses four producer threads and verifies unique captured
sequences under pressure. It forces deterministic ingress exhaustion, checks
split ISR/thread merge order and source attribution, protects critical
reservation while retained, verifies stale history pages, and confirms final
aggregate materialization during shutdown. The fixture retains an SMP-only
gate-contention branch for later SMP target runs. Typed decode is also tested
against a different event with the same payload size and schema version so
local event identity, rather than shape alone, is required.

## 14. Specification Refinements And Decisions

### Compact history representation

```text
Observed contract:
  Retained history must be compact, bounded, variable-sized, and evict complete
  oldest records.
Evidence:
  A contiguous byte store meets those semantics with less initial state-machine
  risk than wrapped split-record handling.
Accepted change:
  Stage 10 compacts surviving bytes on eviction. The public cursor and capacity
  contract is unchanged.
Physical implementation:
  include/solar/events/facility.hpp, detail::CompactHistory
Reversal path:
  Replace the private storage engine with a wrapped byte ring and retain the
  existing append/next/history API and tests.
```

### Source-scoped condition state

```text
Observed contract:
  Recovery from one source must not clear the same condition for another.
Evidence:
  Device and service faults may share an event type while recovering
  independently.
Accepted change:
  Every event owns a fixed source table sized from the effective component
  graph plus application and built-in source slots.
Physical implementation:
  include/solar/events/facility.hpp and include/solar/events/runtime.hpp
Reversal path:
  Generalize the table key through a declared condition-key policy while
  retaining SourceId as the default extractor.
```

### Processor isolation boundary

```text
Observed contract:
  Slow sinks cannot block producers or require hidden Event-owned workers.
Evidence:
  Stage 07 already provides explicit bounded executors and workqueues.
Accepted change:
  process(RecordView) is a bounded admission hook. An observer needing slower
  work owns its queue/executor and returns admission status.
Physical implementation:
  include/solar/events/processor.hpp and component-owned Process declarations
Reversal path:
  Add a typed queue adapter implemented as an ordinary infrastructure observer;
  no core Events ownership needs to change.
```

### Native no-wait synchronization

```text
Observed contract:
  Thread and ISR capture require a bounded no-wait path without unsafe
  priority inversion.
Evidence:
  A private atomic-flag spin loop did not mask local preemption, so a higher
  priority thread could preempt the holder and spin while preventing release.
Accepted change:
  Stage 05 SpinLock now wraps Zephyr's k_spin_trylock, and Events uses the
  native SpinLock for both ordinary and try capture.
Physical implementation:
  include/solar/kernel/spinlock.hpp, include/solar/events/facility.hpp,
  include/solar/events/runtime.hpp, and focused Kernel/Events tests
Reversal path:
  Replace only the kernel wrapper backend if Zephyr supplies a stronger typed
  primitive later; Events depends on acquire/try_acquire semantics, not native
  lock representation.
```

### Automatic accounting configuration

```text
Observed contract:
  Kconfig switches must remove or change real capability and cost.
Evidence:
  Event policy operation currently depends on some state colocated with focused
  accounting, and an accounting switch did not elide it.
Accepted change:
  Accounting remains canonical whenever Events is compiled; the inert Kconfig
  symbol was removed.
Physical implementation:
  zephyr/Kconfig
Reversal path:
  Introduce specialized minimal event state, verify symbol and storage removal,
  then add an explicit optional accounting capability.
```

## 15. Firmware And Host Impact

Firmware migration remains assigned to Stage 20, after all subsystem APIs are
stable. No compatibility wrapper for the removed Events implementation was
introduced. Host-side Remote, logging, and metrics integrations will consume
the typed descriptors, immutable record views, history copies, and processor
routes in their owning stages.

## 16. Known Limits And Deferred Work

- Nested borrowed members inside an arbitrary trivial payload cannot be
  introspected until reflection; declaration authors must honor the owned,
  bounded payload contract.
- Persistent framing, checksums, schema migration, retry, batching, and backend
  timeouts belong to a later persistent event adapter contract.
- One synchronous processor/store callback already in progress cannot be
  preempted by the final-drain deadline.
- Key-specific condition identity beyond source scoping is a later policy
  extension; keyed aggregation is already supported.
- Direct event-to-Metric, Log, and Remote adapters land with those facilities.
- Hardware-target ISR timing and physical transport behavior remain later
  integration gates; native simulation proves the Stage 10 semantic contract.

## 17. Documentation Handoff

The public documentation pass should explain:

- when to use Events instead of Bus, Logs, or Metrics;
- compact declaration and component contribution patterns;
- source identity, explicit recovery, and condition queries;
- capture versus retention policy and Kconfig precedence;
- ordinary, try, and ISR observation constraints;
- critical reservation and exhaustion consequences;
- processor admission ownership and slow-sink isolation;
- caller-owned history paging and stale cursors;
- post-capture error accounting and shutdown behavior;
- the executable fixtures under `tests/zephyr/events*`.

## 18. Closure Statement

Stage 10 is complete. The implementation has one canonical bounded Events
facility, ordered thread/ISR capture, explicit loss truth, focused immutable
queries, and no hidden execution or allocation. It unblocks Stage 11 Metrics
and the later Logging and Remote adapters without coupling those facilities
into capture.
