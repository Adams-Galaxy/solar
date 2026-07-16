# Typed Metrics

Date: 2026-07-15

Status: accepted design

Owning phase: Phase 7

Depends on:

- `00-design-conventions.md`
- `00a-modern-cpp-result-and-status.md`
- `01-system-blueprint-and-binding.md`
- `02-identity-contributions-and-catalogs.md`
- `03-lifecycle-kernel-and-configuration.md`
- `04-bus.md`
- `05-parameters.md`
- `06-events.md`

## 1. Purpose

This specification defines Solar's typed metrics subsystem.

Metrics provide passive canonical numeric state representing accumulated,
current, or reduced measurements. They retain static identity, semantic
ownership, bounded storage, coherent typed readings, and explicit update
semantics without owning export, delivery, or application behavior.

It establishes:

- counter, gauge, distribution, timer, instrument, measurement, reading, and
  view vocabulary;
- compact metric declarations and component contributions;
- structured instruments with storage-free scalar views;
- stable metadata, ownership, descriptions, units, and identity;
- strict typed `inc`, `add`, `set`, `observe`, `record`, `get`, and timer APIs;
- per-instrument storage, reduction, reset, overflow, and numeric policy;
- coherent typed readings and erased scalar view records;
- per-metric synchronization without one global update lock;
- explicit non-waiting and ISR-safe operations;
- ergonomic domain groups without facility or store template arguments;
- declarative event-to-metric adapters and event accounting views;
- passive lifecycle, exporter, Remote, and inspection boundaries;
- Kconfig capability and C++ policy responsibilities;
- statically derivable resource cost.

The normal update path remains compact:

```cpp
solar::metrics::inc<FramesDropped>();
solar::metrics::set<TxQueueDepth>(depth);
solar::metrics::observe<LoopDuration>(elapsed_us);
```

No metrics facility object, system type, runtime registry, exporter, or context
appears in ordinary application code.

## 2. Non-Goals

Metrics are not:

- application events or commands;
- application bus messages;
- callbacks or change notifications;
- observability event history;
- human-oriented log records;
- runtime parameters;
- arbitrary component state storage;
- an unbounded time-series database;
- a runtime label map;
- a telemetry transport;
- a Remote protocol;
- a periodic exporter;
- a persistence system;
- a general dimensional-analysis library;
- a universal system snapshot.

A metric records numeric truth. Exporters and adapters may read or transform
that truth, but do not become its canonical owner.

## 3. Semantic Boundaries

### 3.1 Metrics and events

An event states that an occurrence happened. A metric stores a current numeric
instrument state.

```cpp
solar::events::observe<FrameDropped>(payload);
solar::metrics::inc<FramesDropped>();
```

Event history can explain individual occurrences. A counter can efficiently
report how many occurred. Explicit adapters may connect the two.

Metrics do not retain event payloads or occurrence sequences.

### 3.2 Metrics and parameters

Parameters are validated mutable configuration. Metrics are observational
state.

A queue capacity may be a parameter. Current queue depth is a gauge. Neither
subsystem owns the other's canonical value.

### 3.3 Metrics and logs

Logs explain operation to humans. Metrics provide typed numeric state. Metric
updates do not automatically format logs, and log records do not automatically
create metrics.

### 3.4 Metrics and component state

Metrics expose selected numeric facts, not arbitrary internal object state.
Domain state needed for behavior remains in its owning component.

### 3.5 Metrics and export

The metrics facility stores and reads instruments. Exporters own scheduling,
serialization, batching, transport, and external backpressure.

## 4. Canonical Vocabulary

The public vocabulary is:

- **metric**: one registered typed numeric instrument declaration;
- **instrument**: the semantic update and storage shape of a metric;
- **counter**: monotonic accumulation within one reset epoch;
- **gauge**: explicitly set current point-in-time value;
- **distribution**: bounded reduction of repeated measurements;
- **timer**: duration-specialized distribution;
- **measurement**: one value supplied to a distribution or timer;
- **reducer**: policy that maps measurements into bounded instrument state;
- **reading**: one coherent typed copy of an instrument's state;
- **view**: one scalar projection of an instrument reading;
- **record**: erased scalar view plus identity and runtime metadata;
- **group**: application-authored facade over related metrics;
- **reset epoch**: interval within which counter monotonicity and revisions are
  interpreted;
- **exporter**: separate infrastructure that reads and publishes metrics.

“Sample” refers to a measurement. It is not the canonical instrument kind in
the reformed API.

## 5. Instrument Model

Solar initially defines four instrument kinds:

```cpp
solar::metrics::Counter
solar::metrics::Gauge
solar::metrics::Distribution<Reducer>
solar::metrics::Timer<Reducer>
```

Each metric declaration selects exactly one instrument kind. That selection
determines:

- valid mutation verbs;
- required numeric value contract;
- canonical storage;
- typed reading shape;
- available scalar views;
- default synchronization;
- ISR eligibility;
- reset and overflow semantics.

Instrument kind is not inferred from which frontend happens to be called.

## 6. Metric Declaration

### 6.1 Counter

```cpp
struct FramesDropped
{
    using Value = std::uint64_t;
    using Instrument = solar::metrics::Counter;
    using Unit = solar::metrics::units::Frames;

    static constexpr solar::metrics::Descriptor descriptor{
        .name = "remote.frames.dropped",
        .description = "Frames rejected before processing",
    };
};
```

### 6.2 Gauge

```cpp
struct TxQueueDepth
{
    using Value = std::uint16_t;
    using Instrument = solar::metrics::Gauge;
    using Unit = solar::metrics::units::Items;

    static constexpr solar::metrics::Descriptor descriptor{
        .name = "remote.tx_queue.depth",
        .description = "Current transmit queue depth",
    };
};
```

### 6.3 Distribution

```cpp
struct LoopDuration
{
    using Value = std::uint32_t;
    using Instrument = solar::metrics::Distribution<
        solar::metrics::Summary>;
    using Unit = solar::metrics::units::Microseconds;

    static constexpr solar::metrics::Descriptor descriptor{
        .name = "control.loop.duration",
        .description = "Control loop execution duration",
    };
};

struct ControlCycleTime
{
    using Value = std::uint32_t;
    using Instrument = solar::metrics::Timer<
        solar::metrics::Summary>;
    using Unit = solar::metrics::units::Microseconds;

    static constexpr solar::metrics::Descriptor descriptor{
        .name = "control.cycle.time",
    };
};
```

### 6.4 Timer

```cpp
struct ControlCycleTime
{
    using Value = std::uint32_t;
    using Instrument = solar::metrics::Timer<
        solar::metrics::Summary>;
    using Unit = solar::metrics::units::Microseconds;

    static constexpr solar::metrics::Descriptor descriptor{
        .name = "control.cycle.time",
    };
};
```

### 6.5 Required declarations

Every metric declares:

- `Value`;
- `Instrument`;
- `Unit`;
- `descriptor`.

Optional policy aliases include concurrency, arithmetic overflow, invalid
numeric input, reset eligibility, timestamping, and view exposure.

## 7. Descriptor And Metadata

The metric descriptor initially supports:

- stable human-readable name;
- optional description;
- optional explicit stable ID under Phase 2 rules;
- optional schema version when externally exposed;
- display metadata that does not alter numeric semantics.

Instrument kind, value type, unit, reducer, concurrency, views, reset, and
overflow remain typed policy. They do not become unstructured descriptor flags.

Descriptor text may be stripped through Kconfig. Identity and schema metadata
required by an enabled external exposure cannot be stripped.

## 8. Metric Contributions

### 8.1 Component alias

Components contribute metrics through the compact conventional alias:

```cpp
struct RemoteService
{
    using Metrics = solar::metrics::Metrics<
        FramesDropped,
        BytesDropped,
        TxQueueDepth>;
};
```

The metrics subsystem owns
`contribution_source<metrics::metric_tag, Component>`. The generic Phase 2
collector preserves semantic owner and registration origin.

### 8.2 Root metrics

Application-owned metrics use the root section:

```cpp
solar::Metrics<SystemUptime, BootCount>
```

### 8.3 Catalog registration

Every typed operation requires effective catalog membership:

```cpp
solar::metrics::inc<UnregisteredMetric>(); // strict error; relaxed NotRegistered
```

No strict or relaxed frontend silently creates an independent static storage
slot for an unregistered type.

### 8.4 Ownership

Contribution ownership means the component defines and provides the metric's
meaning. Other components may update or read it without becoming owners.

Updating another component's metric does not create a graph dependency on that
component. The shared passive facility is the storage dependency.

## 9. Identity

### 9.1 Type identity

The declaration type is local compile-time identity for typed operations.

### 9.2 Local numeric identity

The effective catalog assigns a compact build-local ID for storage indexing,
inspection, and records. It may change when firmware composition changes.

### 9.3 Stable identity

An internal-only metric may use local identity only, as accepted by Phase 2.

A metric or view requires stable explicit or manifest identity when it is:

- exposed through Remote;
- included in a stable host schema;
- persisted externally;
- consumed by tooling across firmware versions;
- referenced by another stable external schema.

Name hashes and compiler type-name hashes are not external identity contracts.

### 9.4 Rename and schema change

Renaming human-readable metadata does not change stable identity. Changing unit,
instrument meaning, view meaning, or numeric schema may require a schema
version or new stable identity for external consumers.

## 10. Value Contract

### 10.1 Common requirements

A metric `Value` must be:

- a complete numeric or explicitly supported boolean type;
- trivially copyable;
- bounded without dynamic allocation;
- compatible with the selected instrument and reducer;
- representable by focused records or declared custom view codecs.

References, pointers, text, containers, arbitrary structs, and dynamically
allocated numeric wrappers are not ordinary metric values.

### 10.2 Counter values

Initial counters use unsigned integral values. This gives a compile-time
non-negative domain and predictable overflow behavior.

Fractional cumulative instruments may use a future explicitly validated
floating counter policy. Until then, fixed-point integers or distributions are
preferred.

### 10.3 Gauge values

Gauges may use supported integral, floating, or boolean values.

### 10.4 Distribution values

Distributions and timers use arithmetic values accepted by their reducer.
Reducers declare accumulator width and intermediate arithmetic requirements.

## 11. Units

### 11.1 Proper type tags

Units are typed semantic descriptors:

```cpp
struct Microseconds
{
    using Dimension = solar::metrics::dimension::Time;
    using Ratio = std::micro;

    static constexpr solar::metrics::UnitDescriptor descriptor{
        .symbol = "us",
        .name = "microseconds",
    };
};
```

A unit provides:

- type identity;
- dimension tag;
- ratio to the dimension's canonical base when conversion is supported;
- stable symbol and name metadata;
- optional stable identity for external schema use.

### 11.2 Built-in units

Solar should provide at least:

- unitless;
- count;
- items;
- bytes and bits;
- frames and packets;
- nanoseconds, microseconds, milliseconds, and seconds;
- hertz;
- percent and ratio;
- volts and amperes;
- degrees and radians.

### 11.3 Conversion scope

Solar supports explicit compile-time ratio conversion between units sharing one
dimension. Timer frontends use this conversion for `std::chrono::duration`.

Ordinary numeric mutation input is interpreted in the declared unit:

```cpp
solar::metrics::set<BatteryVoltage>(12.4f); // volts when declared as volts
```

There is no implicit runtime conversion between unrelated dimensions and no
initial general dimensional algebra for multiplication or division of units.

### 11.4 Unit and identity

Changing a unit may change external schema meaning even when the C++ `Value`
type remains unchanged. External exposure validation therefore includes unit
compatibility.

## 12. Structured Instruments And Views

### 12.1 Decision

Solar uses one canonical storage slot per instrument and permits that instrument
to expose several scalar views.

A summary distribution stores one coherent reducer state rather than several
independent metrics updated manually.

```cpp
solar::metrics::observe<LoopDuration>(elapsed_us);
```

may update count, sum, minimum, maximum, and mean in one operation.

### 12.2 Typed reading

```cpp
auto reading = solar::metrics::get<LoopDuration>();

if (reading)
{
    auto count = reading->count;
    auto maximum = reading->maximum;
    auto mean = reading->mean;
}
```

The reading is one coherent copy produced by the reducer.

### 12.3 Scalar views

Views project scalar values from a reading:

```cpp
auto maximum = solar::metrics::get_view<
    LoopDuration,
    solar::metrics::view::Maximum>();
```

The reducer defines available view tags. Requesting an unsupported view is a
compile-time error.

### 12.4 View storage

Views own no duplicate mutable metric state. They read from the canonical
instrument slot under the same coherence boundary as the complete reading.

### 12.5 View identity

Local view identity is the tuple:

```text
instrument local identity + view type
```

An externally addressable view receives its own stable explicit or manifest ID.
The default display name may append a stable view key such as `.maximum`, but
that generated name is not itself the wire identity.

### 12.6 Why not one stored metric per scalar

Independent scalar metrics would require repeated calls, repeated locks,
duplicated counters, duplicated storage, and could expose internally
inconsistent count/min/max/mean combinations. Structured instruments preserve
one observation and one coherent reduction.

## 13. Counter Semantics

### 13.1 Valid operations

Counters accept only:

```cpp
solar::metrics::inc<FramesDropped>();
solar::metrics::add<BytesDropped>(bytes);
```

`inc` always means adding one. `add` accepts a non-negative value in the
counter's unsigned type.

### 13.2 Invalid operations

The following are compile-time errors:

```cpp
solar::metrics::set<FramesDropped>(4);
solar::metrics::observe<FramesDropped>(4);
solar::metrics::record<FramesDropped>(duration);
```

There is no public arbitrary counter restoration or set operation.

### 13.3 Initial state

Counters initialize to zero and epoch zero during facility initialization.

### 13.4 Zero addition

Adding zero succeeds with `UpdateDisposition::Unchanged`. It does not increment
the metric revision or update timestamp.

### 13.5 Monotonicity

The counter value never decreases within one reset epoch. A reset changes the
epoch so exporters can distinguish administrative reset from invalid backward
movement.

## 14. Gauge Semantics

### 14.1 Valid operation

Gauges accept:

```cpp
solar::metrics::set<TxQueueDepth>(depth);
```

Gauge increment and distribution observation are invalid unless a distinct
future instrument type explicitly supplies those semantics.

### 14.2 Initial state

A gauge begins unset unless the declaration provides:

```cpp
static constexpr Value initial_value = 0;
```

The reading preserves initialized state so an unset sensor does not falsely
appear to report zero.

### 14.3 Equal values

Setting an initialized gauge to the same value still records a successful
refresh:

- update count increments;
- revision increments;
- last-update timestamp refreshes when timestamps are enabled.

Gauge freshness can be operationally meaningful. This deliberately differs
from parameter equal-value suppression.

### 14.4 Gauge is not Last

A gauge owns current state supplied by `set`. A distribution using a `Last`
reducer owns the latest member of a measurement stream and may additionally
track sample count. They remain distinct instruments.

## 15. Distribution Semantics

### 15.1 Valid operation

Distributions accept:

```cpp
solar::metrics::observe<LoopDuration>(elapsed_us);
```

Every accepted measurement updates reducer state atomically with respect to a
typed reading.

### 15.2 Initial reducers

The initial reducer vocabulary should include:

```cpp
solar::metrics::Last
solar::metrics::Minimum
solar::metrics::Maximum
solar::metrics::Summary
solar::metrics::WindowMean<N>
solar::metrics::Ema<Numerator, Denominator>
solar::metrics::Histogram<Boundaries...>
```

### 15.3 Summary

`Summary` stores a coherent bounded state sufficient for:

- count;
- sum;
- minimum;
- maximum;
- mean.

Accumulator type and overflow behavior are declared by the reducer or metric
policy. Mean does not require retaining all measurements.

### 15.4 Window mean

`WindowMean<N>` owns exactly `N` bounded samples or an equivalent exact bounded
representation. `N` is non-zero and cannot exceed the configured hard ceiling.

### 15.5 EMA

`Ema<Numerator, Denominator>` validates a finite ratio in `[0, 1]`. Its reading
includes initialized state and accepted measurement count.

### 15.6 Histogram

Histogram boundaries are compile-time ordered values. Storage consists of exact
fixed buckets plus declared underflow/overflow handling, count, and optional
sum. Runtime labels do not create buckets.

### 15.7 Empty readings

A distribution reading distinguishes empty state. It does not present a
default-constructed zero as an observed minimum, maximum, or mean.

## 16. Timer Semantics

### 16.1 Duration recording

Timers accept compatible `std::chrono::duration` values:

```cpp
solar::metrics::record<ControlCycleTime>(elapsed);
```

The duration is checked and converted to the timer's declared unit before the
reducer is updated.

Raw numeric recording is permitted only when explicitly expressed as the
timer's declared `Value` and unit contract. Duration overloads are preferred.

### 16.2 Scoped timer

```cpp
auto timer = solar::metrics::scoped<ControlCycleTime>();

perform_control_work();

auto result = timer.finish();
```

The scoped timer is move-only. It supports:

- `finish()` to record once and return the update result;
- `cancel()` to suppress recording;
- automatic recording on destruction when still active.

### 16.3 Destructor failure

A destructor cannot return an error. Automatic destructor-time recording
therefore updates focused facility failure state if recording fails. Code that
must react locally calls `finish()` explicitly.

### 16.4 Clock

The default clock is Solar's configured monotonic kernel clock. Timer policy may
select another statically supported monotonic clock. Wall-clock time is not used
for duration measurement.

## 17. Public Mutation API

### 17.1 Common forms

```cpp
solar::metrics::inc<CounterMetric>();
solar::metrics::add<CounterMetric>(amount);
solar::metrics::set<GaugeMetric>(value);
solar::metrics::observe<DistributionMetric>(measurement);
solar::metrics::record<TimerMetric>(duration);
solar::metrics::scoped<TimerMetric>();
```

### 17.2 Return type

Mutating operations return:

```cpp
solar::Result<solar::metrics::Update, solar::metrics::Error>
```

Callers may ignore a result when metric failure does not affect domain
behavior. The failure remains visible in focused records.

### 17.3 Update result

`Update` includes:

- disposition;
- resulting revision;
- reset epoch;
- saturation or degradation flags;
- optional resulting primary scalar where cheap and meaningful.

Initial dispositions include:

- updated;
- unchanged;
- saturated.

An unexpected result guarantees no partial change to that instrument's
canonical state.

### 17.4 No cross-metric transaction

Each update is atomic only for its target instrument. Solar does not initially
provide transactions spanning independent metrics. Metrics are observational
facts, and groups must not imply all-or-none mutation.

## 18. Non-Waiting APIs

Every potentially waiting operation has a non-waiting form where its selected
synchronization can contend:

```cpp
solar::metrics::try_inc<FramesDropped>();
solar::metrics::try_add<BytesDropped>(bytes);
solar::metrics::try_set<TxQueueDepth>(depth);
solar::metrics::try_observe<LoopDuration>(elapsed_us);
solar::metrics::try_record<ControlCycleTime>(elapsed);
solar::metrics::try_get<TxQueueDepth>();
```

`try_` means no waiting. It does not skip registration, numeric, lifecycle, or
instrument-kind validation.

An atomic or bounded spin-locked operation may never report ordinary thread
contention, but retains the same frontend for generic code.

## 19. ISR APIs

ISR use is explicit:

```cpp
solar::metrics::try_inc_isr<UartInterrupts>();
solar::metrics::try_add_isr<EncoderEdges>(edges);
solar::metrics::try_set_isr<LatestCapture>(capture);
solar::metrics::try_observe_isr<EncoderPeriod>(period);
```

An ISR operation is available only when:

- the instrument operation is semantically valid;
- the value and reducer are ISR compatible;
- synchronization is ISR safe;
- update cost has a documented bound;
- no floating helper or clock path violates target ISR constraints.

Mutex-protected reducers are not ISR safe. Large windows and histograms may be
rejected even when their lock primitive is technically callable from ISR.

Solar never silently changes reducer, precision, or storage policy for an ISR
call.

## 20. Numeric Validity

### 20.1 Invalid floating input

Floating gauges and distributions require explicit policy for NaN and infinity.
The safe default rejects non-finite values without changing state.

Optional policy may preserve, clamp, or count invalid input when a domain has a
clear reason.

### 20.2 Conversion

Numeric and unit conversion checks range before mutation. Narrowing overflow or
loss forbidden by policy returns an error and leaves state unchanged.

### 20.3 Reducer arithmetic

Reducers define accumulator types large enough for their intended bounded
operation. They must not silently rely on undefined signed overflow.

### 20.4 Invalid measurement accounting

Rejected measurements update focused failure accounting but do not increment
the reducer's accepted measurement count.

## 21. Counter Overflow

Initial overflow policies are:

```cpp
solar::metrics::overflow::Saturate
solar::metrics::overflow::Reject
solar::metrics::overflow::Wrap
```

The default is `Saturate`.

- Saturate stores the maximum value, sets a saturation latch, and returns a
  successful saturated update.
- Reject leaves the value unchanged and returns an overflow error.
- Wrap uses defined unsigned modular arithmetic and records that wrapping
  occurred.

Wrapping is explicit because external monotonic-counter consumers may otherwise
misinterpret the result.

Accumulator overflow for distributions follows equivalent reducer-owned
policy.

## 22. Reset Semantics

### 22.1 Lifecycle reset

All metric slots reset during Metrics facility initialization.

The reset establishes:

- initial value or empty state;
- epoch zero;
- revision zero;
- cleared overflow and failure latches;
- reducer-specific empty state.

### 22.2 Runtime reset

Runtime reset is available only when the metric declares an effective
`RuntimeResettable` policy and Kconfig includes runtime reset capability:

```cpp
solar::metrics::reset<FramesDropped>();
```

Otherwise typed runtime reset is a compile-time error.

### 22.3 Epoch

Every successful runtime reset increments the metric epoch and resets revision
within that epoch. Exporters and inspection preserve the epoch.

### 22.4 Reset by group or owner

Focused management may reset a statically known group or owner in deterministic
catalog order. Initial reset-all/group behavior is not transactional across
metrics unless a future maintenance gate explicitly provides that guarantee.

### 22.5 Reset side effects

Reset does not automatically emit an event, log, bus message, or Remote update.
Explicit adapters may record operationally important resets.

## 23. Typed Readings

### 23.1 `get`

```cpp
auto reading = solar::metrics::get<Metric>();
auto reading = solar::metrics::try_get<Metric>();
```

The return type is:

```cpp
solar::Result<solar::metrics::Reading<Metric>, solar::metrics::Error>
```

`Reading<Metric>` is instrument-specific.

### 23.2 Common metadata

Every reading includes or exposes:

- reset epoch;
- revision;
- successful update count;
- initialized or non-empty state where meaningful;
- last update timestamp when enabled;
- overflow, saturation, or degradation flags.

### 23.3 Counter reading

A counter reading contains current value and monotonic epoch metadata.

### 23.4 Gauge reading

A gauge reading contains value, initialized state, and freshness metadata.

### 23.5 Distribution reading

A distribution reading is reducer-defined and coherent. A summary reading
contains count, sum, minimum, maximum, and mean.

### 23.6 Timer reading

A timer reading uses the reducer output and preserves declared duration unit.

### 23.7 Copy semantics

Readings are bounded value copies. They do not return references to mutable
canonical reducer storage.

## 24. Scalar View Records

### 24.1 Focused erased representation

Inspection and exporters consume scalar `MetricViewRecord` values equivalent
to:

```cpp
struct MetricViewRecord
{
    MetricId metric;
    ViewId view;
    ComponentId owner;
    InstrumentKind kind;
    UnitId unit;
    ScalarValue value;
    std::uint32_t epoch;
    std::uint64_t revision;
    Timestamp updated_at;
    RecordFlags flags;
};
```

Exact widths are implementation and Kconfig decisions.

### 24.2 Scalar value

The initial erased scalar supports the configured signed integer, unsigned
integer, floating, and boolean representations. Unsupported custom values
require explicit view conversion.

### 24.3 Query surface

Likely focused forms are:

```cpp
solar::metrics::records::metric<LoopDuration>();
solar::metrics::records::view<LoopDuration, metrics::view::Maximum>();
solar::metrics::records::read(cursor, destination);
```

Bulk reads use caller-owned bounded pages and report stale cursors or changed
epochs explicitly.

### 24.4 No vague snapshot

The current broad `Snapshot` value is replaced by typed `Reading<Metric>` and
focused scalar records. The subsystem may internally take coherent snapshots,
but `snapshot()` is not the canonical public vocabulary.

## 25. Runtime Storage

Each registered metric receives one exact static slot conceptually containing:

```cpp
MetricSlot<Metric> {
    reducer or scalar storage;
    synchronization state;
    epoch and revision;
    update and failure accounting;
    optional timestamp;
    overflow and degradation flags;
}
```

Storage is keyed by the effective bound system and metric type. It is not owned
by the declaration type itself and is not allocated through a runtime registry.

Views contain no mutable duplicate storage.

No dynamic allocation is required.

## 26. Synchronization

### 26.1 Per-metric granularity

Every metric slot owns its synchronization. Updating one metric does not lock
or serialize every unrelated metric.

The current implementation's one global critical section around all metric
operations is not part of the target design.

### 26.2 Initial policies

Initial synchronization policies are:

```cpp
solar::metrics::concurrency::Atomic
solar::metrics::concurrency::SpinLocked
solar::metrics::concurrency::MutexProtected
solar::metrics::concurrency::Automatic
```

### 26.3 Automatic selection

`Automatic` chooses the smallest valid policy for the target and instrument:

- lock-free atomic or sequence-protected atomic state for supported simple
  counters and gauges;
- bounded per-metric spinlock for small reducers;
- mutex for larger thread-only reducers.

The choice remains visible in descriptor inspection and static resource
accounting.

### 26.4 Atomic requirements

Explicit atomic policy requires target-proven lock-free operations for the
selected value and metadata scheme. Solar does not assume host lock freedom
implies target lock freedom.

### 26.5 Coherence

A typed reading observes one coherent instrument state. Multi-field reducers
use a lock or valid sequence protocol so count, sum, minimum, maximum, and mean
belong to one completed update boundary.

### 26.6 Cross-metric coherence

No coherence is implied across independent metrics. A group read is a sequence
of per-metric coherent copies unless it explicitly uses a future coordinated
read mechanism.

## 27. Metric Groups

### 27.1 Purpose

A metric group is an application-authored domain facade that packages related
declarations and ergonomic update operations.

```cpp
struct RemoteMetrics
{
    using Metrics = solar::metrics::Metrics<
        FramesDropped,
        BytesDropped,
        TxQueueDepth>;

    static solar::Result<void> frame_dropped(std::uint32_t bytes)
    {
        return solar::metrics::inc<FramesDropped>()
            .and_then([bytes](auto) {
                return solar::metrics::add<BytesDropped>(bytes);
            })
            .transform([](auto) {});
    }

    static solar::Result<void> queue_depth(std::uint16_t depth)
    {
        return solar::metrics::set<TxQueueDepth>(depth)
            .transform([](auto) {});
    }
};
```

### 27.2 Contribution

The component directly reuses the group's list:

```cpp
struct RemoteService
{
    using Metrics = RemoteMetrics::Metrics;
};
```

### 27.3 No store parameter

Group methods call the global bound metric frontends. They do not receive a
facility type, store type, system context, or `Use<System>` parameter.

### 27.4 No separate identity or storage

A group is not itself a metric instrument and owns no metric storage. Its member
metrics retain individual identity and semantic ownership through the
contributing component.

### 27.5 Partial group updates

Several metric updates in one group method are not transactional. If a later
update fails, earlier successful metric truth remains. The returned result and
focused records expose the failure.

## 28. Event-To-Metric Adapters

### 28.1 Composition-level declaration

The preferred adapter declaration lives on the contributing component or
application composition rather than inside reusable event schema:

```cpp
struct RemoteService
{
    using Metrics = RemoteMetrics::Metrics;

    using EventMetrics = solar::events::metrics::Adapters<
        solar::events::metrics::On<
            FrameDropped,
            solar::events::metrics::Increment<FramesDropped>,
            solar::events::metrics::Add<
                BytesDropped,
                &FrameDropped::Payload::bytes>>>;
};
```

This keeps event schema independent from an optional metrics subsystem and
allows product-specific mappings.

### 28.2 Initial adapter operations

Initial declarative operations are:

- increment a counter;
- add a projected payload value to a counter;
- set a gauge from a payload projection;
- observe a distribution from a payload projection;
- record a timer from a compatible duration projection.

Custom transformations must remain statically registered, bounded, and free of
arbitrary application behavior.

### 28.3 Processing stage

The Phase 6 deferred event processor applies metric adapters after canonical
event capture. The event producer never waits for metric update work.

### 28.4 Aggregated events

Adapters declare whether an aggregated event operation uses:

- the material record once;
- `occurrence_count` as the counter increment;
- a reducer-provided aggregate payload field.

Default event-count adapters add `occurrence_count`, not merely one.

### 28.5 Failure

Adapter failure updates the event adapter record and metric failure record. It
does not invalidate the already captured event, retry without bound, emit a
recursive event, or trigger domain behavior.

## 29. Automatic Event Accounting Metrics

Phase 6 automatic event accounting remains canonical state owned by the Events
facility. Metrics does not duplicate every attempted, captured, dropped, and
retained counter by default.

An explicit adapter may expose selected accounting as storage-free read-only
metric views:

```cpp
solar::events::metrics::ExposeAccounting<
    FrameDropped,
    solar::events::metrics::Captured,
    solar::events::metrics::Lost>
```

These views:

- read canonical event counters;
- carry origin and owner metadata;
- allocate no second mutable counter;
- receive stable identity when externally exposed;
- disappear when their adapter or metrics capability is absent.

Product metrics such as `FramesDropped` remain explicit declarations when they
carry product schema, naming, unit, reset, or exposure semantics distinct from
internal event accounting.

## 30. Passive Facility And Exporters

The Metrics facility owns:

- canonical metric slots;
- synchronization;
- typed readings;
- scalar view records;
- reset state;
- update and failure accounting.

It owns no:

- worker thread;
- periodic schedule;
- sink;
- transport;
- formatter;
- Remote session;
- persistent history.

An exporter is a separate service, task, or executor registration that reads
metrics according to its own period and bounded output policy.

One slow exporter cannot block metric producers indefinitely. Exporter failure
does not mutate or reset canonical metric state.

## 31. Lifecycle

### 31.1 Inclusion

The built-in Metrics facility is included only when:

- metrics capability is enabled by Kconfig; and
- the effective system has metric declarations, event metric adapters,
  accounting views, exporters, or explicit metric configuration requiring it.

### 31.2 Implicit dependency

When included, Metrics is an implicit built-in dependency of ordinary
components. Users do not list the built-in facility in their graph.

### 31.3 Initialization

Metrics initializes before ordinary components and resets every effective slot
to its declared boot state. Component init and start hooks may update metrics.

### 31.4 Availability

Metrics remains readable and mutable through:

- dependent component init;
- start;
- running operation;
- boot failure rollback;
- dependent component stop;
- dependent component deinit.

It closes only after dependants can no longer update it.

### 31.5 Start and stop

The passive facility requires no meaningful start work. A missing start hook is
normal under the Phase 3 optional lifecycle-hook contract.

Metrics does not need an executor drain. Exporter execution is contained by its
own service or executor lifecycle before metric storage deinitializes.

### 31.6 Controlled reboot

The current reboot policy rejects in-process reboot. A future controlled reboot
must explicitly define whether metric epochs restart or remain continuous.

## 32. Error Model

The typed metrics error domain initially distinguishes:

- facility unavailable or not ready;
- update/read window closed;
- wrong execution context;
- non-blocking contention;
- finite synchronization timeout;
- counter or accumulator overflow under reject policy;
- invalid or non-finite numeric input;
- unit conversion overflow or incompatibility;
- reducer failure;
- runtime reset forbidden;
- timestamp or clock failure;
- internal invariant failure.

Compile-time invalid instrument operations are not runtime errors.

Errors carry structured reason and relevant metric identity. Optional text is
diagnostic metadata rather than the contract.

## 33. Focused Runtime Records

The facility retains bounded per-metric operational facts such as:

- epoch;
- revision;
- successful update count;
- rejected update count;
- contention count;
- overflow and saturation count;
- invalid numeric count;
- runtime reset count;
- initialized or empty state;
- last update timestamp;
- last failure reason and timestamp;
- selected synchronization policy.

These records are not automatically public metrics. Explicit accounting views
may expose selected fields without duplicating storage.

## 34. Inspection

Compile-time inspection consumes immutable metric and view descriptors.
Runtime inspection consumes typed readings, scalar records, and focused
operational records.

Likely query areas include:

```cpp
solar::metrics::catalog::descriptor<Metric>();
solar::metrics::catalog::views<Metric>();
solar::metrics::get<Metric>();
solar::metrics::records::metric<Metric>();
solar::metrics::records::read(cursor, destination);
```

Inspection may filter, page, and format. It does not own storage, reducers,
epochs, or reset behavior.

There is no universal all-system snapshot.

## 35. Remote Boundary

Remote may explicitly consume:

- selected metric descriptors;
- selected scalar view descriptors;
- current typed or erased readings;
- focused metric records;
- explicit reset methods where separately authorized.

Remote does not own metric state, reducers, synchronization, epochs, update
policy, or exporter scheduling.

External exposure requires:

- explicit exposure registration;
- stable metric or view identity;
- stable unit and numeric schema;
- bounded codec;
- session authorization;
- polling or publication policy;
- per-session backpressure independent from producers.

Registration never implies Remote exposure. A slow Remote session reads stale
or skipped exports according to its policy; it cannot stall metric mutation.

## 36. Bus, Parameters, And Logging Boundaries

The Metrics facility does not automatically:

- emit bus messages when values change;
- invoke parameter hooks;
- generate observability events on every update;
- write logs on saturation or rejection;
- schedule exporter work.

Focused failure state remains available when sibling subsystems are disabled.
Explicit adapters may record selected exceptional facts without creating loops.

Metrics have no application change-hook API. Code that needs to react to a
value change should use the owning component, parameter hooks, or the typed bus
according to the actual semantics.

## 37. Configuration

### 37.1 Kconfig ownership

Kconfig owns build capability, hard ceilings, and defaults such as:

- metrics subsystem inclusion;
- supported scalar numeric representations;
- atomic, spinlock, and mutex backends;
- maximum histogram bucket count;
- maximum window reducer capacity;
- descriptor text retention;
- timestamp support;
- runtime reset capability;
- focused accounting inclusion;
- default concurrency policy;
- default arithmetic overflow policy;
- optional event accounting view support.

Likely symbols include:

```text
CONFIG_SOLAR_METRICS
CONFIG_SOLAR_METRICS_DESCRIPTOR_STRINGS
CONFIG_SOLAR_METRICS_TIMESTAMPS
CONFIG_SOLAR_METRICS_RUNTIME_RESET
CONFIG_SOLAR_METRICS_MAX_HISTOGRAM_BUCKETS
CONFIG_SOLAR_METRICS_MAX_WINDOW_SIZE
CONFIG_SOLAR_METRICS_ACCOUNTING
```

Exact names are finalized during implementation. There is no C++ fallback
configuration header.

### 37.2 C++ ownership

C++ declarations own:

- metric membership and semantic owner;
- descriptor and identity;
- value type;
- instrument and reducer;
- unit;
- views;
- synchronization override;
- overflow and invalid-input policy;
- reset eligibility;
- event metric adapters;
- explicit external exposure.

### 37.3 Precedence

```text
explicit metric or reducer policy
    > metrics blueprint configuration
    > Kconfig default
```

Typed policy cannot enable an excluded synchronization backend, timestamp,
runtime reset, numeric capability, reducer capacity, or sibling subsystem.

### 37.4 Default policy

The safe baseline is:

- automatic per-metric synchronization;
- saturating unsigned counters;
- reject non-finite floating values;
- gauges initially unset unless declared otherwise;
- no runtime reset unless explicitly enabled;
- no timestamps when compiled out;
- no event, logging, Remote, or exporter adapter by default.

## 38. Resource Accounting

The effective system computes exact metric-owned resources from the catalog:

- one exact slot per canonical instrument;
- reducer-specific bounded state;
- one per-metric synchronization primitive or atomic scheme;
- epoch, revision, and selected accounting fields;
- optional timestamp state;
- no storage for derived scalar views;
- no exporter queue or thread in the Metrics facility;
- no event accounting duplication for storage-free views;
- bounded descriptor and view metadata according to Kconfig.

Examples:

- a simple counter pays for one scalar plus synchronization and selected
  accounting;
- a gauge adds initialized and freshness state;
- `WindowMean<N>` pays for exactly its bounded window representation;
- `Histogram<B...>` pays for exactly its fixed buckets;
- `Summary` does not retain individual measurements.

Storage cost is derivable from the bound blueprint and target-selected policy.

## 39. Complete Example

```cpp
struct FramesDropped
{
    using Value = std::uint64_t;
    using Instrument = solar::metrics::Counter;
    using Unit = solar::metrics::units::Frames;

    static constexpr solar::metrics::Descriptor descriptor{
        .name = "remote.frames.dropped",
    };
};

struct BytesDropped
{
    using Value = std::uint64_t;
    using Instrument = solar::metrics::Counter;
    using Unit = solar::metrics::units::Bytes;

    static constexpr solar::metrics::Descriptor descriptor{
        .name = "remote.bytes.dropped",
    };
};

struct TxQueueDepth
{
    using Value = std::uint16_t;
    using Instrument = solar::metrics::Gauge;
    using Unit = solar::metrics::units::Items;

    static constexpr Value initial_value = 0;

    static constexpr solar::metrics::Descriptor descriptor{
        .name = "remote.tx_queue.depth",
    };
};

struct LoopDuration
{
    using Value = std::uint32_t;
    using Instrument = solar::metrics::Distribution<
        solar::metrics::Summary>;
    using Unit = solar::metrics::units::Microseconds;

    static constexpr solar::metrics::Descriptor descriptor{
        .name = "control.loop.duration",
    };
};

struct RemoteMetrics
{
    using Metrics = solar::metrics::Metrics<
        FramesDropped,
        BytesDropped,
        TxQueueDepth>;

    static solar::Result<void> frame_dropped(std::uint32_t bytes)
    {
        return solar::metrics::inc<FramesDropped>()
            .and_then([bytes](auto) {
                return solar::metrics::add<BytesDropped>(bytes);
            })
            .transform([](auto) {});
    }
};

struct RemoteService
{
    using Metrics = RemoteMetrics::Metrics;

    using EventMetrics = solar::events::metrics::Adapters<
        solar::events::metrics::On<
            FrameDropped,
            solar::events::metrics::Increment<FramesDropped>,
            solar::events::metrics::Add<
                BytesDropped,
                &FrameDropped::Payload::bytes>>>;
};

struct DriveController
{
    using Metrics = solar::metrics::Metrics<
        LoopDuration,
        ControlCycleTime>;
};

using RobotBlueprint = solar::Blueprint<
    solar::Services<RemoteService>,
    solar::Facilities<DriveController>,
    solar::metrics::Configuration<
        solar::metrics::DefaultConcurrency<
            solar::metrics::concurrency::Automatic>>>;

using RobotSystem = solar::System<RobotBlueprint>;

SOLAR_BIND_SYSTEM(RobotSystem);
```

Runtime use:

```cpp
solar::metrics::set<TxQueueDepth>(queue.size());

auto timer = solar::metrics::scoped<ControlCycleTime>();
run_control_loop();
auto recorded = timer.finish();

auto summary = solar::metrics::get<LoopDuration>();
```

## 40. Include Direction

Metric declaration headers include only:

- their numeric domain types;
- Solar metric descriptor, unit, instrument, and reducer headers.

Component headers include metric declarations they own or directly reference
and the contribution headers they require. They never include the application
composition root.

Definitions calling bound metric APIs normally live in source files that
include the completed root:

```cpp
// services/remote_service.cpp
#include "app/system.hpp"

solar::Result<void> RemoteService::record_drop(std::uint32_t bytes)
{
    return RemoteMetrics::frame_dropped(bytes);
}
```

Relaxed mode permits ordinary non-template inline methods without binding
visibility. Strict mode may use an out-of-line definition or the optional
defaulted function-template pattern from Phase 1.

## 41. Compile-Time Validation

Effective-system validation rejects:

- blueprint views or adapters referencing an unregistered metric;
- missing or invalid `Value`, `Instrument`, `Unit`, or descriptor;
- duplicate metric type, name, local identity, or stable identity;
- externally exposed metric or view without stable identity;
- incompatible reuse of an external identity or schema;
- invalid value type for the selected instrument;
- counter with non-unsigned initial value contract;
- unsupported reducer or reducer output;
- invalid histogram boundaries;
- zero or oversized window capacity;
- unavailable requested scalar view;
- view external identity collision;
- invalid unit descriptor or dimension ratio;
- incompatible timer unit;
- explicit atomic policy without target lock-free support;
- ISR frontend with incompatible reducer or synchronization;
- runtime reset request without metric and Kconfig capability;
- event adapter referencing unregistered event or metric;
- adapter operation incompatible with the metric instrument;
- accounting view requiring unavailable Events or Metrics capability;
- exporter or Remote exposure requiring unavailable codec or stable schema;
- metric registration while Metrics is disabled;
- generated lifecycle dependency cycles.

Diagnostics identify the metric, owner, instrument, reducer, view, and violated
capability instead of failing only inside slot or tuple generation.

## 42. Runtime Failure Behavior

Runtime errors remain possible after valid compilation:

- relaxed frontend use before binding, while disabled, or with an unregistered
  metric;
- facility not initialized or already closed;
- synchronization contention in a no-wait operation;
- finite lock timeout;
- arithmetic overflow under reject policy;
- invalid floating input;
- unit conversion overflow;
- target clock failure;
- reducer-reported failure;
- event metric adapter failure;
- scoped timer destructor recording failure;
- exporter read racing an administrative reset, reported through epoch.

One metric update either commits one coherent new instrument state or leaves it
unchanged. Later exporter failure cannot retroactively invalidate mutation.

## 43. Migration Direction

The current Solar metric implementation provides useful foundations:

- typed static descriptors;
- compile-time catalogs;
- static per-type storage;
- reducer policies;
- scalar erased values;
- passive facility semantics;
- domain group experimentation.

The target reforms:

- FNV name hashes as metric identity;
- `Gauge` implemented as a `Last` sample;
- public counter `set`;
- `Sample` as the primary instrument vocabulary;
- one global critical section for all metrics;
- context-based catalog reset;
- facility/store template arguments in groups;
- one-value `Snapshot` as the only query shape;
- facility-owned nested group binding;
- unrestricted assumptions about ISR safety;
- object-shaped facility access rather than global bound frontends.

Migration should introduce descriptors and contribution normalization first,
then slots and synchronization, strict frontends, structured readings/views,
groups, and explicit event adapters.

## 44. Verification Requirements

The implementation must eventually cover:

- compact `using Metrics` contribution collection;
- root metric declarations;
- owner and registration origin preservation;
- strict unregistered metric rejection and relaxed `NotRegistered` behavior;
- explicit and manifest identity behavior;
- duplicate metric and view diagnostics;
- counter, gauge, distribution, and timer concepts;
- invalid operation compile failures for every instrument kind;
- counter increment and non-negative addition;
- zero-add unchanged behavior;
- saturate, reject, and wrap overflow;
- counter monotonicity within reset epoch;
- unset and initialized gauges;
- equal-value gauge freshness updates;
- Last, Minimum, Maximum, Summary, WindowMean, EMA, and Histogram reducers;
- coherent multi-field summary readings;
- empty distribution readings;
- invalid histogram and window diagnostics;
- NaN, infinity, narrowing, and accumulator overflow policies;
- chrono duration conversion and incompatible-unit rejection;
- explicit and destructor-finished scoped timers;
- typed readings and scalar views;
- unsupported view compile failures;
- storage-free view behavior;
- external stable view IDs;
- per-metric atomic, spinlock, and mutex synchronization;
- target lock-free atomic validation;
- concurrent updates to unrelated metrics without global serialization;
- coherent read during concurrent update;
- non-waiting mutation and read forms;
- valid and invalid ISR frontends;
- lifecycle reset and runtime-reset policy;
- epoch behavior across reset;
- group use without a facility/store argument;
- explicit non-transactional group failure;
- event increment, add, set, observe, and timer adapters;
- aggregated event occurrence-count semantics;
- event adapter failure accounting without recursion;
- storage-free event accounting views;
- availability during init, rollback, stop, and deinit;
- disabled and unused facility elision;
- typed, record, catalog, and paged inspection;
- explicit Remote exposure and stable schema checks;
- exact static resource accounting;
- Kconfig defaults and typed policy precedence.

Host tests should use deterministic clocks and synchronization harnesses.
Zephyr integration tests should exercise target atomics, spinlocks, mutexes, ISR
updates, concurrent producers, lifecycle ordering, and exporter isolation.

Compile-fail tests are required for registration, operation, value, reducer,
view, unit, synchronization, ISR, reset, adapter, and capability violations.

## 45. Deferred Capabilities

The following remain deliberate later work:

- floating monotonic counters;
- up/down counters as a distinct instrument;
- dynamic labels or runtime cardinality;
- arbitrary user-defined structured metric values;
- cross-metric transactions;
- coherent point-in-time reads across many independent metrics;
- persistent metric history;
- host-controlled runtime reducer reconfiguration;
- quantile sketches and approximate high-cardinality reducers;
- distributed aggregation across devices;
- automatic dimensional algebra;
- generated schemas and views through future C++ reflection;
- stable shared-memory exporter views;
- in-process reboot epoch policy.

Deferred capabilities must preserve static registration, bounded storage,
instrument semantics, and passive canonical ownership.

## 46. Rejected Alternatives

### 46.1 One independent stored metric per distribution scalar

Rejected because repeated updates duplicate synchronization and storage and may
produce inconsistent count, minimum, maximum, and mean readings.

### 46.2 One opaque structured value with no scalar views

Rejected because embedded inspection, Remote, and exporters benefit from flat
scalar records with independent stable exposure identity.

### 46.3 Views own duplicate mutable storage

Rejected because the instrument reducer is canonical. Views are projections,
not another truth source.

### 46.4 Keep Sample as an instrument kind

Rejected because a sample is one measurement. Distribution more accurately
describes accumulated reduced state.

### 46.5 Implement gauge as Last distribution

Rejected because current explicitly set state and latest observed sample have
different initialization, mutation, freshness, and reading semantics.

### 46.6 Permit public counter set

Rejected because it destroys monotonic semantics and makes accidental backward
movement easy. Reset is explicit and epoch-aware.

### 46.7 Permit signed negative counter additions

Rejected initially because that creates an up/down counter with different
semantics. It may become a distinct future instrument.

### 46.8 Make all metric mutation return void

Rejected because lifecycle closure, contention, numeric rejection, and overflow
can matter to callers and deserve typed expected results.

### 46.9 Hide failures because metrics are best effort

Rejected because a caller may intentionally depend on diagnostic integrity.
Ignoring a returned result remains an explicit call-site choice.

### 46.10 One global critical section

Rejected because unrelated metric producers and rich readings would serialize
and large reducers would extend global interrupt latency.

### 46.11 Atomic storage for every metric

Rejected because rich reducer state cannot generally update coherently through
one lock-free atomic value and target guarantees vary.

### 46.12 Mutex storage for every metric

Rejected because simple high-rate and ISR metrics need bounded low-overhead
updates.

### 46.13 Infer ISR safety from frontend use

Rejected because reducers and synchronization must prove ISR compatibility.
Solar never silently weakens policy.

### 46.14 Return mutable metric references

Rejected because callers could bypass instrument semantics, synchronization,
revision, epoch, and accounting.

### 46.15 Keep Snapshot as the only output

Rejected because one scalar cannot express coherent distributions, while a
single oversized variant would burden simple counters. Typed readings and
scalar view records serve the two real needs.

### 46.16 Require a facility or store template in groups

Rejected because one active bound system owns one canonical metrics facility.
Global frontends keep domain groups compact.

### 46.17 Make groups catalog identities

Rejected because groups are ergonomic application facades. Their member
instruments retain the actual identity and storage.

### 46.18 Make group updates transactional

Rejected initially because metrics are independent observational facts and
cross-slot rollback would add broad locking and unclear failure semantics.

### 46.19 Put metric adapters inside every event schema

Rejected as the only path because reusable event schema should not require an
optional metrics subsystem or product-specific metric naming. Composition-level
adapters are primary.

### 46.20 Duplicate event accounting into metrics automatically

Rejected because Events already owns canonical accounting. Explicit
storage-free views or product metrics avoid duplicate truth.

### 46.21 Automatically emit events or logs on metric change

Rejected because metrics remain passive and high-rate updates could create
feedback loops or diagnostic floods.

### 46.22 Allow application subscriptions to metric changes

Rejected because metrics would become a hidden behavior bus. Use direct calls,
parameters, or bus messages for behavior.

### 46.23 Put exporter execution in the Metrics facility

Rejected because storage and transport scheduling have different lifecycle,
backpressure, and dependency ownership.

### 46.24 Automatically expose all metrics through Remote

Rejected because registration is not authorization, stable schema, rate policy,
or session capacity.

### 46.25 Runtime labels

Rejected initially because label values create runtime cardinality, allocation,
identity, storage, and exposure complexity. Use separate static metrics or a
bounded explicitly structured instrument.

### 46.26 Full dimensional-analysis library

Rejected because Solar needs semantic unit tags and safe same-dimension
conversion, not arbitrary symbolic unit algebra.

## 47. Accepted Decisions

1. Metrics are passive canonical numeric instruments.
2. Metrics do not deliver application behavior.
3. Metrics own no exporter, sink, transport, or worker thread.
4. Initial instrument kinds are counter, gauge, distribution, and timer.
5. Sample means one measurement rather than an instrument kind.
6. Every metric declares `Value`, `Instrument`, `Unit`, and `descriptor`.
7. Authored metadata uses `solar::metrics::Descriptor`.
8. The conventional component contribution alias is `Metrics`.
9. Root-owned metrics use `solar::Metrics`.
10. Contributions preserve semantic owner and registration origin.
11. Typed operations require effective catalog registration.
12. Internal-only metrics may use build-local identity.
13. Externally exposed metrics and views require stable identity.
14. Name and compiler type hashes are not external identity contracts.
15. Units are proper type tags with dimensions and optional ratios.
16. Solar supports explicit same-dimension ratio conversion.
17. Solar does not initially provide full dimensional algebra.
18. One canonical storage slot exists per instrument.
19. Structured instruments may expose several scalar views.
20. Views own no duplicate mutable state.
21. External scalar views receive stable explicit or manifest identity.
22. Typed readings are coherent instrument-specific copies.
23. Counters accept only `inc` and non-negative `add`.
24. Initial counters use unsigned integral values.
25. Public arbitrary counter set is prohibited.
26. Counters begin at zero.
27. Counter monotonicity is scoped to one reset epoch.
28. Counter overflow defaults to saturation.
29. Reject and explicit wrap overflow policies are supported.
30. Gauges represent explicitly set current state.
31. Gauges begin unset unless an initial value is declared.
32. Equal gauge sets refresh revision, count, and timestamp.
33. A gauge is not implemented as a Last distribution.
34. Distributions accept measurements through `observe`.
35. Initial reducers include Last, Minimum, Maximum, Summary, WindowMean, EMA,
    and Histogram.
36. Reducer state and capacity are statically bounded.
37. Empty distribution state is explicit.
38. Timers are duration-specialized distributions.
39. Timer recording accepts compatible `std::chrono::duration` values.
40. Scoped timers are move-only and support finish and cancel.
41. Destructor-time timer failure updates focused records.
42. Metric mutation returns `Result<Update, Error>`.
43. An unexpected mutation result leaves that instrument unchanged.
44. Cross-metric updates are not initially transactional.
45. `try_` operations are strictly non-waiting.
46. ISR operations use explicit `try_*_isr` frontends.
47. ISR compatibility derives from instrument, reducer, and synchronization.
48. Solar never weakens a reducer or policy for ISR use.
49. Non-finite floating input is rejected by default.
50. Numeric and unit conversion validates before mutation.
51. Runtime reset requires explicit metric and Kconfig capability.
52. Runtime reset increments the metric epoch.
53. Reset emits no automatic event, log, or bus message.
54. `get<Metric>` returns a typed bounded reading.
55. Focused erased inspection uses scalar metric view records.
56. Snapshot is not the canonical public metrics vocabulary.
57. Canonical runtime state lives in static Metrics facility slots.
58. Metric declaration types do not own mutable runtime state.
59. Every metric slot owns its synchronization.
60. There is no facility-wide metric update lock.
61. Initial concurrency policies are atomic, spin-locked, mutex-protected, and
    automatic.
62. Explicit atomic policy requires target-proven lock-free support.
63. One metric reading is coherent; unrelated metric readings are independent.
64. Metric groups are application-authored domain facades.
65. Groups call global frontends without facility or store template arguments.
66. Groups own no separate metric identity or storage.
67. Group updates are not transactional.
68. Event-to-metric integration uses declarative static adapters.
69. Composition-level event metric adapters are the preferred reusable form.
70. Aggregated event count adapters honor occurrence count.
71. Event metric adapter failure does not invalidate canonical event capture.
72. Event accounting remains canonical Events facility state.
73. Explicit event accounting metric views duplicate no storage.
74. Metrics remains a passive lifecycle facility.
75. Metrics is included only when enabled and required.
76. Metrics is an implicit built-in dependency of ordinary components when
    present.
77. Metrics initializes before ordinary components and resets all slots.
78. Metrics remains available through dependent init, rollback, stop, and
    deinit.
79. Exporter lifecycle and backpressure remain outside Metrics.
80. Focused records preserve update, reset, overflow, contention, and failure
    truth.
81. Metric changes have no application subscription API.
82. Remote consumes selected descriptors and readings without owning storage.
83. Registration never implies Remote exposure.
84. Kconfig owns build capabilities, hard ceilings, and defaults.
85. C++ types own membership, value, instrument, reducer, unit, and policy.
86. Explicit metric policy overrides blueprint configuration, then Kconfig.
87. Static storage cost is derivable from the effective blueprint.
88. Views, exporters, and event accounting projections do not silently allocate
    duplicate canonical state.
89. Metrics requires no dynamic allocation.
90. There is no Metrics facility object in the user-facing API.

## 48. Open Questions

There are no blocking open questions for Phase 8.

Later specifications and implementation must refine without changing this
contract:

- exact Kconfig symbol names and hard numeric ceilings;
- exact `Reading` and `MetricViewRecord` field widths;
- target-specific atomic and sequence-lock implementation;
- final stable view manifest representation;
- exact exporter registration through Phase 9 execution;
- Remote metric exposure syntax and publication policy;
- logging of selected metric infrastructure failures in Phase 8;
- generated unit, reducer, and view schema tooling;
- future C++ reflection-derived declarations and codecs;
- controlled in-process reboot epoch behavior.

These are extensions of the accepted instrument semantics, structured views,
bounded storage, synchronization, passive ownership, and adapter boundaries.
