# Observability Events

Date: 2026-07-15

Status: accepted design

Owning phase: Phase 6

Depends on:

- `00-design-conventions.md`
- `00a-modern-cpp-result-and-status.md`
- `01-system-blueprint-and-binding.md`
- `02-identity-contributions-and-catalogs.md`
- `03-lifecycle-kernel-and-configuration.md`
- `04-bus.md`
- `05-parameters.md`

## 1. Purpose

This specification defines Solar's observability event subsystem.

Events record structured operational facts independently from application bus
messages and human-oriented logs. They provide stable identity, source
attribution, occurrence ordering, bounded capture, retained history, and
infrastructure processing without becoming another application behavior bus.

It establishes:

- event, payload, occurrence, record, observer, and sink vocabulary;
- compact typed declarations and contribution aliases;
- stable event identity, semantic ownership, and occurrence source rules;
- severity, extensible domain, context, timestamp, sequence, and correlation;
- global observe, try-observe, source-override, and ISR APIs;
- synchronous bounded capture separated from deferred processing;
- fixed-slot ingress and compact variable-sized retained history;
- sampling, rate limiting, aggregation, retention, and critical reservations;
- automatic per-event and per-sink accounting;
- explicit recovery and consecutive-condition semantics;
- infrastructure-only observers and declarative sibling-subsystem adapters;
- lifecycle, shutdown, overflow, and failure behavior;
- Kconfig capability and C++ policy boundaries.

The normal path remains compact:

```cpp
auto result = solar::events::observe<FrameDropped>({
    .session = session,
    .bytes = frame.size(),
    .reason = DropReason::QueueFull,
});
```

No event facility object, system reference, runtime registry, or subscriber
handle appears in application code.

## 2. Non-Goals

Observability events are not:

- application commands or behavior notifications;
- a replacement for direct typed component calls;
- a replacement for the typed application bus;
- a runtime callback or subscription registry;
- mutable current-value storage;
- runtime parameters;
- raw numeric metric storage;
- textual log records;
- arbitrary component state snapshots;
- an unbounded audit database;
- a transport protocol or automatic Remote surface;
- a general variable-sized object queue;
- a guarantee that finite firmware can retain unlimited occurrences.

An event may later produce a metric update, textual log, persistent record, or
Remote frame through an explicit infrastructure adapter. Those derived outputs
do not replace the canonical event occurrence.

## 3. Semantic Boundaries

### 3.1 Structured operational fact

An event states that an operationally meaningful occurrence happened.

Examples include:

- a frame was dropped;
- a storage write failed;
- a component entered a degraded mode;
- a scheduling deadline was missed;
- a link recovered;
- bounded capacity was exhausted.

An event remains useful when logging, metrics, persistence, and Remote are all
disabled.

### 3.2 Bus message

A bus message coordinates application behavior. An observability event records
a fact.

```cpp
solar::bus::emit<EmergencyStopRequested>({.reason = reason});
solar::events::observe<CommandRejected>({.reason = reason});
```

Application behavior may subscribe to the bus. It must not subscribe to events
as a hidden control path. No automatic bus-to-event or event-to-bus conversion
exists.

### 3.3 Log

A log is human-oriented diagnostic text. An event is typed structured data with
stable identity.

An event may generate a log through a declared adapter. Event capture never
depends on text formatting and never calls the public logging frontend.

### 3.4 Metric

A metric is canonical numeric instrument state. Event accounting and explicit
event-to-metric adapters may update metrics, but the event subsystem does not
turn every event into a public metric automatically.

### 3.5 Parameter

A parameter is validated current runtime configuration. Parameter changes are
not automatically events. Explicit adapters may record selected changes or
persistence failures.

## 4. Canonical Vocabulary

The public vocabulary is:

- **event type**: the compile-time declaration of one operational fact schema;
- **payload**: typed data specific to that event;
- **occurrence**: one observation attempt and, when accepted, one captured fact;
- **receipt**: the synchronous result of an observation attempt;
- **event record**: a type-erased common envelope plus copied payload;
- **semantic owner**: the component or root that defines the event's meaning;
- **source**: the registered origin attributed to one occurrence;
- **observer**: infrastructure that consumes immutable event records;
- **sink**: an observer that retains, serializes, or forwards records;
- **capture policy**: synchronous admission and suppression behavior;
- **retention policy**: whether and where accepted records are retained;
- **adapter**: declarative conversion into another infrastructure subsystem;
- **condition identity**: event, source, and optional key used for recovery and
  consecutive semantics.

An event type is not an occurrence object. Application code does not construct
or publish `EventRecord` values directly.

## 5. Event Declaration

### 5.1 Common declaration

The event declaration and payload are deliberately separate:

```cpp
struct FrameDropped
{
    struct Payload
    {
        SessionId session;
        std::uint32_t bytes;
        DropReason reason;
    };

    static constexpr solar::events::Descriptor descriptor{
        .name = "remote.frame.dropped",
        .severity = solar::events::Severity::Warning,
        .domain = solar::events::domain::Communication,
    };

    using Capture = solar::events::capture::EveryOccurrence;
    using Retention = solar::events::retention::Buffered;
};
```

Only `Payload` and `descriptor` are required. Missing optional policies resolve
through event configuration and Kconfig defaults.

The separation provides one natural home for schema metadata and event policy
without adding those fields to every payload value.

### 5.2 Payload-free event

An event without event-specific data declares `void`:

```cpp
struct ControlLoopStarted
{
    using Payload = void;

    static constexpr solar::events::Descriptor descriptor{
        .name = "control.loop.started",
        .severity = solar::events::Severity::Informational,
        .domain = solar::events::domain::Lifecycle,
    };
};
```

It is observed without an empty placeholder object:

```cpp
solar::events::observe<ControlLoopStarted>();
```

### 5.3 Descriptor customization

The normal authored source is:

```cpp
static constexpr solar::events::Descriptor descriptor{...};
```

The generic Phase 2 customization point is:

```cpp
solar::descriptor_traits<solar::events::event_tag, Event>
```

Trait specialization supports third-party declarations without changing the
ordinary event syntax.

## 6. Descriptor And Metadata

The authored descriptor initially contains:

- stable human-readable name;
- default severity;
- operational domain;
- optional description;
- optional explicit stable ID under Phase 2 rules;
- schema version when persistence or external schema requires it.

Capture, retention, logging, metric, and sink behavior remain typed policy.
They do not become an open-ended descriptor flag bag.

Descriptor text may be stripped through Kconfig when inspection does not need
it. Identity and schema information required by persistence or an explicit
external adapter cannot be stripped while those capabilities remain enabled.

## 7. Severity

The initial ordered severity vocabulary is:

```cpp
solar::events::Severity::Trace
solar::events::Severity::Informational
solar::events::Severity::Warning
solar::events::Severity::Error
solar::events::Severity::Critical
```

Severity describes operational importance. It does not by itself select:

- capture guarantees;
- retention capacity;
- logging level;
- persistence;
- Remote exposure;
- system recovery behavior.

Those are explicit policies. In particular, `Critical` does not promise
unbounded or infallible storage.

Ordinary observations use the descriptor severity. Call sites cannot casually
lower or raise it. A semantically different severity should usually be a
different event or a structured payload field.

## 8. Domains

Domains classify operational meaning independently from component ownership.

Solar initially provides descriptor values for:

- lifecycle;
- scheduling;
- communication;
- storage;
- power;
- safety;
- device;
- resource management.

Domains are extensible stable descriptor values rather than a closed enum.
Applications and libraries may declare additional domains with valid identity
and metadata.

The same component may own events in several domains. Domain therefore never
replaces source or semantic owner.

## 9. Event Contributions

### 9.1 Component alias

Components contribute event declarations through the compact conventional
alias:

```cpp
struct RemoteService
{
    using Events = solar::events::Events<
        FrameDropped,
        SessionTimedOut,
        LinkRecovered>;
};
```

The events subsystem owns
`contribution_source<events::event_tag, Component>`. The generic Phase 2
collector preserves semantic owner and registration origin.

### 9.2 Root events

Application-owned declarations use the accepted root section:

```cpp
solar::Events<BootModeSelected, OperatorLockoutEntered>
```

Their default source is the system root unless explicitly overridden by a
registered source.

### 9.3 Catalog registration

Every typed observation requires effective event catalog membership:

```cpp
solar::events::observe<UnregisteredEvent>(); // strict error; relaxed NotRegistered
```

No frontend creates standalone event state for an unregistered type.

## 10. Identity And Ownership

### 10.1 Type identity

The C++ declaration type is the local compile-time identity used by typed APIs.

### 10.2 Local numeric identity

The effective event catalog assigns a compact local ID for runtime indexing.
Local IDs may change when the firmware composition changes and are not wire or
persistence contracts.

### 10.3 Stable identity

Every effective event resolves a stable external identity through an explicit
ID or generated manifest under Phase 2 rules.

Name hashing and compiler type-name hashing are not supported persistence or
wire identities.

### 10.4 Semantic owner

The contribution origin normally becomes the semantic owner. Ownership means
the component defines the event schema and meaning; it does not imply that
every occurrence physically originated there.

### 10.5 Schema evolution

Changing payload interpretation while retaining a stable ID requires a schema
version and an explicit compatibility story for every persistent or external
consumer. An incompatible meaning receives a new stable event ID.

## 11. Source Attribution

### 11.1 Default source

The default occurrence source is the event's semantic owner. This keeps the
common path concise for component-owned events.

### 11.2 Explicit typed override

Shared declarations, adapters, and forwarded observations use:

```cpp
solar::events::observe_from<UartTransport, FrameDropped>(payload);
```

The source must resolve to:

- an effective registered component;
- the system root;
- a reserved Solar facility source;
- another explicitly registered source descriptor.

Arbitrary integers and strings are not accepted by the normal typed API.

### 11.3 Source is not ownership

The catalog preserves semantic owner. Each record preserves occurrence source.
Inspection and external adapters may expose both.

## 12. Public Observation API

### 12.1 Thread-context APIs

```cpp
solar::events::observe<Event>(payload, options);
solar::events::try_observe<Event>(payload, options);

solar::events::observe_from<Source, Event>(payload, options);
solar::events::try_observe_from<Source, Event>(payload, options);
```

Payload-free forms omit `payload`.

### 12.2 ISR APIs

```cpp
solar::events::try_observe_isr<Event>(payload, options);
solar::events::try_observe_isr_from<Source, Event>(payload, options);
```

There is no potentially waiting `observe_isr` overload. ISR observation is
always explicit and non-blocking.

### 12.3 Return type

Every frontend returns:

```cpp
solar::Result<solar::events::Receipt, solar::events::Error>
```

Callers may intentionally ignore a receipt when no local response is useful.
The result is not made `void` merely because failure is also accounted
globally.

### 12.4 Meaning of `try_`

`try_observe` never waits for capture capacity or a contended synchronization
primitive. It does not mean “skip validation” or “ignore errors.”

`observe` may perform only the finite capture-admission wait allowed by the
effective policy. It never waits for deferred processing, logging, metrics,
Remote, formatting, persistence, or arbitrary sinks.

## 13. Observation Options

The initial options are bounded value metadata, not an extensible callback
object:

```cpp
struct ObserveOptions
{
    CorrelationId correlation{};
    LogIntent log_intent{LogIntent::Default};
};
```

The logging intent values are:

- `Default`;
- `Suppress`;
- `Force`.

Event policy remains authoritative. Suppress cannot override mandatory event
logging, and force cannot override forbidden logging.

Source selection remains a typed template operation rather than an unvalidated
field in `ObserveOptions`. Severity, retention, event ID, and domain are not
mutable call-site options.

Exact logging integration is finalized by Phase 8 without changing this
authority rule.

## 14. Receipt And Disposition

The receipt reports synchronous admission:

```cpp
struct Receipt
{
    CaptureDisposition disposition;
    Sequence sequence;
    Timestamp timestamp;
    std::uint32_t occurrence_count;
};
```

Initial dispositions include:

- captured;
- sampled out;
- rate limited;
- accumulated into an aggregate.

Sampling, rate limiting, and aggregation are successful policy outcomes. They
are not errors. A sequence is valid only when the receipt refers to a material
captured record.

A successful receipt promises synchronous acceptance under the event capture
policy. It does not promise that every optional downstream sink will later
accept the record.

## 15. Event Error

The typed event error domain initially distinguishes:

- facility unavailable or not ready;
- mutation/capture window closed;
- capture buffer full;
- non-blocking contention;
- bounded admission timeout;
- invalid execution context;
- unsupported ISR event or policy;
- payload size or alignment unsupported;
- required capture guarantee exhausted;
- internal invariant failure.

Errors carry structured reason and relevant local identity. Human-readable text
is optional metadata rather than the error contract.

No exception is thrown by the event frontend.

## 16. Common Event Record Envelope

Every material occurrence carries a common header equivalent to:

```cpp
struct EventRecordHeader
{
    EventId event;
    SourceId source;
    Sequence sequence;
    Timestamp timestamp;
    ContextKind context;
    Severity severity;
    CorrelationId correlation;
    std::uint32_t occurrence_count;
    std::uint32_t lost_before;
    std::uint16_t payload_size;
    RecordFlags flags;
};
```

The exact integer widths remain implementation details subject to the resource
and identity requirements below.

### 16.1 Sequence

Sequence is globally monotonic across successfully materialized event records
for one boot. Observation attempts suppressed before materialization do not
consume a material record sequence.

Wrap behavior is explicit and detectable. The implementation should use a
width for which wrap is not expected during a normal boot.

### 16.2 Timestamp

Timestamp is captured at observation, never when deferred processing happens.

Thread observations use the configured monotonic clock. ISR capture may retain
a cycle-domain timestamp and normalize it later. Clock domain or timestamp
quality is represented in flags when normalization cannot be exact.

### 16.3 Context kind

Context initially distinguishes:

- thread;
- ISR;
- kernel or Solar infrastructure;
- reserved platform contexts where required.

Context is derived from the selected frontend and runtime environment. Callers
do not forge it.

### 16.4 Correlation

Correlation IDs group related occurrences and derived records. Absence is a
valid value. Correlation is not event identity and does not change aggregation
keys unless a policy explicitly includes it.

### 16.5 Loss and aggregation

`occurrence_count` is one for an ordinary record and greater than one for a
materialized aggregate. `lost_before` reports known preceding loss associated
with the relevant source, event, or capture partition according to record
flags.

## 17. Payload Contract

### 17.1 Initial requirements

A non-void event payload must be:

- complete;
- trivially copyable;
- trivially destructible;
- bounded at compile time;
- no larger or more aligned than the configured capture slot permits;
- free from hidden ownership requiring dynamic allocation.

Pointers, references, spans, string views, and borrowed buffers are invalid as
canonical asynchronous event payload data unless an explicit future policy can
prove a stable lifetime. The initial implementation does not provide that
policy.

### 17.2 Bounded semantic data

Variable semantic data uses owned bounded representations such as:

- `FixedString<N>`;
- `std::array<T, N>`;
- bounded vectors with inline storage;
- compact IDs and enums;
- hashes, lengths, or excerpts.

An event should describe a failed frame rather than copy an entire frame.

### 17.3 External encoding

Trivial copyability does not make raw object bytes a stable wire format.
Persistent and Remote adapters require an explicit deterministic codec and
schema version under Phase 2 rules.

## 18. Runtime Ownership

The built-in Events facility owns all mutable event runtime state through
static type-owned storage:

- thread ingress slots;
- ISR ingress slots;
- retained history bytes;
- per-event policy state;
- automatic accounting;
- processor scheduling state;
- observer and sink records;
- overflow latches;
- focused query synchronization.

Event declaration types do not own mutable queues or counters. Observers do not
own canonical event history merely because they consume records.

No monolithic `System` runtime object or dynamic event registry is introduced.

## 19. Two-Stage Storage Model

### 19.1 Fixed capture ingress

Synchronous capture uses fixed-size slots with a Kconfig-bounded maximum copied
payload size.

This provides:

- deterministic copies;
- simple bounded synchronization;
- predictable ISR behavior;
- complete-record admission;
- no dynamic allocation;
- explicit static memory cost.

The thread and ISR paths have separate ingress partitions so ordinary thread
traffic cannot consume all capacity reserved for ISR observations.

### 19.2 Compact retained history

Deferred retention uses a bounded byte ring containing complete variable-sized
records:

```text
[header][payload][header][payload]...
```

The stored length derives from the actual event payload size rather than the
maximum ingress payload size. Wrap markers and integrity metadata are internal
and cannot appear as partial public records.

### 19.3 Why two stages

One global maximum-sized history slot wastes memory for small events. A
variable-sized producer ring complicates deterministic ISR reservation and
concurrent complete-record admission. The two-stage model gives the capture
path fixed predictable cost and the history path compact storage.

## 20. Synchronous Capture

Capture performs only work required to accept one structured fact:

1. validate compile-time registration and runtime availability;
2. determine source and context;
3. acquire a timestamp;
4. increment observation accounting;
5. apply cheap synchronous capture policy;
6. reserve one appropriate ingress slot;
7. copy header and payload completely;
8. assign material occurrence sequence;
9. signal deferred processing;
10. return a receipt.

Capture does not:

- format text;
- call application handlers;
- invoke Remote;
- update declared public metrics;
- call persistent storage;
- fan out directly to sinks;
- hold a critical section across external code.

The current Solar implementation's direct sink publication while holding the
event critical section is therefore not part of the target design.

## 21. Deferred Processing

The event processor performs infrastructure work after capture:

```text
drain ingress by sequence
    -> materialize retention
    -> update policy and condition state
    -> update automatic accounting
    -> invoke metric adapters
    -> invoke log adapters
    -> route immutable records to configured sinks
```

The processor uses shared Solar execution rather than requiring one service
thread per event subsystem. Phase 9 defines the final executor adapter and work
registration spelling.

Thread and ISR ingress are each FIFO. The processor compares their head
sequences to preserve material occurrence order while draining.

Optional slow observers require their own bounded admission. No observer may
block the core event processor indefinitely.

## 22. Capture Policies

Initial typed capture policies are:

```cpp
solar::events::capture::EveryOccurrence
solar::events::capture::SampleEvery<N>
solar::events::capture::RateLimited<Interval>
solar::events::capture::AggregateCount<Window, Key>
```

### 22.1 Every occurrence

`EveryOccurrence` means Solar does not intentionally sample or rate-suppress
the event. It does not promise infinite queue or history capacity.

### 22.2 Sampling

`SampleEvery<N>` captures the first observation and then every Nth subsequent
observation under a precisely testable counter rule. Suppressed observations
remain visible in automatic accounting.

### 22.3 Rate limiting

Rate limiting captures the first eligible occurrence in a window and counts
suppressed occurrences. A later material record reports the accumulated
suppression where the policy permits.

The clock and boundary convention are part of the policy type and are tested at
exact window edges.

### 22.4 Aggregation

Aggregation accumulates a bounded occurrence count and emits a representative
record with:

- aggregation window;
- occurrence count;
- representative or policy-defined payload;
- condition key where applicable.

Keyed aggregation declares fixed cardinality and explicit key-table overflow.
It cannot allocate an unbounded key map at runtime.

### 22.5 Policy state

Capture policy state is exact static per-event storage. Events using only
`EveryOccurrence` do not pay for rate windows, sampling counters beyond normal
accounting, or keyed aggregation tables.

## 23. Retention Policies

Initial retention policies are:

```cpp
solar::events::retention::Transient
solar::events::retention::Buffered
solar::events::retention::Critical<ReservedSlots>
solar::events::retention::Persistent<Store>
```

### 23.1 Transient

The record may feed deferred infrastructure adapters but is not copied into
ordinary retained history.

### 23.2 Buffered

The record enters the bounded in-memory flight-recorder history. This is the
default event retention policy.

### 23.3 Critical

Critical retention reserves bounded ingress and history capacity and requires
an explicit exhaustion action. Lower-priority traffic cannot consume its
reservation.

### 23.4 Persistent

Persistent retention adds an explicit typed storage sink with stable identity,
schema, codec, capacity, and failure policy. Persistence is not implied by
severity or ordinary buffered retention.

### 23.5 Severity independence

Severity communicates importance; retention allocates resources. A warning may
need durable audit retention, while a high-rate critical transition may use a
small reserved flight recorder and explicit panic policy. The two remain
independent.

## 24. History

### 24.1 Complete records

History inserts and evicts complete records. Partial headers or payloads are
never exposed after wrap or power-independent in-memory corruption checks.

### 24.2 Normal overflow

The ordinary buffered history defaults to dropping the oldest complete record
to admit a new record. Eviction increments bounded accounting and creates a
detectable sequence gap.

### 24.3 Critical partition

Critical retention uses a separate partition or equivalent reservation.
Ordinary history pressure cannot evict reserved critical records.

### 24.4 Query surface

Focused query forms include:

```cpp
solar::events::history::latest();
solar::events::history::latest<FrameDropped>();
solar::events::history::read(cursor, destination);
```

Queries return value copies or records decoded into caller-owned bounded
storage. They do not return a span whose backing bytes may be overwritten after
the lock is released.

History cursors include enough sequence and generation information to report
that requested records were evicted.

### 24.5 Typed decoding

Infrastructure and inspection may decode a compatible record as its declared
event payload. Decode validates event identity, schema, payload size, and
alignment. Application behavior must not use history replay as a command path.

## 25. ISR Behavior

ISR observation:

- uses only `try_observe_isr` frontends;
- never waits;
- copies into dedicated fixed ingress slots;
- uses only ISR-safe timestamp and synchronization operations;
- performs no formatting or sink calls;
- supports only ISR-compatible capture policies;
- returns explicit failure when no slot is available.

An event callable from ISR must have a trivially copyable bounded payload and a
policy proven ISR safe at compile time.

Rate limiting or aggregation requiring non-ISR-safe clocks, locks, or key tables
is rejected for ISR topology. Solar never silently changes the event's policy
when called from ISR.

## 26. Concurrency

Concurrent thread producers and ISR producers are supported.

The implementation may use Zephyr spinlocks, atomics, or bounded critical
sections appropriate to the selected target, provided that:

- no external observer runs under the capture lock;
- complete-slot ownership is unambiguous;
- sequence assignment is race free;
- producer latency has a documented bound;
- history mutation and queries are coherent;
- `try_` operations never wait on a mutex;
- target atomic assumptions are compile-time or platform validated.

One global critical section covering capture, history, formatting, and sink I/O
is rejected.

## 27. Overflow And Critical Capture

### 27.1 Honest bounded guarantees

No finite event system can guarantee retaining an unbounded occurrence stream.
Solar expresses guarantees in terms of reserved capacity and explicit
exhaustion behavior.

### 27.2 Ingress overflow

Default ordinary ingress overflow rejects the newest observation. It preserves
already accepted records awaiting processing and reports `capture_full` to the
producer.

Alternative overwrite behavior requires explicit policy because it destroys a
previously accepted occurrence.

### 27.3 Critical exhaustion actions

Initial bounded actions may include:

- latch failure and continue;
- reject and report;
- attempt a configured synchronous emergency store;
- controlled panic.

There is no arbitrary callback exhaustion action. Panic is never inferred only
from `Severity::Critical`.

### 27.4 Failure while reporting failure

The event subsystem does not recursively emit an event when event capture
itself fails. It updates dedicated counters and latches. The next successful
record may report preceding loss through `lost_before`.

## 28. Automatic Accounting

The facility maintains bounded per-event accounting independent of the public
metrics subsystem:

- observation attempts;
- materialized captures;
- sampled observations;
- rate-suppressed observations;
- aggregate accumulation and flushes;
- ingress rejection;
- history retention and eviction;
- known lost occurrences;
- last capture failure reason and timestamp;
- active overflow latch.

Per-observer or per-sink records maintain:

- offered records;
- accepted records;
- rejected records;
- processing failures;
- last failure;
- queue occupancy and high-water mark where applicable.

These are focused event facility records. A later metric adapter may expose
selected values as typed metrics without becoming their canonical owner.

## 29. Infrastructure Observers And Sinks

### 29.1 Allowed responsibilities

Observers may:

- retain records;
- serialize records;
- forward records to bounded diagnostic transports;
- update metrics through declared adapters;
- generate logs through the internal event-log bridge;
- maintain bounded diagnostic aggregates;
- store persistent event records.

### 29.2 Prohibited role

Observers must not perform application domain behavior such as:

- commanding motors;
- changing control modes;
- satisfying application requests;
- invoking arbitrary component recovery;
- publishing hidden behavior notifications.

Solar provides no public `subscribe<Event>()` API. Infrastructure observers are
part of static event configuration and declare the infrastructure observer
role.

C++ cannot inspect an observer body to prove architectural purity. The API
therefore removes the casual behavior-subscription path, constrains observer
registration, and makes misuse visible in design and review.

### 29.3 Immutable input

Observers receive an immutable event record view valid only for the documented
call. An observer retaining data must copy it into its own bounded storage.

### 29.4 Sink isolation

A slow or fallible sink uses bounded independent admission. Failure of one sink
does not prevent attempts to deliver to other configured sinks or invalidate
canonical capture already completed.

## 30. Recovery And Resolution

### 30.1 Explicit recovery

The absence of another fault event never means recovery.

One declaration may explicitly resolve another:

```cpp
struct LinkRecovered
{
    using Payload = LinkIdentity;
    using Resolves = LinkLost;

    static constexpr solar::events::Descriptor descriptor{
        .name = "remote.link.recovered",
        .severity = solar::events::Severity::Informational,
        .domain = solar::events::domain::Communication,
    };
};
```

Alternatively, one state-change event may carry previous and current states.

### 30.2 Resolution compatibility

A resolution relation validates compatible source and key semantics. A
recovery event cannot silently clear every instance of a keyed fault unless it
explicitly declares that scope.

### 30.3 No automatic domain action

Resolution updates event processor condition state and derived diagnostic
policy only. It does not invoke application recovery behavior.

## 31. Consecutive Semantics

“Consecutive” means repeated observations of one condition identity, not
adjacent records in the global concurrent event stream.

The default condition identity is:

```text
event + source + optional declared key
```

Unrelated interleaved events do not reset a streak.

A streak resets through:

- a declared recovery event;
- an explicit state transition;
- an optional policy-owned expiry window.

Policies such as `AfterConsecutive<5>` must declare key extraction, reset event,
window, and threshold edge behavior when the defaults are insufficient.

Consecutive policy is principally useful for deferred logging, metric, and
alert adapters. It does not require suppressing canonical capture unless a
capture policy separately says so.

## 32. Event-To-Metric Adapters

Metric integration is declarative infrastructure behavior.

An adapter may:

- increment a counter for each material occurrence;
- add `occurrence_count` for aggregates;
- observe a typed numeric payload field;
- update a bounded derived instrument according to explicit policy.

It preserves event owner, occurrence source, and adapter origin through Phase 2
catalog rules.

If metrics are disabled, optional adapters compile out and event capture
continues. A configuration that explicitly requires an unavailable metric
adapter is a compile-time configuration error.

Phase 7 defines exact metric declaration and adapter syntax. It must not change
event capture, identity, or accounting ownership established here.

## 33. Event-To-Log Adapters

Log integration is also declarative infrastructure behavior.

An event-generated log preserves:

- stable and local event identity where available;
- occurrence sequence;
- source;
- timestamp;
- correlation;
- aggregation count;
- rendering policy identity.

The adapter uses an internal event-to-log bridge. It does not call the public
`solar::log` frontend, so it cannot recursively create the same event through
ordinary log ingestion.

Logging policies may include:

- always;
- never;
- first occurrence;
- every Nth occurrence;
- after N consecutive occurrences;
- rate limited;
- on transition or recovery.

Infrastructure failure events may declare logging forbidden. Mandatory and
forbidden event policy outrank call-site `LogIntent`.

If logging is disabled, optional adapters compile out and events remain useful.
Phase 8 defines exact renderer, level, and backend integration syntax.

## 34. Correlation And Derived Records

Derived metric, log, persistence, and Remote records preserve the originating
event's correlation and sequence where their subsystem schema supports it.

Infrastructure may create a new correlation ID through a focused utility when
beginning a diagnostic operation. Correlation generation is bounded and does
not require an event facility object.

Correlation does not imply causal ordering. An optional future causal parent
field may reference an occurrence sequence, but is not part of the initial
minimum envelope.

## 35. Lifecycle

### 35.1 Inclusion

The built-in Events facility is included only when:

- event support is enabled by Kconfig; and
- the effective system has events, observers, adapters, or explicit event
  configuration requiring it.

When included, it is a normal lifecycle facility with static type-owned state.

### 35.2 Dependency injection

Ordinary components that contribute or observe events acquire an implicit
dependency on the Events facility. Users do not list the built-in facility in
their component graph.

The facility initializes before dependent components, so component init and
start hooks may record boot-time facts.

### 35.3 Availability window

Event capture becomes available after Events facility initialization and
remains available through:

- dependent component init;
- start;
- running operation;
- boot failure rollback;
- dependent component stop;
- dependent component deinit.

The facility closes capture only when its dependants can no longer execute.

### 35.4 Shutdown

The deferred event processor normally uses shared execution while that
execution remains available. During shutdown:

1. normal deferred work is contained under the Phase 3 executor rules;
2. dependent components may still capture stop and deinit facts;
3. the Events facility performs a final bounded synchronous drain;
4. shutdown-safe retention and sinks receive eligible records;
5. unavailable asynchronous sinks are accounted rather than awaited forever;
6. capture closes and storage deinitializes.

This permits lifecycle failure records without violating global execution
containment.

### 35.5 Facility failure

If the Events facility itself cannot initialize, the boot report remains the
authoritative fallback. Solar does not require events in order to report that
events are unavailable.

## 36. Configuration

### 36.1 Kconfig ownership

Kconfig owns compiled capability, hard ceilings, and platform defaults such as:

- event subsystem inclusion;
- thread ingress depth;
- maximum capture payload size and alignment;
- ISR ingress capability and depth;
- retained history bytes;
- critical reserve capacity;
- processor execution capability;
- descriptor text retention;
- automatic accounting inclusion;
- timestamp backend capability;
- persistent event support;
- default capture, retention, overflow, and timeout policy.

Likely symbol names include:

```text
CONFIG_SOLAR_EVENTS
CONFIG_SOLAR_EVENTS_INGRESS_DEPTH
CONFIG_SOLAR_EVENTS_MAX_PAYLOAD_SIZE
CONFIG_SOLAR_EVENTS_ISR_INGRESS_DEPTH
CONFIG_SOLAR_EVENTS_HISTORY_BYTES
CONFIG_SOLAR_EVENTS_CRITICAL_RESERVE
CONFIG_SOLAR_EVENTS_DESCRIPTOR_STRINGS
CONFIG_SOLAR_EVENTS_ACCOUNTING
```

Exact names are finalized during implementation. There is no C++ fallback
configuration header.

### 36.2 Typed C++ ownership

C++ declarations own:

- event membership and semantic owner;
- payload type;
- descriptor and stable identity;
- severity and domain;
- capture and retention policy;
- recovery and condition-key relations;
- observer and sink registration;
- metric and logging adapters;
- persistent codec and schema;
- source override types.

### 36.3 Precedence

```text
explicit event or adapter policy
    > event blueprint configuration
    > Kconfig default
```

Typed policy cannot re-enable excluded ISR, persistence, logging, metric,
executor, or storage capability or exceed hard Kconfig resource ceilings.

### 36.4 Default policy

The safe baseline is:

- `EveryOccurrence` capture;
- `Buffered` retention;
- reject-newest ingress overflow;
- drop-oldest complete record in ordinary history;
- no implicit persistent, metric, logging, or Remote adapter;
- automatic accounting enabled when compiled;
- no arbitrary application observers.

## 37. Resource Accounting

The effective system computes event-owned static resources from the catalog:

- fixed thread ingress slots;
- fixed ISR ingress slots only when needed;
- one bounded compact history ring;
- critical reservations only for events declaring them;
- exact per-event counters and policy state;
- bounded keyed aggregation tables only where declared;
- one shared processor execution registration when deferred work is required;
- exact observer queue and record state;
- codec and persistent scratch only for persistent adapters.

An event using default capture and retention does not pay for keyed aggregation,
persistent codecs, or per-event dedicated threads.

## 38. Focused Inspection

The event subsystem provides focused immutable descriptors and mutex- or
critical-section-protected value copies for runtime state.

Likely query areas include:

```cpp
solar::events::catalog::descriptor<Event>();
solar::events::records::event<Event>();
solar::events::records::sink<Sink>();
solar::events::history::latest<Event>();
solar::events::history::read(cursor, destination);
```

Inspection may format and correlate this information. It does not own or mutate
capture, policy state, history, or sink truth.

There is no universal system snapshot containing all event payloads and every
other subsystem.

## 39. Remote Boundary

Remote may consume:

- event descriptors and schemas;
- selected event records;
- bounded history pages;
- automatic event accounting;
- explicit live event streams.

Remote does not own event capture, identity, history, policy state, or canonical
records.

Registration never implies Remote exposure. External forwarding requires:

- explicit exposure registration;
- stable external identity;
- deterministic payload codec;
- schema version;
- authorization and session policy;
- bounded per-session admission and loss reporting.

A slow or disconnected Remote session cannot block event producers or the core
event processor.

## 40. Complete Example

```cpp
struct FrameDropped
{
    struct Payload
    {
        SessionId session;
        std::uint32_t bytes;
        DropReason reason;
    };

    static constexpr solar::events::Descriptor descriptor{
        .name = "remote.frame.dropped",
        .severity = solar::events::Severity::Warning,
        .domain = solar::events::domain::Communication,
    };

    using Capture = solar::events::capture::RateLimited<
        solar::events::delay::Milliseconds<100>>;

    using Retention = solar::events::retention::Buffered;
};

struct LinkLost
{
    using Payload = LinkIdentity;

    static constexpr solar::events::Descriptor descriptor{
        .name = "remote.link.lost",
        .severity = solar::events::Severity::Error,
        .domain = solar::events::domain::Communication,
    };

    using Retention = solar::events::retention::Critical<4>;
};

struct LinkRecovered
{
    using Payload = LinkIdentity;
    using Resolves = LinkLost;

    static constexpr solar::events::Descriptor descriptor{
        .name = "remote.link.recovered",
        .severity = solar::events::Severity::Informational,
        .domain = solar::events::domain::Communication,
    };
};

struct RemoteService
{
    using Events = solar::events::Events<
        FrameDropped,
        LinkLost,
        LinkRecovered>;
};

using RobotBlueprint = solar::Blueprint<
    solar::Services<RemoteService>,
    solar::events::Configuration<
        solar::events::DefaultRetention<
            solar::events::retention::Buffered>>>;

using RobotSystem = solar::System<RobotBlueprint>;

SOLAR_BIND_SYSTEM(RobotSystem);
```

Runtime use:

```cpp
auto dropped = solar::events::observe<FrameDropped>({
    .session = session,
    .bytes = frame.size(),
    .reason = DropReason::QueueFull,
});

auto lost = solar::events::try_observe_isr_from<UartTransport, LinkLost>(link);
```

## 41. Include Direction

Event declaration headers include only:

- their payload-domain value types;
- Solar event declaration and policy headers.

Component headers include event declarations they own or directly reference and
the Solar contribution headers they require. They never include the application
composition root.

Definitions calling bound global APIs normally live in source files that
include the completed root:

```cpp
// services/remote_service.cpp
#include "app/system.hpp"

solar::Result<void> RemoteService::receive_frame(Frame frame)
{
    if (!queue.try_push(frame))
    {
        return solar::events::observe<FrameDropped>({
            .session = frame.session(),
            .bytes = frame.size(),
            .reason = DropReason::QueueFull,
        }).transform([](const auto&) {});
    }

    return {};
}
```

The composition root includes component declarations and supplies the one
application binding. Ordinary event headers remain independent from the root,
preventing circular inclusion.

## 42. Compile-Time Validation

Effective-system validation rejects:

- blueprint processors or adapters referencing an unregistered event;
- missing or invalid `Payload`;
- missing or invalid descriptor;
- duplicate event type, name, local identity, or stable identity;
- missing stable identity after manifest resolution;
- incompatible reuse of a stable ID or schema version;
- invalid or unavailable source override;
- payload size or alignment beyond Kconfig ceilings;
- non-trivially-copyable initial payloads;
- borrowed or unbounded payload members detectable by declared traits;
- invalid severity or domain identity;
- capture policy requiring disabled timing or storage capability;
- keyed aggregation without bounded key cardinality;
- ISR use with incompatible payload, policy, clock, or synchronization;
- critical reservation beyond configured capacity;
- persistent retention without stable identity, codec, or schema;
- observer registration lacking infrastructure role;
- incompatible recovery key or source semantics;
- log or metric adapter requiring a disabled sibling subsystem;
- event registration while events are disabled;
- generated lifecycle dependency cycles.

Diagnostics identify the event, semantic owner, source, policy, and violated
capability instead of failing only inside erased record storage generation.

## 43. Runtime Failure Behavior

Runtime failures remain possible after valid compilation:

- relaxed frontend use before binding, while disabled, or with an unregistered
  event;
- facility not initialized or already closed;
- ingress capacity exhausted;
- non-blocking synchronization contention;
- finite admission timeout;
- timestamp backend failure or degraded quality;
- history eviction;
- aggregation-key capacity exhausted;
- deferred processor scheduling failure;
- observer queue rejection;
- sink serialization or storage failure;
- final shutdown drain timeout.

Synchronous frontend errors report capture failure. Failures after successful
capture remain in focused records and sink accounting; they do not retroactively
turn the producer's receipt into an error.

No event failure causes an unbounded retry or recursive event emission.

## 44. Migration Direction

The current event implementation provides useful seeds:

- fixed static history;
- source and severity metadata;
- sequence and timestamp fields;
- bounded formatting buffers;
- dropped-record accounting.

The target replaces or reforms:

- FNV name hashes as event identity;
- narrow fixed `value/detail` payload fields;
- raw name and source pointers in canonical records;
- direct sink publication during capture;
- formatting and I/O under the event critical section;
- object/context-shaped facility access;
- `void` emission that loses capture status;
- sink failures ignored by the producer-facing model;
- one combined capture, history, formatting, and delivery path.

Migration should proceed by introducing the typed catalog and record envelope,
then the split ingress/processor architecture, then policy state, history,
queries, and explicit adapters.

## 45. Verification Requirements

The implementation must eventually cover:

- compact `using Events` contribution collection;
- root event declarations;
- semantic owner and registration origin preservation;
- strict unregistered observation rejection and relaxed `NotRegistered`
  behavior;
- explicit and manifest stable identity;
- duplicate identity diagnostics;
- payload-free and bounded payload events;
- payload size, alignment, copyability, and borrowing diagnostics;
- default and explicit typed source attribution;
- thread `observe` and non-blocking `try_observe`;
- ISR `try_observe_isr` and source override;
- receipt disposition and typed errors;
- monotonic material occurrence sequencing;
- timestamp capture and ISR normalization flags;
- context and correlation preservation;
- complete-record fixed ingress admission;
- independent ISR and thread capacity;
- sequence-order merge across ingress rings;
- compact variable-sized history insertion and wrap;
- complete-record history eviction;
- caller-owned history pages and stale cursors;
- every-occurrence, sample, rate-limit, and aggregate policies;
- deterministic policy boundary behavior;
- bounded keyed aggregation and key overflow;
- transient, buffered, critical, and persistent retention;
- critical reservation isolation;
- every critical exhaustion action;
- per-event and per-sink automatic accounting;
- no recursive event on event infrastructure failure;
- explicit recovery and state-transition forms;
- source/key-aware consecutive semantics;
- unrelated event interleaving not resetting streaks;
- infrastructure observer role validation;
- sink isolation and all-observer attempts;
- metric and log adapter boundaries;
- event-to-log origin and sequence preservation;
- recursion prevention for event-generated logs;
- availability during dependent init, rollback, stop, and deinit;
- final bounded synchronous drain;
- disabled and unused facility elision;
- exact static resource accounting;
- focused descriptor, record, sink, and history queries;
- Kconfig default and typed override precedence.

Host tests should use deterministic clocks, sequence sources, and fixed test
executors. Zephyr integration tests should exercise ISR capture, target
synchronization, worker scheduling, shutdown ordering, and pressure under
concurrent producers.

Compile-fail tests are required for catalog, payload, source, ISR, recovery,
adapter, and Kconfig-capability violations.

## 46. Deferred Capabilities

The following remain deliberate later work:

- arbitrary-length encoded producer payloads;
- dynamically attached diagnostic observers;
- unbounded or host-backed history;
- causal parent/span tracing beyond correlation IDs;
- generalized complex payload reflection;
- C++26 reflection-derived event schemas and codecs;
- cross-device total occurrence ordering;
- runtime reconfiguration of capture and retention policy;
- persistent event database query languages;
- generalized alerting or supervisor behavior;
- automatic application recovery from event observations;
- large payload pools and stable shared handles.

Deferred features must preserve static registration, bounded producer work,
canonical event identity, and the prohibition on hidden domain behavior
subscriptions.

## 47. Rejected Alternatives

### 47.1 Use the bus for operational facts

Rejected because behavior coordination and diagnostic retention have different
ownership, storage, execution, and failure semantics.

### 47.2 Allow application event subscribers

Rejected because events would become a second hidden behavior bus and
diagnostic policy could change application correctness.

### 47.3 Make the event type also the payload

Rejected for events because event-level severity, domain, retention, recovery,
and adapter policy need a compact natural declaration home. The separate
payload keeps occurrence values free of schema metadata.

### 47.4 Hash names as stable IDs

Rejected because collisions, renames, and hashing choices would become
accidental storage and wire contracts.

### 47.5 Let call sites provide arbitrary source IDs

Rejected because it permits forged or unqueryable attribution. Source override
is typed and catalog validated.

### 47.6 Make observe return void

Rejected because bounded capture can fail and producers may need an immediate
domain-specific response.

### 47.7 Treat sampling as an error

Rejected because policy-intended suppression is a successful disposition, not
infrastructure failure.

### 47.8 Format and publish synchronously

Rejected because producer and ISR latency would depend on text formatting,
transport, storage, and sink behavior.

### 47.9 Invoke sinks under the capture lock

Rejected because it creates priority inversion, reentrancy, deadlock, and
unbounded critical-section latency.

### 47.10 Use one variable-sized producer byte ring

Rejected initially because deterministic concurrent and ISR reservation is more
complex than fixed complete-record ingress slots.

### 47.11 Use maximum-sized slots for retained history

Rejected because most event payloads are small and a compact byte ring retains
substantially more useful history for the same static memory.

### 47.12 Allow borrowed payload views

Rejected because deferred processing outlives the observation call and cannot
generally prove the borrowed object's lifetime.

### 47.13 Infer capture guarantee from severity

Rejected because importance does not create memory. Guarantees require explicit
reserved capacity and exhaustion behavior.

### 47.14 Emit an event when event capture fails

Rejected because the same unavailable capacity would recurse. Dedicated
counters and latches own infrastructure failure truth.

### 47.15 Infer recovery from silence

Rejected because absence may mean recovery, inactivity, disconnection, or
failed observation. Recovery is explicit.

### 47.16 Define consecutive as adjacent global records

Rejected because unrelated concurrent events would make the result meaningless
and scheduling dependent.

### 47.17 Automatically log every event

Rejected because structured capture remains useful independently, and high-rate
events can overwhelm textual diagnostics.

### 47.18 Automatically create public metrics for every event

Rejected because event accounting and product metric schema have different
ownership and exposure requirements.

### 47.19 Automatically expose every event through Remote

Rejected because registration is not authorization, external schema, session
capacity, or transport policy.

### 47.20 Give events a dedicated thread unconditionally

Rejected because shared execution can process ordinary deferred work without
one stack per subsystem. Dedicated execution remains an explicit future policy.

### 47.21 Universal event snapshot

Rejected because event descriptors, counters, sink records, and history have
different shapes, costs, and synchronization. Focused query surfaces remain
clearer.

## 48. Accepted Decisions

1. Events are typed structured operational facts.
2. Events remain useful without logs, metrics, persistence, or Remote.
3. Bus messages coordinate behavior; events record facts.
4. Events are not an application callback or subscription mechanism.
5. There is no public application `subscribe<Event>()` API.
6. The event declaration and payload are separate types.
7. Payload-free events declare `Payload = void`.
8. Authored metadata uses `solar::events::Descriptor`.
9. Descriptor metadata includes name, severity, and extensible domain.
10. Capture, retention, recovery, and adapters remain typed policy.
11. The conventional component contribution alias is `Events`.
12. Root-owned event declarations use `solar::Events`.
13. Contributions preserve semantic owner and registration origin.
14. Typed observation requires effective event catalog registration.
15. Every event resolves a stable explicit or manifest identity.
16. Name and compiler type hashing are not stable identity contracts.
17. Schema version remains separate from stable event identity.
18. Default occurrence source is the event's semantic owner.
19. Source override is typed and catalog validated.
20. Semantic owner and occurrence source remain distinct.
21. Initial severity is trace, informational, warning, error, and critical.
22. Severity does not select capture guarantee or retention.
23. Domains are extensible stable descriptor values.
24. The common API is `observe<Event>(payload)`.
25. `observe_from<Source, Event>` provides explicit attribution.
26. `try_observe` is strictly non-blocking.
27. ISR supports only `try_observe_isr` forms.
28. Observation returns `Result<Receipt, Error>`.
29. Sampling, rate limiting, and aggregation are successful dispositions.
30. Successful capture does not promise every downstream sink succeeds.
31. Timestamp is acquired at capture rather than processing.
32. Material event records receive a globally monotonic boot-local sequence.
33. Context kind is derived and cannot be forged by ordinary callers.
34. Correlation is optional and preserved through derived infrastructure.
35. Event payloads are initially bounded and trivially copyable.
36. Borrowed payload members are not part of the initial contract.
37. Raw trivial object bytes are not automatically a wire format.
38. Mutable runtime state belongs to the static Events facility.
39. Event declaration types do not own mutable queues or counters.
40. Capture uses fixed complete-record ingress slots.
41. Thread and ISR ingress capacity are separate.
42. Retained history uses a compact bounded variable-sized byte ring.
43. Capture performs no formatting, external sink I/O, or domain callbacks.
44. Deferred processing drains material records by sequence.
45. Deferred processing uses shared Solar execution by default.
46. Initial capture policies are every occurrence, sampling, rate limiting, and
    bounded aggregation.
47. Every occurrence means no intentional suppression, not infinite retention.
48. Keyed aggregation has fixed cardinality and explicit overflow.
49. Initial retention is transient, buffered, critical, and persistent.
50. Buffered retention is the default.
51. Critical retention requires reserved bounded resources and exhaustion
    behavior.
52. Ordinary history overflow evicts the oldest complete record.
53. Critical records use a protected partition or equivalent reservation.
54. History queries return copies or caller-owned bounded pages.
55. ISR capture is no-wait and never invokes deferred infrastructure inline.
56. Concurrent thread and ISR producers are supported.
57. No external observer executes under a capture or history lock.
58. Default ingress overflow rejects the newest observation.
59. Event capture failure updates counters and latches without recursive events.
60. Automatic per-event accounting is canonical event facility state.
61. Public metrics may derive selected accounting but do not own it.
62. Observers and sinks are statically configured infrastructure.
63. Observers may retain, aggregate, serialize, log, meter, or forward records.
64. Observers must not invoke application domain behavior.
65. Slow sinks use independent bounded admission.
66. One sink failure does not prevent attempts to deliver to other sinks.
67. Recovery is explicit through relation or state-transition events.
68. Silence never implies recovery.
69. Consecutive identity is event, source, and optional declared key.
70. Unrelated interleaved events do not reset a consecutive streak.
71. Resolution or explicit expiry resets condition state.
72. Event-to-metric integration is an explicit declarative adapter.
73. Event-to-log integration uses an internal non-recursive bridge.
74. Event-generated logs preserve event identity, source, and sequence.
75. Mandatory and forbidden logging policy outrank call-site intent.
76. Disabled optional sibling adapters do not disable event capture.
77. Requiring an unavailable sibling adapter is a configuration error.
78. The Events facility is included only when enabled and required.
79. Dependent components acquire an implicit Events facility dependency.
80. Events are available during dependent init, start, rollback, stop, and
    deinit.
81. The facility performs a final bounded synchronous shutdown drain.
82. Boot reports remain authoritative if the Events facility itself fails.
83. Kconfig owns build capability, hard ceilings, and defaults.
84. C++ types own schema, identity, source, policy, and adapter architecture.
85. Explicit event policy overrides blueprint configuration, then Kconfig.
86. Event runtime storage requires no dynamic allocation.
87. Remote exposure is explicit and never implied by registration.
88. Inspection uses focused descriptors, records, sink state, and history APIs.
89. There is no event facility object in the user-facing API.
90. The current synchronous format-and-publish implementation is replaced by
    split capture and deferred processing.

## 49. Open Questions

There are no blocking open questions for Phase 7.

Later specifications and implementation must refine without changing this
contract:

- exact Kconfig symbol names and numeric ceilings;
- final shared-executor registration syntax from Phase 9;
- exact metric adapter declarations from Phase 7;
- exact logging renderer and bridge declarations from Phase 8;
- persistent event store envelope and integrity algorithm;
- Remote event stream exposure and codec syntax;
- concrete monotonic and ISR timestamp backend;
- final bounded history page and cursor types;
- optional future causal parent/span metadata;
- generated schemas and codecs using future reflection.

These are extensions of the accepted identity, bounded capture, retention,
observability-only observer, lifecycle, and failure model.
