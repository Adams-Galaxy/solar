# Stage 11: Metrics

Status: landed

Landed date: 2026-07-16

Implementation repository/branch: `/workspaces/solar`, `static_reform`

Relevant commits or change identifiers: uncommitted reform working tree

## 1. Objective

Stage 11 lands Solar's passive typed Metrics facility: exact catalog-derived
instrument storage, per-metric concurrency, bounded reducers, coherent typed
readings, scalar records, explicit ISR/no-wait paths, and composition-owned
Event adapters. Metrics owns numeric truth but no exporter, transport, worker,
or application behavior.

## 2. Specification Coverage

| Specification | Sections implemented | Notes |
| --- | --- | --- |
| `07-metrics.md` | instruments, declarations, contributions, units, reducers, mutation/read APIs, concurrency, reset, records, lifecycle, configuration, Event adapters | Canonical metric runtime and explicit adapters are landed. Stable external view schemas, storage-free Event accounting exposure, exporters, and Remote exposure remain with their owning later integration stages. |
| `01-system-blueprint-and-binding.md` | root/component catalogs, demand-derived builtin, strict/relaxed frontends | One System-owned facility is included only when the effective catalog or typed configuration demands it. |
| `02-identity-contributions-and-catalogs.md` | compact `using Metrics`, owner/origin preservation, descriptors | Metric local identity is catalog-derived; optional stable metric identity uses the shared catalog contract. |
| `03-lifecycle-kernel-and-configuration.md` | passive lifecycle and Kconfig/C++ precedence | Metrics opens during facility init so dependent init hooks may update it, and closes after dependants stop. |
| `06-events.md` | deferred infrastructure processor adapters | Events remains canonical; Metrics adapters execute only after capture in Event processing context. |

## 3. Public Surface Landed

The public aggregate is:

```cpp
#include <solar/metrics.hpp>
```

Declarations select exact semantics:

```cpp
struct LoopDuration
{
    using Value = std::uint32_t;
    using Instrument = solar::metrics::Distribution<solar::metrics::Summary>;
    using Unit = solar::metrics::units::Microseconds;

    static constexpr solar::metrics::Descriptor descriptor{
        .name = "control.loop.duration",
    };
};

struct Controller
{
    using Metrics = solar::metrics::Metrics<LoopDuration>;
};
```

The normal bound API is compact:

```cpp
solar::metrics::inc<FramesDropped>();
solar::metrics::set<TxDepth>(depth);
solar::metrics::observe<LoopDuration>(elapsed_us);
solar::metrics::record<CycleTime>(elapsed);

auto reading = solar::metrics::get<LoopDuration>();
auto maximum = solar::metrics::get_view<LoopDuration,
                                         solar::metrics::view::Maximum>();
```

Landed instruments and reducers are:

- `Counter`, `Gauge`, `Distribution<Reducer>`, and `Timer<Reducer>`;
- `Last`, `Minimum`, `Maximum`, `Summary`, `WindowMean<N>`,
  `Ema<Numerator, Denominator>`, and `Histogram<Boundaries...>`;
- typed units and ratio-based chrono conversion;
- `Atomic`, `SpinLocked`, `MutexProtected`, and `Automatic` concurrency;
- saturating, rejecting, and wrapping overflow;
- boot-only and declared runtime reset with reset epochs;
- optional timestamps and non-finite numeric rejection;
- `get`, `try_get`, typed scalar views, focused metric/facility records, and
  caller-owned paged scalar record reads;
- move-only scoped timers with explicit finish, cancellation, and destructor
  recording;
- `Of<Application>` alternate-application access.

Event adapters support `Increment`, projected `Add`, `Set`, `Observe`, and
duration `Record`. Projected numeric conversion is checked before mutation.
Aggregate count adapters add the material record's `occurrence_count`.

## 4. Runtime Ownership

| Owner | Storage/resource | Capacity | Synchronization | Lifetime |
| --- | --- | --- | --- | --- |
| generated Metrics facility | lifecycle gate and aggregate accounting | one only when demanded | atomics plus a small record spinlock | System lifetime |
| metric slot | exact scalar or reducer state, epoch, revision, accounting, optional timestamps | one per effective metric | selected per metric | System lifetime |
| atomic scalar slot | scalar plus generation/read metadata | exact declaration value and fixed metadata | lock-free atomics; overlapping writers are not serialized | System lifetime |
| spin/mutex slot | scalar or reducer state and focused record | exact reducer representation | one native Zephyr spinlock or mutex | System lifetime |
| scalar views | no mutable state | derived on read | target slot coherence boundary | call lifetime |
| paged record output | caller-owned span | caller-selected bounded page | sequential coherent per-metric reads | call lifetime |
| Event adapter | Events processor record only | one generated processor route | Events/Execution ownership | System lifetime |
| relaxed frontend | non-owning operation bindings | per application/metric/operation | atomic frontend binding | program lifetime |

Metrics owns no heap, thread, stack, work item, workqueue, timer, poll object,
export queue, sink, or transport. The representative 29-metric fixture owns
4,352 bytes of exact metric slot state and a 48-byte `FacilityRecord`. Its
application archive has no undefined `malloc`, `calloc`, `realloc`, `free`,
global `new`, or global `delete` references.

## 5. Compile-Time Behavior

System normalization collects root and component `Metrics` contributions,
preserves semantic owner/origin, derives one immutable catalog, and includes
the Metrics builtin only when demanded. An enabled but empty Blueprint elides
the facility.

Strict binding validates membership and operation policy at the call site.
Relaxed binding uses the same spelling and result types but reports
`NotReady`, `NotRegistered`, or `Disabled` at runtime. Every public Metrics
header compiles independently in enabled and disabled builds.

Twenty focused compile-fail contracts cover malformed metrics, value size,
counter type, unavailable backends, target lock freedom, reset/timestamp
capabilities, reducer ceilings, timer units, histogram ordering, catalog
ceilings, strict unregistered access, invalid verbs/views/reset, incompatible
ISR policy, and disabled required inclusion.

Policy precedence is verified as:

```text
metric declaration policy
    > typed Metrics configuration
    > Kconfig default
```

## 6. Error And Availability Behavior

Mutations return `Result<Update, metrics::Error>` and typed reads return
`Result<Reading<Metric>, metrics::Error>`. Focused reasons distinguish not
ready, disabled, unregistered, closed, contention, wrong context, overflow,
invalid numeric input, conversion overflow, reset forbidden, unsupported
views, reducer/clock failure, and internal invariant failure.

Counter overflow is policy-defined. Distribution accumulator overflow now
uses the same reject/saturate/wrap contract without partial reducer mutation;
saturated or wrapped reducers latch degraded state. Failed projected Event
conversion updates both the Event processor record and target metric failure
record without mutating the instrument.

Ordinary operations reject ISR context. Explicit `try_*_isr` operations are
available only for statically proven ISR-compatible storage and reducers.
`try_` operations never wait and report `WouldBlock` when a selected backend
or stable atomic read contends.

Paged scalar reads carry an epoch in continuation cursors. A reset between
pages sets `RecordPage::stale` and restarts the changed instrument at its first
view rather than silently combining epochs.

## 7. Zephyr Integration

Metrics uses Zephyr-native mutexes, spinlocks, interrupt-context detection,
monotonic uptime ticks, and C++23 lock-free atomics. No replacement scheduler
or allocator is introduced.

The atomic scalar backend uses a writer-count plus generation protocol.
Writers, including ISR writers, remain overlapping and lock-free. Readers
accept only a generation observed with no active writer; ordinary reads wait
outside ISR context and `try_get` reports contention. This avoids a seqlock
writer gate that could deadlock when an ISR preempts a thread writer.

The benchmark uses native_sim's pseudo-host real-time clock because Zephyr's
cycle counter advances simulated time and reports zero for a CPU-only loop.
Best-of-five results for 200,000 atomic increments were:

- strict: 6,137,791 ns total, 30 ns/op;
- relaxed: 7,498,125 ns total, 37 ns/op.

These are representative host-backed native_sim observations, not target
latency guarantees. Relaxed binding added about 7 ns/op in this run.

## 8. Files Changed

### Added

- `include/solar/metrics/{types,units,reducer,declaration,contribution,storage,runtime,protocol,api}.hpp`
- `include/solar/events/metrics.hpp`
- `tests/zephyr/metrics/`
- `tests/zephyr/metrics_availability/`
- `tests/zephyr/metrics_policy/`
- `tests/zephyr/metrics_disabled/`
- `tests/zephyr/metrics_compile_fail/`
- `tests/zephyr/metrics_disabled_compile_fail/`
- `tests/zephyr/check_metrics_compile_fail.py`
- `tests/zephyr/check_metrics_headers.py`

### Reshaped

- `include/solar/metrics.hpp`
- `include/solar/metrics/{catalog,facility,policy}.hpp`
- `include/solar/events/contribution.hpp`
- `include/solar/system/{sections,blueprint,system}.hpp`
- `include/solar/solar.hpp`
- `zephyr/Kconfig`

### Removed

- the superseded `metrics/group.hpp` and `metrics/value.hpp` object/snapshot
  architecture; Git history remains the archive.

## 9. Tests And Evidence

| Command | Platform/configuration | Result | What it proves |
| --- | --- | --- | --- |
| `cmake -S . -B /tmp/solar-host-stage11 ... && ctest ...` | host GCC 13, C++23 | 47/47 pass | all host and prior compile-fail regressions |
| focused Metrics Twister matrix | native_sim 64, relaxed/strict/disabled/two policy builds | 7/7 configurations, 19/19 cases | instruments, reducers, adapters, lifecycle, policy, concurrency, ISR, stale cursors |
| `check_metrics_compile_fail.py ...` | seven Zephyr compile databases | 20/20 pass | diagnostics fail for the intended contract |
| `check_metrics_headers.py ...` | enabled and disabled Zephyr compile databases | 13/13 headers pass in each mode | public header self-sufficiency |
| `west twister -T tests/zephyr ...` | complete native_sim 64 Solar matrix | 47/47 configurations, 201/201 cases, no warnings | all Stage 00-11 native regressions |
| `size zephyr.elf` | representative relaxed image | text 318,936 B; data 14,285 B; BSS 47,149 B | bounded linked fixture size |
| `size zephyr.elf` | representative strict image | text 302,379 B; data 14,285 B; BSS 47,149 B | strict/relaxed binary comparison |
| `nm -C -u app/libapp.a` allocator filter | representative relaxed image | no matches | no application allocation dependency |
| `git diff --check` | Solar working tree | pass | no whitespace errors |

No firmware build is required at the Stage 11 roadmap checkpoint.

## 10. Implementation Decisions

### 10.1 Automatic Backend Selection

Problem: `Automatic` needs a deterministic target-derived backend without one
global lock.

Constraints: explicit policy must remain inspectable; target lock freedom and
compiled Kconfig capabilities are authoritative.

Options considered: always mutex; always spinlock; value-size-only selection;
target and state-aware selection.

Decision: choose atomic for supported lock-free scalar instruments, spinlock
for small bounded reducer state, and mutex for larger thread-only state.

Physical implementation: `metrics/storage.hpp` and descriptor policy views in
`metrics/api.hpp`.

Tests/evidence: explicit backend concurrency tests, backend-disabled compile
failures, descriptor assertions, and policy fixture.

Reversal path: replace `EffectiveConcurrency` selection while preserving the
four public policies and descriptor result.

### 10.2 Atomic Reading Coherence

Problem: independently atomic scalar and metadata fields could be observed
mid-update.

Constraints: ISR writers must not wait behind a preempted thread writer.

Options considered: sampled metadata; writer-serialized seqlock; packed atomic
state; overlapping writer generation tracking.

Decision: use overlapping writer count plus completed-generation validation.

Physical implementation: `metrics/storage.hpp::AtomicSlot`.

Tests/evidence: four concurrent producers while readers assert
`value == updates`; strict/relaxed stress passes.

Reversal path: a target-proven packed atomic state may replace the private
protocol without changing public APIs.

### 10.3 Epoch-Aware Record Paging

Problem: a cursor could resume inside a reducer after runtime reset and return
views from two epochs without notice.

Decision: cursors carry the active metric epoch; changed metrics restart at
view zero and set an explicit stale flag.

Physical implementation: `metrics/types.hpp` and `metrics/api.hpp`.

Tests/evidence: reset between pages verifies stale reporting and restart.

Reversal path: a future snapshot/export lease can replace the cursor protocol
while retaining explicit stale reporting.

### 10.4 Event Adapter Placement

Problem: Events and Metrics must integrate without either core depending on
the sibling subsystem or creating circular contribution ownership.

Decision: composition-owned `EventMetrics` contributes infrastructure Event
processors from `events/metrics.hpp`; Events core remains generic.

Physical implementation: `events/metrics.hpp` and extension merging in
`events/contribution.hpp`.

Tests/evidence: increment/add/set/observe/timer, aggregate count, narrowing
failure, and processor isolation tests.

Reversal path: generated adapter manifests can produce the same processor
types later without changing Event or Metrics core ownership.

### 10.5 Target-Portable Locked Accounting

Problem: the Stage 12 firmware integration gate found that locked metric slots
and facility accounting still used unconditional 64-bit `std::atomic` values.
The Teensy Cortex-M7 toolchain does not provide those operations lock-free or
ship a `libatomic` fallback.

Decision: preserve 64-bit records while placing facility counters under the
existing record spinlock and locked-slot contention fallback under a dedicated
spinlock. Explicit atomic slots still require target lock freedom.

Physical implementation: `metrics/facility.hpp` and `metrics/storage.hpp`.

Tests/evidence: Metrics relaxed/strict tests remain 14/14 green; native and
Teensy Stage 12 firmware builds link; the application archive has no undefined
`__atomic_*_8` symbols.

Reversal path: target-native lock-free 64-bit accounting can replace the
private locking without changing public metric state or APIs.

## 11. Specification Refinements

None. Implementation details above fit the accepted public and ownership
contracts without changing subsystem direction or API semantics.

## 12. Firmware And Host Impact

No firmware migration is scheduled at this checkpoint. Host-only declarations
remain usable for catalog and composition validation; runtime storage is
Zephyr-backed. Logging, Remote, and later firmware components can now consume
typed readings and focused scalar records without owning metric state.

## 13. Known Limits And Deferred Work

- stable external view IDs and exposure schemas belong to Remote and manifest
  generation;
- storage-free Event accounting exposure is deferred until an exporter or
  Remote consumer defines which canonical Event counters need schemas;
- exporters own scheduling, batching, encoding, and backpressure;
- no cross-metric transaction or coherent all-metric snapshot exists;
- floating counter, dynamic labels, quantiles, persistent history, and runtime
  reducer reconfiguration remain accepted deferred capabilities;
- benchmark values are native host observations, not Teensy timing claims.

## 14. Documentation Handoff

Public documentation should explain instrument selection, compact component
contribution, reducer/view shapes, unit and duration conversion, overflow and
reset epochs, strict versus relaxed binding, per-metric concurrency, explicit
ISR/no-wait forms, paged stale handling, Event adapters, and exporter
ownership. The main Metrics, availability, and policy fixtures are executable
examples.

## 15. Closure Statement

Stage 11 is complete because the typed Metrics facility, canonical storage,
reducers, coherent readings, concurrency and ISR contracts, records, policy
precedence, lifecycle behavior, and Event adapters are implemented; all
focused and complete regression gates pass; resource and timing evidence is
recorded; and no compatibility architecture or hidden runtime owner remains.
Stage 12 Logging is now unblocked.
