Yes, this distinction is worth making explicit and permanent:

> **Bus events coordinate behaviour. Observability events record facts. Logs explain things in prose.**

They may share transport machinery, buffers, sinks, filtering, and Remote exposure, but they should not share semantics.

A button press on the bus means:

```text
Someone may need to react to this.
```

An enqueue miss observed by the event system means:

```text
This happened, and the system should retain or report that fact.
```

A log means:

```text
Here is a human-oriented diagnostic message about what happened.
```

That gives Solar three deliberately different surfaces:

```cpp
solar::bus::emit<ButtonPressed>({...});

solar::events::observe<FrameDropped>({...});

solar::log::warn<RemoteService>(
    "Dropped frame of {} bytes for session {}",
    size,
    session);
```

They can sometimes occur together, but they are not interchangeable.

# 1. The semantic split

## Bus

The bus exists to produce application behaviour.

```cpp
solar::bus::emit<ButtonPressed>{
    .button = Button::A
};
```

Characteristics:

* has subscribers
* subscribers may perform work
* delivery policy matters
* may be inline, queued, coalesced, or delivered to a service mailbox
* can alter application state
* normally represents domain-level occurrences
* may be consumed and then forgotten

## Observability events

The event system exists to record structured operational facts.

```cpp
solar::events::observe<FrameDropped>({
    .session = session_id,
    .bytes = frame_size,
    .reason = DropReason::TxQueueFull
});
```

Characteristics:

* typed and structured
* should be cheap to record
* does not trigger arbitrary application handlers
* may feed counters, storage, Remote, diagnostics, alerting, or tracing
* often includes severity and operational metadata
* should remain meaningful without prose
* may be aggregated or rate-limited
* frequently comes from infrastructure itself

## Logs

Logs are generic human-readable diagnostics.

```cpp
solar::log::warn<RemoteService>(
    "TX queue full; dropping {}-byte stream frame",
    frame_size);
```

Characteristics:

* no event type needs to be declared
* arbitrary format string and arguments
* optimized for developer comprehension
* usually variable-length
* may contain contextual detail unsuitable for a rigid schema
* weaker for machine processing

The bus is a nervous system. Observability events are the flight recorder. Logs are the engineering notebook.

# 2. Events should not have application subscribers

I would avoid an API such as:

```cpp
solar::events::subscribe<FrameDropped, RestartRemote>();
```

because that quietly turns observability back into a control bus.

Instead, events should go to **observers or sinks**, where an observer is infrastructure:

```cpp
using EventSinks = solar::events::Sinks<
    solar::events::RemoteSink,
    solar::events::MetricsSink,
    solar::events::RingBufferSink<4096>>;
```

A sink may:

* serialize the event
* increment a counter
* retain it
* forward it to a file
* expose it over Remote
* trigger an external diagnostic alert

But it should not casually invoke domain operations.

There may eventually be a special “health policy” that escalates events into system actions, but that should be explicit:

```cpp
using HealthPolicy = solar::health::Policy<
    solar::health::Escalate<
        events::ControlDeadlineMiss,
        solar::health::After<3>,
        solar::health::Action::EnterSafeState>>;
```

That is not ordinary event subscription. It is a declared reliability policy.

# 3. Event definitions

A useful event type could look like:

```cpp
namespace app::events {

struct FrameDropped {
    static constexpr solar::events::Id id = 0x0201;
    static constexpr std::string_view name =
        "remote.frame.dropped";

    static constexpr solar::events::Severity severity =
        solar::events::Severity::Warning;

    struct Payload {
        std::uint16_t session;
        std::uint32_t bytes;
        DropReason reason;
    };
};

}
```

Usage:

```cpp
solar::events::observe<events::FrameDropped>({
    .session = session_id,
    .bytes = frame_size,
    .reason = DropReason::QueueFull
});
```

Or put fields directly on the event:

```cpp
struct FrameDropped {
    static constexpr events::Id id = 0x0201;
    static constexpr auto severity =
        events::Severity::Warning;

    std::uint16_t session;
    std::uint32_t bytes;
    DropReason reason;
};
```

Then:

```cpp
solar::events::observe(
    FrameDropped{
        .session = session_id,
        .bytes = frame_size,
        .reason = DropReason::QueueFull
    });
```

I slightly prefer:

```cpp
observe<EventType>(payload)
```

because it cleanly separates the event descriptor from its occurrence data. It also lets the descriptor carry metadata without embedding it into every record.

# 4. Common event metadata

Each recorded occurrence should get a common envelope:

```cpp
struct EventRecordHeader {
    events::Id id;
    events::Severity severity;
    ComponentId source;
    Timestamp timestamp;
    ContextKind context;
    std::uint16_t payload_size;
    std::uint32_t sequence;
};
```

Possible `ContextKind` values:

```cpp
enum class ContextKind : std::uint8_t {
    Thread,
    Isr,
    Kernel,
    Unknown
};
```

The source can come from the template argument:

```cpp
solar::events::observe<
    FrameDropped,
    RemoteService>(payload);
```

But that starts to look cumbersome.

You could instead let events define their normal owner:

```cpp
struct FrameDropped {
    using Source = RemoteFacility;
};
```

Or expose two forms:

```cpp
solar::events::observe<FrameDropped>(payload);

solar::events::observeFrom<
    RemoteService,
    FrameDropped>(payload);
```

The normal call should remain compact.

# 5. Event severity and classification

Severity is useful, but events also benefit from category or domain metadata.

For example:

```cpp
enum class Domain {
    Scheduling,
    Communication,
    Storage,
    Power,
    Safety,
    Device,
    Lifecycle,
    Resource
};
```

Then:

```cpp
struct DeadlineMissed {
    static constexpr auto domain =
        events::Domain::Scheduling;

    static constexpr auto severity =
        events::Severity::Warning;
};
```

Possible severities:

```cpp
Trace
Informational
Warning
Error
Critical
```

I would avoid making event severity identical to log level. They are related, but not always equivalent.

For example, an informational event may still be retained permanently because it marks a significant lifecycle transition:

```text
firmware booted
configuration migrated
safe state entered
```

# 6. Events can carry policy

Some useful event-level policy traits:

```cpp
struct FrameDropped {
    using Retention =
        events::Retention::Buffered;

    using RateLimit =
        events::RateLimit<100ms>;

    using Aggregation =
        events::AggregateCount<1s>;

    using RemoteAccess =
        remote::Observable;
};
```

You do not need all of this initially, but the model supports it.

Particularly useful policies are:

## Always record

```cpp
events::Always
```

For:

* boot failures
* safety state transitions
* storage corruption
* watchdog resets

## Rate-limited

```cpp
events::RateLimit<1s>
```

For repetitive warnings such as:

* queue full
* transient timeout
* sensor invalid sample

## Aggregated

Instead of producing 400 identical events:

```text
frame dropped
frame dropped
frame dropped
```

emit:

```text
remote.frame.dropped
count: 400
window: 1 second
```

For example:

```cpp
using Aggregation =
    events::CountBy<DropReason, 1s>;
```

## Sampled

```cpp
events::SampleEvery<100>
```

Useful for extremely frequent diagnostics.

# 7. Logs and events may be related, but neither should require the other

A warning event may optionally generate a textual log:

```cpp
solar::events::observe<FrameDropped>(payload);
```

A sink could render:

```text
[warning] remote.frame.dropped:
session=2 bytes=818 reason=queue_full
```

But Solar should not require a log message for every event.

Likewise, this:

```cpp
solar::log::warn<RemoteService>(
    "Remote client sent an unsupported protocol version {}",
    version);
```

does not necessarily deserve a formal event type.

A good rule:

> Create an event type when software or tooling may want to count, filter, graph, retain, compare, or act on the occurrence structurally.

Use a log when the value is primarily explanatory.

Often both are justified:

```cpp
solar::events::observe<ProtocolRejected>({
    .remote_major = version.major,
    .remote_minor = version.minor
});

solar::log::warn<RemoteService>(
    "Rejected remote protocol version {}.{}; supported major is {}",
    version.major,
    version.minor,
    supported_major);
```

The event is stable machine data. The log is richer commentary.

# 8. Sinks should be unified conceptually, not necessarily physically

You may have:

```cpp
using LogSinks = solar::log::Sinks<
    solar::log::Remote,
    solar::log::Uart<board::Console>,
    solar::log::File<SdLogFile>>;

using EventSinks = solar::events::Sinks<
    solar::events::Remote,
    solar::events::PersistentRing<EventFlash>,
    solar::events::Metrics>;
```

Some sink implementations can support both:

```cpp
struct RemoteDiagnosticsSink {
    static void consume(const LogRecordView&);
    static void consume(const EventRecordView&);
};
```

But routing policy should be configured independently.

For example:

* debug logs only to USB CDC
* warnings and errors to Remote
* all critical events to flash
* scheduling events to Remote and metrics
* verbose logs disabled in production
* events retained even when no Remote client is connected

A combined diagnostics configuration might be:

```cpp
using Diagnostics = solar::Diagnostics<
    solar::log::Config<
        solar::log::Level::Info,
        solar::log::Sinks<
            RemoteLogSink,
            UartLogSink>>,

    solar::events::Config<
        solar::events::Sinks<
            RemoteEventSink,
            EventHistory,
            MetricsEventSink>>>;
```

# 9. The event storage format

Events are naturally suitable for compact binary storage.

A record can be:

```text
[event header][typed payload bytes]
```

For example:

```cpp
struct EventRecordHeader {
    std::uint32_t event_id;
    std::uint32_t timestamp;
    std::uint16_t source_id;
    std::uint8_t severity;
    std::uint8_t flags;
    std::uint16_t payload_size;
};
```

Then the payload for `FrameDropped` could be only:

```cpp
struct Payload {
    std::uint16_t session;
    std::uint16_t reason;
    std::uint32_t bytes;
};
```

That is compact, deterministic, and easy to forward through Remote.

The host knows the schema associated with the event ID and can render it meaningfully.

# 10. Events from ISR context

Events are particularly valuable in ISR and kernel-adjacent code, but the ISR path must be constrained.

This should be valid:

```cpp
solar::events::observeIsr<EncoderOverrun>({
    .channel = channel,
    .count = count
});
```

The ISR implementation should only:

1. timestamp or capture a cycle count
2. write a fixed-size header and payload into an IRQ-safe ring
3. signal deferred processing
4. return

No formatting. No sink fan-out. No Remote serialization. No filesystem access.

You could make `observe<Event>()` automatically determine context, but a distinct `observeIsr()` API is clearer and allows stronger compile-time restrictions.

For example:

```cpp
template<typename Event>
concept IsrObservable =
    std::is_trivially_copyable_v<
        typename Event::Payload> &&
    sizeof(typename Event::Payload) <=
        CONFIG_SOLAR_ISR_EVENT_MAX_SIZE;
```

# 11. Should Solar build logs on Zephyr logging?

My view is:

> Use Zephyr logging as an implementation substrate where it helps, but do not let it define Solar’s logging architecture.

Zephyr’s current logging subsystem already supports deferred processing, compile-time and runtime filtering, multiple active backends, custom backends, and a custom frontend. Messages flow through a frontend and then active backends, which means a Solar-owned frontend/backend arrangement is technically viable. ([Zephyr Project Documentation][1])

That makes Zephyr attractive because it already solves:

* deferred log message handling
* log source/module registration
* runtime filtering
* backend activation
* panic and flush support
* formatting infrastructure
* ISR-compatible logging paths
* dictionary-based logging options
* rate-limited logging APIs

Zephyr also supports dictionary-based binary logging, which is relevant if Solar wants compact transport and host-side decoding rather than formatting every message on-device. ([Zephyr Project Documentation][2])

However, there are reasons not to make Zephyr the authoritative Solar model.

# 12. Where Zephyr logging fits well

Solar can expose:

```cpp
solar::log::warn<Source>(
    "Queue full: capacity={} pending={}",
    capacity,
    pending);
```

and internally adapt this to Zephyr’s logger.

`Source` can map to a Zephyr log source or module.

Solar can configure a custom Zephyr backend that forwards records into:

* Remote
* USB CDC
* UART
* an SD-card writer
* a diagnostic ring buffer

Zephyr’s backend API is explicitly designed to support custom backend instances. ([Zephyr Project Documentation][3])

A rough architecture:

```text
solar::log API
      ↓
Solar adapter/frontend
      ↓
Zephyr logger core
      ↓
Solar Zephyr backend
      ↓
Solar sink router
  ├─ Remote
  ├─ UART
  ├─ USB CDC
  └─ SD file
```

This gives Solar a stable API while Zephyr acts as the buffering/filtering engine.

# 13. Where Zephyr logging may fight Solar

The main concern is double architecture.

If Solar already wants to own:

* typed source identities
* variable-record buffering
* per-sink routing
* Remote binary frames
* structured arguments
* log retention
* dynamic log levels
* sink-specific filtering
* session-aware delivery
* its own lifecycle and diagnostics facility

then routing everything through Zephyr may make Zephyr a middleman that adds conversion and constraints without adding much value.

Potential friction includes:

* Solar’s type-level sources must map onto Zephyr’s macro-based source registration.
* Zephyr log messages have their own internal representation and lifecycle.
* Solar may want raw structured arguments while a backend receives a representation optimized for Zephyr’s output model.
* Per-session Remote filtering does not naturally belong inside Zephyr’s global backend model.
* Solar events are not logs and should not be forced through the logging subsystem.
* An SD sink may need batching and file-rotation policy that is better handled by Solar.
* You could end up buffering once in Zephyr and again in Remote.

The architectural smell would be:

```text
Solar formats → Zephyr buffers → Solar backend decodes → Solar buffers again
```

That is a little conveyor belt carrying the same parcel through three warehouses.

# 14. Three possible integration models

## Model A: Zephyr owns logging internals

```text
Solar API
  ↓
Zephyr logging
  ↓
Solar custom backend
  ↓
Solar sinks
```

Good when:

* you want Zephyr’s filtering and deferred machinery
* interoperability with Zephyr-native modules matters
* most logs are conventional formatted messages
* you do not mind adapting Solar sources to Zephyr sources

This is the most pragmatic first implementation.

## Model B: Solar owns logging, Zephyr logs are imported

```text
Solar API → Solar log core → Solar sinks
Zephyr logs → Solar Zephyr backend → Solar log core
```

Here Solar has its own log ring and record representation.

Zephyr-native code still uses `LOG_INF`, but your custom Zephyr backend converts those records into Solar records.

Advantages:

* Solar owns the canonical sink system
* Remote integration is direct
* non-Zephyr builds remain conceivable
* Solar sources and filters are unconstrained
* both Solar and Zephyr logs appear in one stream

Disadvantages:

* you reimplement buffering/filtering pieces
* converting Zephyr logs may lose some structure
* more code

Long-term, this is probably the cleanest architecture.

## Model C: Solar custom frontend replaces normal Zephyr handling

Zephyr allows a custom frontend, so Solar could intercept logging earlier and own how records are processed. ([Zephyr Project Documentation][1])

This is powerful, but it couples Solar closely to Zephyr logging internals and configuration. I would not begin here unless a prototype shows it provides exactly the representation you need.

# 15. My recommendation on Zephyr

I would use a staged approach.

### Initially

Use Zephyr logging underneath Solar:

```text
solar::log API
  ↓
Zephyr logger
  ↓
Solar backend
  ↓
Solar sinks
```

This gets you working quickly and preserves compatibility with Zephyr ecosystem logs.

### Architecturally

Design Solar’s sink interface and public record model so Zephyr is replaceable.

For example:

```cpp
struct LogRecordView {
    LogLevel level;
    SourceId source;
    Timestamp timestamp;
    std::span<const std::byte> encoded_message;
};
```

Sinks consume Solar records:

```cpp
template<typename Sink>
concept LogSink = requires(LogRecordView record) {
    Sink::write(record);
};
```

The Zephyr backend is merely one producer of those records.

Then later, Solar’s own frontend can bypass Zephyr for native Solar logs if there is a compelling reason.

That avoids prematurely rebuilding a logger while preventing Zephyr from hardening into an architectural prison wall.

# 16. What about logs from Zephyr itself?

This is one of the strongest reasons to integrate.

Zephyr drivers, networking stacks, USB, Bluetooth, storage, and your other modules may already produce Zephyr logs.

You probably want these to appear through the same Remote connection and configured sinks.

A Solar Zephyr backend can capture them and assign a source such as:

```text
zephyr.usb_cdc
zephyr.net_tcp
zephyr.fs
zephyr.driver.sensor
```

Solar-native logs can use your type source:

```cpp
solar::log::warn<RemoteService>(...);
```

The host then sees one diagnostic stream regardless of origin.

# 17. Source identity

Your API:

```cpp
solar::log::warn<Source>(...);
```

is excellent.

`Source` should map to stable metadata:

```cpp
template<typename Source>
struct LogSourceTraits {
    static constexpr SourceId id =
        stable_id<Source>;

    static constexpr std::string_view name =
        type_name<Source>;
};
```

For stable Remote schema, explicit IDs or registered names are safer than compiler-derived hashes:

```cpp
struct RemoteService {
    static constexpr solar::SourceId log_source_id =
        0x0312;

    static constexpr std::string_view name =
        "remote.service";
};
```

But requiring every source to manually define an ID is tedious.

A good compromise:

* automatic local source identity for filtering
* generated manifest or stable registered IDs for wire output
* explicit IDs only for public, long-lived diagnostic schemas

Logs do not need the same schema stability as structured events. Events should absolutely have stable IDs.

# 18. Modules and categories

The current model of module, category, text, and level is broadly sound, but I would reshape it around:

```text
source
domain
level
message
```

Where:

* `source` identifies who produced the log
* `domain` is optional classification
* `level` controls importance and filtering
* `message` is arbitrary human-oriented content

Example:

```cpp
solar::log::warn<
    RemoteService,
    solar::log::domain::Transport>(
        "TX queue full");
```

But that syntax may be too heavy.

Another form:

```cpp
solar::log::warn<RemoteService>(
    solar::log::domain::Transport,
    "TX queue full");
```

Or let source metadata define the normal domain.

I would not require categories on every call. Source plus level already covers most needs.

# 19. Log records should retain arguments where possible

Rather than immediately formatting:

```cpp
"TX queue full; dropping 512 bytes"
```

Solar or Zephyr may retain:

```text
format id: 0x91A3
arguments:
  uint32: 512
```

Then formatting happens:

* in deferred processing
* in the sink
* or on the host

This reduces producer latency and can greatly reduce bandwidth.

Zephyr’s dictionary-based logging is evidence that this model is practical in the ecosystem. ([Zephyr Project Documentation][2])

Solar could expose normal syntax:

```cpp
solar::log::warn<RemoteService>(
    "TX queue full; dropping {} bytes",
    bytes);
```

while the implementation records a format token and encoded arguments.

This is especially appealing for Remote.

For SD text logs, the sink may format locally.

For Remote, the sink may send dictionary records.

For UART debugging, the sink may render immediately or deferred.

# 20. Sink routing

A useful configuration model:

```cpp
using Logging = solar::Logging<
    solar::log::Sources<
        solar::log::DefaultLevel<LogLevel::Info>,
        solar::log::LevelFor<
            RemoteService,
            LogLevel::Debug>,
        solar::log::LevelFor<
            ControlLoop,
            LogLevel::Warn>>,

    solar::log::Sinks<
        solar::log::Sink<
            RemoteLogSink,
            solar::log::MinLevel<LogLevel::Info>>,

        solar::log::Sink<
            UsbCdcLogSink,
            solar::log::MinLevel<LogLevel::Debug>>,

        solar::log::Sink<
            SdFileLogSink,
            solar::log::MinLevel<LogLevel::Warn>>>>;
```

Event routing separately:

```cpp
using Observability = solar::Events<
    solar::events::Sinks<
        solar::events::Sink<
            RemoteEventSink,
            solar::events::MinimumSeverity<
                events::Severity::Informational>>,

        solar::events::Sink<
            PersistentEventSink,
            solar::events::MinimumSeverity<
                events::Severity::Warning>>,

        solar::events::Sink<
            MetricsEventSink>>>;
```

Per-sink filtering is important. A UART developer stream and persistent SD record should not necessarily receive the same volume.

# 21. Events as metric sources

Many observability events naturally feed metrics.

For example:

```cpp
struct FrameDropped {
    std::uint32_t bytes;
    DropReason reason;
};
```

The metrics sink can automatically maintain:

```text
remote.frames_dropped.total
remote.bytes_dropped.total
remote.frames_dropped.by_reason.queue_full
```

Similarly:

```cpp
DeadlineMissed
```

can produce:

```text
scheduler.deadline_misses.total
scheduler.maximum_lateness_us
```

This is another reason observability events should be structured.

The source component does not need to separately emit an event, update a counter, and write a log. The event can be the canonical operational fact, and sinks derive views from it.

# 22. Event history and snapshots

Remote should support more than live subscription.

Useful RPCs:

```text
events.list_types
events.subscribe
events.history
events.clear_history
events.statistics
```

A fixed history ring can retain recent records:

```cpp
using EventHistory =
    solar::events::History<
        solar::Bytes<8192>,
        solar::overflow::DropOldest>;
```

When a host connects after a fault, it can retrieve what happened before connection.

For critical events, you may persist a smaller flash-backed history:

```cpp
using CriticalHistory =
    solar::events::PersistentHistory<
        FlashPartition,
        solar::events::MinimumSeverity<Critical>>;
```

This begins to resemble an embedded Windows Event Log, which seems close to your goal.

# 23. A coherent public shape

Logs:

```cpp
solar::log::debug<NavigationService>(
    "Candidate path count: {}",
    candidates.size());

solar::log::info<System>(
    "Boot complete in {} ms",
    boot_time_ms);

solar::log::warn<RemoteService>(
    "Session {} is falling behind",
    session_id);

solar::log::error<Lidar>(
    "Read failed with error {}",
    error);
```

Observed events:

```cpp
solar::events::observe<DeadlineMissed>({
    .task = task_id,
    .deadline_us = deadline_us,
    .completed_us = completed_us
});

solar::events::observe<FrameDropped>({
    .session = session_id,
    .bytes = bytes,
    .reason = DropReason::QueueFull
});

solar::events::observe<OvercurrentDetected>({
    .device = device_id,
    .measured_ma = measured,
    .limit_ma = limit
});
```

Bus:

```cpp
solar::bus::emit<ButtonPressed>({
    .button = Button::A
});
```

The three APIs are visually related, but semantically unmistakable.

# 24. Recommended internal architecture

```text
                    Producers
       ┌──────────────┼───────────────┐
       │              │               │
     Bus API       Event API        Log API
       │              │               │
  dispatch routes   event ring     log frontend
       │              │               │
 application work   event router   log processing
                      │               │
                event sinks       log sinks
                      │               │
                  ┌───┴───────────────┴───┐
                  │ shared output targets  │
                  ├─ Remote                │
                  ├─ USB CDC               │
                  ├─ UART                  │
                  ├─ SD/file               │
                  └─ persistent ring       │
```

Bus remains separate all the way down.

Events and logs may converge at physical outputs, but only after retaining their different record types.

Remote can expose them as separate topics:

```text
diagnostics.logs
diagnostics.events
```

That lets the host treat them differently.

# 25. My strongest recommendations

1. **Keep bus, events, and logs as three independent first-class concepts.**

2. **Do not allow ordinary event observers to become behaviour handlers.** Event sinks observe, aggregate, retain, or export.

3. **Make events typed, compact, schema-driven records with stable IDs.**

4. **Make logs source-tagged, leveled, variable-length, and human-oriented.**

5. **Allow events to derive metrics and rendered text through sinks.**

6. **Use Zephyr logging initially, especially to capture ecosystem logs and benefit from deferred processing and filtering.** Zephyr is designed around frontends and active backends, including custom ones. ([Zephyr Project Documentation][1])

7. **Do not expose Zephyr logging directly as Solar’s architecture.** Own the public API, sink contracts, and Remote representation.

8. **Implement a Solar Zephyr backend that feeds the Solar sink router.** Keep the design open to a native Solar frontend later.

9. **Give ISR events and logs constrained, allocation-free producer paths.**

10. **Store events and variable-length logs in separate byte-oriented rings.** Their overflow and retention requirements differ.

This would give Solar a diagnostics system with three lenses: the bus tells the system what to do, events tell tooling what happened, and logs tell the engineer the story behind it.

Yes, I think your instinct is right:

> Keep events, logs, and metrics as distinct systems, but let the event pipeline derive logs and metrics from an observed event through explicit policy.

That gives you one canonical operational fact:

```cpp
solar::events::observe<FrameDropped>(payload);
```

and then lets the observability machinery decide whether that occurrence should:

* be retained as an event
* increment metrics
* emit a textual log
* be forwarded through Remote
* be persisted
* be rate-limited or aggregated

I would not introduce a public `Record` abstraction. Internally there may be a processing record or envelope, but the user should still think, “I am observing an event.”

# 1. Events should be the canonical structured fact

Suppose:

```cpp
struct FrameDropped {
    static constexpr auto id =
        solar::events::id<0x0201>;

    static constexpr auto severity =
        solar::events::Severity::Warning;

    struct Payload {
        RemoteSessionId session;
        std::uint32_t bytes;
        DropReason reason;
    };
};
```

The producer does:

```cpp
solar::events::observe<FrameDropped>({
    .session = session,
    .bytes = frame_size,
    .reason = DropReason::QueueFull
});
```

That one observation should be enough to feed every downstream diagnostic representation.

The producer should not normally need to write:

```cpp
events::observe<FrameDropped>(...);
metrics::increment<FrameDrops>();
log::warn<RemoteService>(...);
```

That duplicates policy and invites drift.

Instead:

```text
FrameDropped observed
        ↓
event policy processing
        ├─ retain event
        ├─ increment metrics
        ├─ optionally emit log
        └─ route to configured event sinks
```

The event remains the source of truth.

# 2. Use event-level observability policy

An event definition can declare its normal behavior:

```cpp
struct FrameDropped {
    static constexpr auto severity =
        events::Severity::Warning;

    struct Payload {
        RemoteSessionId session;
        std::uint32_t bytes;
        DropReason reason;
    };

    using Observability = events::Policy<
        events::metrics::Count,
        events::metrics::Sum<&Payload::bytes>,
        events::logging::When<
            events::conditions::Every<3>>,
        events::retention::Buffered>;
};
```

That is one possible expressive style, though I would keep the initial syntax simpler.

Perhaps:

```cpp
struct FrameDropped {
    static constexpr auto severity =
        events::Severity::Warning;

    struct Payload {
        RemoteSessionId session;
        std::uint32_t bytes;
        DropReason reason;
    };

    using Metrics = events::metrics::Policy<
        events::metrics::Count,
        events::metrics::Sum<&Payload::bytes>>;

    using Logging = events::logging::AfterConsecutive<3>;

    static void formatLog(
        log::Writer& out,
        const Payload& payload) {

        out.warn(
            "Dropped {} byte frame for session {}: {}",
            payload.bytes,
            payload.session,
            payload.reason);
    }
};
```

This is easier to understand than a single giant policy typelist.

The useful separation is:

* **event metadata**
* **event-to-metrics policy**
* **event-to-log policy**
* **event formatting**

# 3. Do not make the event handler itself write the log directly

You mentioned an event defining a standard handler that emits a log.

Conceptually that is correct, but I would avoid calling it a “handler,” because handlers imply immediate behavior and can blur the line with the bus.

Instead, give the event a rendering or logging adapter:

```cpp
struct FrameDropped {
    // ...

    static void log(
        const Payload& value) {

        solar::log::warn<RemoteService>(
            "Dropped {} byte frame for session {} because {}",
            value.bytes,
            value.session,
            value.reason);
    }
};
```

Then the event processor invokes that only when policy says it should.

Better yet, avoid recursively calling the public log API from inside the event processor. Have it produce a log entry directly:

```cpp
static void writeLog(
    log::RecordBuilder& builder,
    const Payload& value) {

    builder.warn<RemoteService>(
        "Dropped {} byte frame for session {} because {}",
        value.bytes,
        value.session,
        value.reason);
}
```

Why? Because this prevents weird pipeline recursion:

```text
event observed
  → event emits log
    → logging observes internal failure
      → event emitted
        → event emits log
```

A direct internal bridge is safer than re-entering the entire public logging frontend.

# 4. Call-site overrides should be small and explicit

Your idea of overriding default log behavior at the observation site is useful.

I would support a compact options object:

```cpp
solar::events::observe<FrameDropped>(
    payload,
    events::options::noLog);
```

Or:

```cpp
solar::events::observe<FrameDropped>(
    payload,
    events::Options{
        .logging = events::LoggingOverride::Suppress
    });
```

Other cases:

```cpp
events::options::forceLog
events::options::noMetrics
events::options::noRetention
events::options::criticalPath
```

But be cautious. Too many per-call overrides can undermine centralized policy.

A sensible minimal set is:

```cpp
enum class LoggingOverride {
    Default,
    Suppress,
    Force
};
```

Then:

```cpp
struct ObserveOptions {
    LoggingOverride logging =
        LoggingOverride::Default;
};
```

Usage:

```cpp
events::observe<FrameDropped>(
    payload,
    {
        .logging =
            events::LoggingOverride::Suppress
    });
```

This is more verbose than a tag, but much easier to extend without API explosion.

You could provide helper constants:

```cpp
events::observe<FrameDropped>(
    payload,
    events::suppressLog);
```

# 5. Call-site overrides should not bypass safety policy

Suppose a critical event normally logs:

```cpp
struct BatteryThermalRunaway {
    static constexpr auto severity =
        Severity::Critical;
};
```

Should the caller be allowed to suppress it?

Probably not always.

You can distinguish:

```cpp
events::logging::DefaultOn
events::logging::DefaultOff
events::logging::Mandatory
events::logging::Forbidden
```

Then `Suppress` can override `DefaultOn`, but not `Mandatory`.

Conceptually:

```cpp
constexpr bool shouldLog(
    EventPolicy policy,
    LoggingOverride override) {

    switch (policy) {
    case Mandatory:
        return true;

    case Forbidden:
        return false;

    case DefaultOn:
        return override != Suppress;

    case DefaultOff:
        return override == Force;
    }
}
```

That gives event definitions authority over important guarantees.

# 6. Conditional logging should be a first-class policy system

You are right that this should be more capable than a boolean.

Useful conditions include:

```cpp
events::logging::Always
events::logging::Never
events::logging::AfterConsecutive<3>
events::logging::EveryNth<100>
events::logging::RateLimited<1s>
events::logging::OnFirst
events::logging::OnTransition
events::logging::AboveSeverity<Severity::Warning>
events::logging::When<Predicate>
```

You may also want combinations:

```cpp
using Logging = events::logging::AllOf<
    events::logging::AfterConsecutive<3>,
    events::logging::RateLimited<1s>>;
```

Or:

```cpp
using Logging = events::logging::AnyOf<
    events::logging::OnFirst,
    events::logging::EveryNth<100>>;
```

This gives you very expressive behavior without moving condition logic into producers.

# 7. “Consecutive” needs a defined meaning

For:

```cpp
AfterConsecutive<3>
```

you need to decide what breaks the streak.

Possibilities:

* another event of a different type occurs
* an explicit success event occurs
* a time window expires
* the payload key changes
* the same source must emit it repeatedly

For example, three consecutive motor overcurrent warnings should probably be tracked per motor:

```cpp
struct OvercurrentWarning {
    struct Payload {
        MotorId motor;
        std::uint16_t measured_ma;
    };

    using Logging =
        events::logging::AfterConsecutive<
            3,
            events::key<&Payload::motor>>;
};
```

Then left and right motors have independent streaks.

Likewise:

```cpp
using Logging =
    events::logging::AfterConsecutiveWithin<
        3,
        500ms,
        events::key<&Payload::motor>>;
```

That means three events for the same motor within half a second.

You do not need this complexity in version one, but design the policy state so it can be keyed.

# 8. Add explicit recovery or resolution events

Many observability conditions are more meaningful as state transitions than isolated warnings.

For example:

```cpp
DeadlineMissed
DeadlineRecovered
```

or:

```cpp
QueueCongested
QueueHealthy
```

Then event policy can log only transitions:

```cpp
using Logging =
    events::logging::OnStateTransition;
```

This avoids log spam:

```text
queue full
queue full
queue full
queue full
queue full
```

and produces:

```text
Remote TX queue entered congested state
Remote TX queue recovered after 438 ms
```

You could encode a common stateful event shape:

```cpp
struct TxQueueHealthChanged {
    enum class State {
        Healthy,
        Congested
    };

    struct Payload {
        RemoteSessionId session;
        State state;
        std::uint16_t depth;
    };

    using Logging =
        events::logging::OnTransition;
};
```

That is often better than trying to infer recovery from the absence of repeated events.

# 9. Metrics integration should be declarative

An event can contribute several metrics.

For `FrameDropped`:

```cpp
using Metrics = events::metrics::Policy<
    events::metrics::Counter<
        "remote.frames_dropped">,

    events::metrics::Sum<
        "remote.bytes_dropped",
        &Payload::bytes>,

    events::metrics::CounterBy<
        "remote.frames_dropped.by_reason",
        &Payload::reason>>;
```

That is conceptually attractive, although compile-time string handling may make the syntax cumbersome.

A type-based metric definition is probably cleaner:

```cpp
namespace metrics {

struct RemoteFramesDropped {
    using Type = Counter;
};

struct RemoteBytesDropped {
    using Type = Counter;
};

template<DropReason Reason>
struct RemoteDropsByReason {
    using Type = Counter;
};

}
```

Then:

```cpp
struct FrameDropped {
    using Metrics = events::metrics::Map<
        events::metrics::Increment<
            metrics::RemoteFramesDropped>,

        events::metrics::Add<
            metrics::RemoteBytesDropped,
            &Payload::bytes>,

        events::metrics::IncrementBy<
            metrics::RemoteDropsByReason,
            &Payload::reason>>;
};
```

That preserves type-level identity across the whole architecture.

# 10. Metrics can also be derived through a sink

There are two ways to integrate metrics.

## Event-owned mapping

The event declares its metric consequences:

```cpp
struct FrameDropped {
    using Metrics = /* mappings */;
};
```

Advantages:

* metric meaning stays near event definition
* easy to inspect statically
* no separate central mapping table
* event fully describes its operational semantics

## Metrics sink mapping

A metrics sink receives the event and knows how to map it:

```cpp
template<>
struct MetricsObserver<FrameDropped> {
    static void observe(
        const FrameDropped::Payload& p) {

        Metrics::increment<RemoteFramesDropped>();
        Metrics::add<RemoteBytesDropped>(p.bytes);
    }
};
```

Advantages:

* event definitions remain lightweight
* metrics logic lives in the metrics subsystem
* easier to vary metrics policy between builds

I would support an event-level adapter type:

```cpp
struct FrameDropped {
    using MetricsAdapter =
        metrics::adapters::FrameDropped;
};
```

Then:

```cpp
struct metrics::adapters::FrameDropped {
    static void apply(
        const events::FrameDropped::Payload&);
};
```

That keeps definitions readable while retaining static association.

# 11. Some metrics should be automatic for every event

The event system can maintain generic metrics without event-specific policy:

```text
events.observed.total
events.observed.by_type
events.dropped.total
events.processing_latency
events.buffer_utilization
events.rate_limited.total
events.logs_generated.total
```

For each event type:

```text
events.FrameDropped.count
events.FrameDropped.last_timestamp
```

You may not expose every per-type count as a permanent metric, because that can bloat the registry. But at least internally, counts are useful for diagnostics and policy.

Event-specific metrics are then additional:

```text
remote.bytes_dropped.total
scheduler.maximum_lateness_us
motor.overcurrent.maximum_ma
```

# 12. The event processor should have stages

A clean processing pipeline might be:

```text
observe<Event>(payload)
        ↓
construct event occurrence
        ↓
fast producer-side validation
        ↓
enqueue into event buffer
        ↓
event service drains record
        ↓
update event statistics
        ↓
apply aggregation/rate policy
        ↓
apply metric adapters
        ↓
evaluate log policy
        ↓
generate log record if required
        ↓
route event to event sinks
```

This makes the event service the right place for:

* consecutive occurrence tracking
* rate limiting
* aggregation
* metric derivation
* conditional log generation
* sink routing

The producer remains extremely cheap.

# 13. Decide what must happen synchronously

Most event processing should be deferred, but some data may be lost if you only enqueue pointers or views.

So `observe()` should generally copy the typed payload into bounded storage.

For small fixed payloads:

```cpp
events::observe<DeadlineMissed>({
    .task = task,
    .lateness_us = lateness
});
```

the whole occurrence can be copied.

Then metrics and logs happen later.

You may want an explicit immediate event mode for a small subset:

```cpp
using Processing =
    events::processing::Immediate;
```

But I would avoid that initially. A single deferred event path is much easier to reason about.

# 14. Logging generated from events should preserve linkage

When an event generates a log, include the originating event ID and occurrence sequence:

```cpp
struct LogRecordHeader {
    LogLevel level;
    SourceId source;
    Timestamp timestamp;

    std::optional<EventId> origin_event;
    std::optional<EventSequence> origin_sequence;
};
```

Then Remote tooling can show:

```text
Event: remote.frame.dropped
  session: 2
  bytes: 818
  reason: queue_full

Generated log:
  "Dropped 818 byte frame for session 2 because TX queue was full"
```

This also helps prevent double counting. Tooling knows the event and log describe the same underlying occurrence.

# 15. Avoid emitting event-generated logs back through event observation

You need a one-way bridge:

```text
events → logs
```

not:

```text
events ↔ logs
```

The logging subsystem may itself observe internal events such as:

```cpp
LogBufferOverflow
LogSinkFailed
```

but those events should not generate normal logs through the same failing path.

For example:

```cpp
struct LogBufferOverflow {
    using Logging =
        events::logging::Never;
};
```

Otherwise:

```text
log buffer overflows
→ event observed
→ event generates log
→ log buffer overflows
→ event observed
```

A beautifully engineered infinite scream.

Infrastructure events should declare whether they are permitted to generate logs.

# 16. Default formatting can be generated or custom

For some events, custom formatting is useful:

```cpp
struct DeadlineMissed {
    static void writeLog(
        log::RecordBuilder& out,
        const Payload& p) {

        out.warn<Scheduler>(
            "Task {} missed deadline by {} us",
            p.task,
            p.lateness_us);
    }
};
```

For simple events, Solar could auto-render fields:

```text
remote.frame.dropped:
  session=2
  bytes=818
  reason=queue_full
```

That requires field reflection or schema descriptors.

For example:

```cpp
struct FrameDropped {
    struct Payload {
        SessionId session;
        std::uint32_t bytes;
        DropReason reason;

        using Schema = solar::Fields<
            solar::Field<"session", &Payload::session>,
            solar::Field<"bytes", &Payload::bytes>,
            solar::Field<"reason", &Payload::reason>>;
    };
};
```

Then default event logging and Remote rendering can share the schema.

This may be worth doing eventually because Remote already benefits from typed schemas.

For the first version, require custom log rendering only for events that log.

# 17. Dedicated daemons fit this very well

Your facility/service split makes sense:

```text
Logging facility
  ├─ producer API
  ├─ buffers
  ├─ source registry
  └─ sink definitions

Logging daemon
  ├─ deferred formatting
  ├─ filtering
  ├─ routing
  └─ sink draining
```

Likewise:

```text
Events facility
  ├─ observe API
  ├─ event registry
  ├─ event rings
  └─ policy metadata

Events daemon
  ├─ aggregation
  ├─ consecutive tracking
  ├─ metrics derivation
  ├─ log derivation
  └─ sink routing
```

And potentially:

```text
Bus facility
  ├─ emit API
  ├─ routes
  └─ subscriber queues

Bus daemon
  ├─ deferred dispatch
  └─ work scheduling
```

Though the bus daemon may really be one or more executors rather than a single central service.

# 18. Kconfig versus explicit graph inclusion

These facilities are fundamental enough that automatic inclusion via Kconfig is attractive:

```text
CONFIG_SOLAR_LOGGING=y
CONFIG_SOLAR_EVENTS=y
CONFIG_SOLAR_REMOTE=y
```

Then Solar injects the relevant internal nodes.

That gives users:

```cpp
using System = solar::System<
    Board,
    Devices,
    Services>;
```

without writing:

```cpp
solar::Facilities<
    Logging,
    Events,
    Metrics>
```

I would separate **framework daemons** from **application graph components**.

For example:

```text
Application graph:
  LeftMotor
  Navigation
  RemoteTransport

Solar infrastructure graph:
  LogDaemon
  EventDaemon
  DefaultExecutor
  TimerDaemon
```

Both can be compiled into one resolved graph internally, but the user does not have to declare framework plumbing manually.

A useful model:

```cpp
using System = solar::System<
    AppComponents...,
    solar::Features<
        solar::feature::Remote,
        solar::feature::Events>>;
```

with Kconfig providing defaults.

Kconfig decides availability and broad configuration:

```text
CONFIG_SOLAR_EVENTS=y
CONFIG_SOLAR_EVENT_BUFFER_SIZE=4096
```

The C++ system definition decides application-level customization:

```cpp
using Observability = solar::Events<
    EventPolicies,
    EventSinks>;
```

If no explicit C++ event configuration is present, Solar supplies defaults from Kconfig.

That is a strong compromise.

# 19. A possible event API

Event definition:

```cpp
struct DeadlineMissed {
    static constexpr events::Id id{0x0101};

    static constexpr auto severity =
        events::Severity::Warning;

    struct Payload {
        TaskId task;
        std::uint32_t lateness_us;
    };

    using Metrics =
        scheduler::DeadlineMissMetrics;

    using Logging =
        events::logging::AllOf<
            events::logging::AfterConsecutive<3>,
            events::logging::RateLimited<1s>>;

    static void writeLog(
        log::RecordBuilder& out,
        const Payload& p) {

        out.warn<Scheduler>(
            "Task {} repeatedly missed its deadline; latest miss was {} us",
            p.task,
            p.lateness_us);
    }
};
```

Normal observation:

```cpp
events::observe<DeadlineMissed>({
    .task = task_id,
    .lateness_us = lateness
});
```

Suppress default log for a known artificial test:

```cpp
events::observe<DeadlineMissed>(
    {
        .task = task_id,
        .lateness_us = lateness
    },
    events::suppressLog);
```

Force a log despite a normally silent policy:

```cpp
events::observe<QueueDepthExceeded>(
    payload,
    events::forceLog);
```

# 20. My recommended rules

I would lock in these design principles:

1. **An observed event is the canonical structured operational fact.**

2. **Logs and metrics may be derived from events.** Producers should not duplicate all three manually.

3. **Event-to-log behavior is policy-driven, not hardwired into `observe()`.**

4. **An event may define a standard log renderer.** The event service invokes it after evaluating policy.

5. **Call sites may request `Default`, `Suppress`, or `Force` logging.** Mandatory and forbidden policies remain authoritative.

6. **Conditional policies are stateful and evaluated by the event daemon.** Producers should remain cheap and mostly stateless.

7. **Metric mapping should be statically associated with event types.** Either directly or through a named adapter.

8. **Event-generated logs must carry origin metadata.**

9. **Infrastructure failure events must be able to forbid log generation.** This prevents recursive failure loops.

10. **Do not expose a new public `Record` concept.** Internal envelopes are fine, but users observe events, write logs, and update metrics.

This creates a very coherent observability model: events state what occurred, metrics summarize how often and how severely, and logs translate selected occurrences into language for humans.
