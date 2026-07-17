# Unified Logging

Date: 2026-07-15

Status: accepted design

Owning phase: Phase 8

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

## 1. Purpose

This specification defines Solar's unified logging subsystem and its integration
with Zephyr-native logging.

Logs provide bounded human-oriented diagnostic explanation. Solar owns one
canonical log stream while preserving logs produced by Zephyr drivers,
networking, storage, USB, Bluetooth, and other ecosystem modules.

It establishes:

- source, domain, level, message, callsite, correlation, and origin vocabulary;
- concise typed `debug`, `info`, `notice`, `warn`, `error`, and `fatal` APIs;
- automatic component source identity and contributed non-component sources;
- compile-time, ingress-runtime, and per-sink filtering;
- compile-time-checked brace formatting and retained bounded arguments;
- canonical variable-length records with several payload encodings;
- one bounded multi-producer ingress with priority reservation;
- explicit non-waiting and ISR-safe frontends;
- a Solar-owned Zephyr custom frontend in frontend-only mode;
- ingestion of Zephyr source metadata, `cbprintf` packages, hexdumps, and
  redirected `printk` output;
- deferred processing, sink routing, formatting, and retained history;
- early-boot capture, bounded flush, shutdown, panic, and fatal behavior;
- event-generated logs without event/log recursion;
- Remote consumption without Remote becoming canonical storage;
- Kconfig capability and C++ policy responsibilities;
- migration from Solar's current direct synchronous logger.

The common path remains concise:

```cpp
solar::log::warn<RemoteService>(
    "Session {} is falling behind", session);
```

No logger object, logger alias, system object, context, sink, or Zephyr module
macro appears at ordinary Solar call sites.

## 2. Non-Goals

Logs are not:

- structured observability events;
- application bus messages;
- application commands or callbacks;
- runtime parameters;
- numeric metric instruments;
- canonical component state;
- an unbounded audit database;
- a general event history;
- a runtime source registry;
- an application behavior subscription surface;
- automatically durable;
- automatically exposed through Remote;
- guaranteed delivery after hardware or transport failure;
- a reason to format text in an ISR;
- a replacement for Zephyr's existing ecosystem logging call sites.

Logs explain. Events record structured facts. Metrics retain numeric state. The
three may be connected by explicit infrastructure adapters without sharing
canonical storage or semantics.

## 3. Architectural Decision

### 3.1 Solar owns the canonical core

Solar owns:

- canonical log record semantics;
- one ingress sequence;
- bounded ingress storage;
- source and domain normalization;
- correlation and event linkage;
- retained history;
- sink routing and formatting;
- drop, truncation, sink, and panic records;
- Remote adapter input.

### 3.2 Zephyr custom frontend

Solar installs a Zephyr custom logging frontend with frontend-only operation:

```text
Solar log API -------------------+
                                  v
Zephyr LOG_* -> Solar frontend -> Solar ingress ring
                                  |
                                  v
                           Solar log processor
                                  |
                    +-------------+-------------+
                    |             |             |
                  history       console       Remote adapter
```

The Zephyr default log buffer and backend fan-out are absent in the normal
Solar architecture. Zephyr-native calls still retain Zephyr compile-time and
frontend runtime filtering, source descriptors, `cbprintf` packaging, hexdump
support, and panic notification.

### 3.3 Why frontend-only

Frontend-only mode provides:

- one canonical sequence for Solar and Zephyr records;
- one pending-record buffer rather than Zephyr buffering followed by mandatory
  Solar copying;
- timestamp capture at the original call;
- direct normalized source and context capture;
- Solar-owned sink and Remote semantics;
- no format/decode/reformat conveyor between two logger cores.

### 3.4 Compatibility mode

A later Zephyr-backend import mode may support applications that already own a
custom frontend. It is not the primary architecture and may incur a second
bounded copy and weaker cross-origin ordering.

## 4. Canonical Vocabulary

The public vocabulary is:

- **log**: one human-oriented diagnostic message occurrence;
- **source**: the registered component or source descriptor that produced it;
- **domain**: optional typed classification of the message subject;
- **level**: filtering and importance classification;
- **callsite**: one source location and static format origin;
- **message**: format plus retained arguments or copied text;
- **record**: complete canonical header and encoded payload;
- **origin**: native Solar, Zephyr, event bridge, redirected print, or internal;
- **correlation**: optional identifier linking related diagnostics;
- **sink**: static infrastructure destination for immutable records;
- **renderer**: conversion from encoded record to bounded output bytes;
- **history**: optional retained complete-record byte ring;
- **panic mode**: one-way emergency synchronous logging state for the current
  boot;
- **fatal policy**: terminal action after emergency capture and drain.

The term **backend** is reserved for Zephyr integration or a physical writer
implementation. Solar's architecture-level destination is a sink.

## 5. Public API

### 5.1 Common levels

```cpp
solar::log::debug<NavigationService>(
    "Candidate path count: {}", candidates.size());

solar::log::info<System>(
    "Boot complete in {} ms", boot_time_ms);

solar::log::notice<RemoteService>(
    "Diagnostic session {} connected", session);

solar::log::warn<RemoteService>(
    "Session {} is falling behind", session);

solar::log::error<Lidar>(
    "Read failed: {}", error);
```

### 5.2 Domain override

The common call omits a domain. A typed domain may be selected when useful:

```cpp
solar::log::warn<
    RemoteService,
    solar::log::domain::Transport>(
        "TX queue full: {} pending", pending);
```

### 5.3 Correlation

Explicit correlation uses a small options value without changing source type:

```cpp
solar::log::info<RemoteService>(
    solar::log::correlated(request_id),
    "Handling request {}", request_id);
```

An optional thread-local correlation scope may be added later, but explicit
correlation remains the canonical async-safe form.

### 5.4 Return type

Non-fatal logging returns:

```cpp
solar::Result<solar::log::Receipt, solar::log::Error>
```

The result is intentionally not `[[nodiscard]]`. Best-effort callers may ignore
it. Code that requires diagnostic capture may inspect it.

### 5.5 Non-waiting forms

```cpp
solar::log::try_debug<Source>(...);
solar::log::try_info<Source>(...);
solar::log::try_notice<Source>(...);
solar::log::try_warn<Source>(...);
solar::log::try_error<Source>(...);
```

`try_` means no waiting for ingress capacity or synchronization. It does not
skip registration, formatting validation, filtering, argument ownership, or
context checks.

## 6. Levels

The ordered Solar levels are:

```cpp
solar::log::Level::Debug
solar::log::Level::Info
solar::log::Level::Notice
solar::log::Level::Warning
solar::log::Level::Error
solar::log::Level::Fatal
```

Their meanings are:

- **Debug**: developer-oriented detail useful during diagnosis;
- **Info**: ordinary operational progress and state explanation;
- **Notice**: normal but operationally significant information worth separating
  from routine chatter;
- **Warning**: unusual or degraded behavior that may recover;
- **Error**: an operation or capability failed;
- **Fatal**: the system cannot safely continue under the selected fatal policy.

`Notice` sits between `Info` and `Warning` for filtering. It maps naturally to
RFC 5424 Notice in syslog output.

Zephyr-native levels map as follows:

```text
LOG_DBG -> Debug
LOG_INF -> Info
LOG_WRN -> Warning
LOG_ERR -> Error
```

Zephyr has no distinct native Notice or Fatal level. Imported records are never
silently promoted into either.

## 7. Fatal API

`fatal` is a terminal operation, not merely another best-effort level:

```cpp
[[noreturn]]
solar::log::fatal<MotorController>(
    "Control invariant violated: {}", reason);
```

It performs:

1. emergency capture of the Fatal record;
2. one-way transition into panic mode;
3. synchronous draining of already accepted records through panic-safe sinks;
4. bounded sink flush;
5. invocation of the configured fatal policy.

The production default fatal policy is Zephyr kernel panic and does not return.
Tests may select an explicit test terminal hook. A policy that simply continues
normal production execution is not a valid default.

Recoverable severe conditions use `error` or structured events.

There is no `try_fatal`; a terminal call cannot meaningfully promise to continue
if logging capture fails.

## 8. Component Sources

Every effective registered Solar component is automatically a valid log source:

```cpp
solar::log::warn<RemoteService>(...);
```

The component descriptor supplies default source name, local identity, semantic
owner, and optional stable external identity.

No `Logger`, `Log`, `Source`, or `Categories` alias is required on a component.

Source identity layers follow Phase 2:

- C++ component type for normal calls;
- compact local source ID for runtime records;
- stable explicit or manifest ID when an external dictionary requires it;
- human-readable name as metadata.

Compiler type names and `__PRETTY_FUNCTION__` hashes are not stable source IDs.

## 9. Non-Component Sources

Libraries, parser phases, or internal domains that are not lifecycle components
may contribute dedicated sources:

```cpp
struct ProtocolParserLog
{
    static constexpr solar::log::SourceDescriptor descriptor{
        .name = "remote.protocol.parser",
    };
};

struct RemoteService
{
    using LogSources = solar::log::Sources<ProtocolParserLog>;
};
```

The logging subsystem owns
`contribution_source<log::source_tag, Component>`. Collection preserves the
contributing component as semantic owner and registration origin.

Typed calls require either an effective component source or a contributed
source. Arbitrary source strings and integers are not accepted by the normal
Solar frontend.

Root-owned sources use a root logging source section where needed. `System` is
the ordinary root source for application-wide messages.

## 10. Domains

Domains classify message subject independently from source ownership.

Solar initially provides extensible descriptors for:

- lifecycle;
- transport;
- communication;
- storage;
- control;
- device;
- scheduling;
- security;
- resource management.

A source may declare a default domain through source traits or logging
configuration. Calls may select another registered domain explicitly.

Domains have local identity, stable metadata where externally required, and
human-readable names. They replace the current source-private free-form category
lists.

Source answers “who produced this?” Domain answers “what area is this about?”

## 11. Source And Domain Catalogs

The effective logging catalog contains:

- all component sources;
- contributed non-component sources;
- root and built-in Solar sources;
- registered domain descriptors;
- sink and routing descriptors;
- callsite/dictionary metadata when retained.

Zephyr-linked sources form a read-only platform catalog extension. They are
immutable linker-defined sources rather than C++ component contributions.

Inspection preserves origin:

- Solar component source;
- Solar contributed source;
- Zephyr domain/source pair;
- event-derived source;
- redirected print source.

## 12. Canonical Record Envelope

Every accepted record has a complete header equivalent to:

```cpp
struct LogRecordHeader
{
    Sequence sequence;
    Timestamp timestamp;
    LogSourceId source;
    DomainId domain;
    Level level;
    ContextKind context;
    CorrelationId correlation;
    CallsiteId callsite;
    Origin origin;
    Encoding encoding;
    std::uint16_t payload_size;
    RecordFlags flags;
};
```

Optional configured metadata includes:

- thread ID or compact thread identity;
- core ID;
- source location;
- originating event ID;
- event occurrence sequence;
- timestamp clock-domain or quality flags;
- known preceding loss.

Exact widths remain implementation and Kconfig decisions. Every complete
record is self-contained for its documented lifetime.

## 13. Sequence And Timestamp

All accepted Solar-native, Zephyr-native, event-derived, and redirected print
records receive one boot-local monotonic sequence from the Solar ingress.

The sequence orders successful capture, not wall-clock causality across remote
processor domains.

Timestamp is acquired at the original logging call or Zephyr frontend callback.
It is never assigned only when a sink later formats the record.

Early boot and ISR may use cycle-domain timestamps. Record flags preserve clock
domain and quality until normalization is possible.

Timestamp and sequence are independent. Sequence resolves equal or unavailable
timestamps within one Solar ingress.

## 14. Origin

Initial record origins are:

- native Solar call;
- imported Zephyr call;
- event-generated log;
- redirected `printk` or raw platform print;
- Solar logging infrastructure;
- panic/emergency path.

Origin is query and routing metadata. It does not replace source, domain, or
level.

Sinks may filter by origin, for example retaining Zephyr driver warnings while
excluding routine redirected prints.

## 15. Payload Encodings

Initial payload encodings are:

- Solar typed argument package;
- Zephyr `cbprintf` package;
- copied text;
- bounded hexdump;
- event-render package or Solar arguments with event linkage.

`LogRecordView` exposes normalized header metadata plus an immutable encoding
view. A sink may:

- render the record;
- forward a supported encoded representation;
- copy the complete record into bounded retained storage;
- reject unsupported encoding with focused accounting.

No sink receives raw pointers to producer stack objects.

## 16. Solar Formatting

### 16.1 Syntax

Solar-native logs use compile-time-checked brace formatting:

```cpp
solar::log::notice<RemoteService>(
    "Session {} connected from {}", session, endpoint);
```

The initial grammar is a deliberately bounded allocation-free subset. Solar
does not claim the complete `std::format` grammar until target code size,
allocation, and library behavior are proven.

Initial formatting should cover:

- default replacement fields;
- integer decimal and hexadecimal;
- width for bounded scalar fields;
- floating precision when enabled;
- booleans and enums through traits;
- bounded strings;
- stable IDs;
- pointers only through an explicit safe wrapper.

### 16.2 Compile-time validation

Format/argument count and supported type compatibility are checked through
C++23 `consteval`, constrained templates, and static callsite metadata.

An unsupported formatter or malformed literal is a compile-time error.

### 16.3 Allocation

Producer capture and deferred rendering require no dynamic allocation. A
standard-library formatting primitive may be used only after implementation
proves allocation-free bounded behavior on supported Zephyr SDK targets.

## 17. Argument Retention

Solar-native capture stores:

- a static format token or callsite reference;
- argument schema token;
- encoded argument values;
- copied transient string bytes;
- truncation and encoding flags.

Producer-side text formatting is not the normal path.

Supported initial retained arguments include:

- fixed-width signed and unsigned integers;
- enabled floating types;
- booleans;
- enums through formatting traits;
- registered compact IDs;
- string literals by static reference or dictionary token;
- bounded owned strings by copy;
- explicitly registered bounded custom format values.

Raw transient character pointers, arbitrary references, unbounded containers,
and objects with hidden dynamic ownership are invalid without explicit bounded
conversion.

Dynamic text uses an explicit copied-text frontend. String arguments are copied
or rejected according to their type; their producer lifetime is never assumed.

## 18. Callsite Identity

A callsite descriptor may contain:

- build-local callsite ID;
- source and default domain;
- static format token;
- argument schema;
- optional file, line, function, and column metadata;
- renderer entry or formatting schema;
- build dictionary identity.

Callsite identity is normally build-local. Logs are human-oriented diagnostics,
not stable structured event schema.

Remote dictionary output associates callsites with one exact firmware build.
Long-lived stable external meaning belongs in an event schema rather than a log
callsite ID.

`SOLAR_LOG_ID()` and hashes of `__PRETTY_FUNCTION__` are removed as stable
identity mechanisms.

## 19. Receipt And Error

### 19.1 Receipt

A successful result contains a receipt with:

- disposition;
- sequence when captured;
- timestamp when captured;
- encoded size;
- truncation or emergency flags.

Initial dispositions include:

- captured;
- compile-time filtered;
- runtime filtered;
- intentionally suppressed by policy;
- captured through elevated reserve;
- captured through emergency path.

Filtering is a successful outcome, not an infrastructure error.

### 19.2 Error

The typed error domain initially distinguishes:

- capture closed;
- ingress capacity exhausted;
- non-waiting contention;
- bounded wait timeout;
- record too large;
- argument encoding failure;
- invalid execution context;
- unsupported ISR argument or format;
- timestamp failure where required;
- panic sink failure;
- internal invariant failure.

Errors update focused records without recursively logging another error.

## 20. Compile-Time Filtering

Kconfig establishes a global maximum compiled level. C++ logging configuration
may lower the maximum by source and domain.

Code below the effective compile-time threshold does not instantiate record
capture, argument encoding, or formatting machinery.

Kconfig's global threshold is visible in every component translation unit and
is therefore always applied at compile time. In strict binding mode the bound
System is also visible at the call site, so Blueprint `CompileLevel`,
`SourceLevel`, and `DomainLevel` thresholds are compile-time filters as well.
In relaxed binding mode ordinary component headers deliberately do not see the
composition root. Those typed bound thresholds are initialized as equivalent
source and domain ingress filters; they preserve filtering semantics with the
accepted negligible relaxed-mode capture overhead. Relaxed
`enabled<Source, Level>` can consequently promise only the Kconfig-known
compile-time fact. Typed configuration may never restore a level removed by
Kconfig.

However, ordinary C++ function arguments are evaluated before entering the
function:

```cpp
solar::log::debug<Source>(
    "Value {}", expensive_debug_value());
```

Solar cannot make a function call suppress side-effectful argument evaluation.
For expensive expressions, the exact form is:

```cpp
if constexpr (solar::log::enabled<Source, solar::log::Level::Debug>)
{
    solar::log::debug<Source>(
        "Value {}", expensive_debug_value());
}
```

An optional macro frontend may provide guaranteed no-evaluation compile-time
filtering, but the typed function remains the ordinary API.

Log arguments should not contain behaviorally required side effects regardless
of filtering mode.

## 21. Runtime Filtering

Runtime filtering has two stages.

### 21.1 Ingress aggregate filter

The facility computes whether any active history or sink route accepts a
source/domain/level/origin combination. If none does, the call returns a
runtime-filtered receipt without allocating a record.

### 21.2 Per-sink filter

Each sink independently filters accepted records by:

- minimum and maximum level;
- source or owner;
- domain;
- origin;
- correlation presence where useful;
- panic compatibility;
- optional static predicate over header metadata.

Filters do not inspect formatted human text.

### 21.3 Runtime mutation

Focused APIs may change runtime source, domain, and sink thresholds under a
bounded synchronization policy. Runtime filters cannot restore compile-removed
code.

For Zephyr sources, Solar updates Zephyr's dedicated custom-frontend runtime
filter slot so rejected records are not packaged before the frontend callback.

## 22. Ingress Storage

Logging uses one bounded variable-length multi-producer ingress ring.

Variable-length storage is appropriate because format arguments, copied strings,
Zephyr packages, text, and hexdumps vary substantially in size.

Admission reserves space for a complete header and payload before commit. The
processor observes either a complete record or no record.

The ingress requires:

- bounded lock-free, spin-protected, or Zephyr MPSC-compatible reservation;
- thread and ISR producers;
- one consumer;
- complete-record wrap markers;
- no dynamic allocation;
- deterministic maximum record size;
- monotonic commit sequence;
- explicit incomplete-reservation recovery if a producer is interrupted.

No sink or formatter executes while producer storage synchronization is held.

## 23. Priority Reservation And Overflow

### 23.1 Priority reservation

Low-level traffic must not consume every byte needed by serious diagnostics.
The ingress reserves bounded capacity for:

- Warning and above;
- Fatal and panic emergency capture.

Debug and Info cannot consume elevated reserves. Notice uses ordinary capacity
by default but may be configured as elevated for a product.

### 23.2 Default overflow

The default is drop newest. Already accepted pending records remain intact.

Alternative overwrite-oldest policy requires explicit configuration because it
destroys a previously accepted record and complicates MPSC behavior.

### 23.3 Waiting

Normal thread calls may use a finite level-specific admission wait only when
explicitly configured. The default common path is non-waiting best effort.

ISR calls never wait. Fatal uses its emergency path rather than ordinary wait.

### 23.4 Loss visibility

Overflow updates counters and latches. The next accepted record may carry
known-preceding-loss metadata. The facility never recursively logs that its log
record could not be logged.

## 24. Oversize And Truncation

No malformed structured package is admitted.

Policy distinguishes:

- copied text, which may truncate on a valid character boundary;
- hexdump data, which may truncate at a byte boundary;
- copied bounded string arguments, which may truncate when their formatter
  contract permits it;
- structured argument packages, which otherwise reject complete-record
  oversize.

Truncation is always flagged and counted. It never silently changes format
argument count or produces invalid encoding.

Source, level, sequence, timestamp, and correlation are retained even when a
payload is truncated through an allowed policy.

## 25. ISR Logging

Solar-native ISR calls are explicit:

```cpp
solar::log::try_warn_isr<Encoder>(
    "Capture overrun: {}", count);
```

ISR logging permits only:

- compile-time static format strings;
- bounded scalar or explicitly ISR-safe arguments;
- ISR-safe timestamp capture;
- non-waiting complete-record reservation;
- no dynamic or transient string copying beyond a fixed proven bound;
- no formatting;
- no sink calls;
- no mutex use.

Normal Solar logging frontends used from ISR return an invalid-context error or
assert under debug policy rather than silently adopting ISR semantics.

Zephyr-native ISR logging enters the custom frontend naturally and is captured
under equivalent package-size and reservation constraints.

## 26. Deferred Processor

The log processor:

1. drains complete ingress records in sequence order;
2. updates canonical accounting and optional history;
3. applies per-sink filters;
4. creates required encoded or rendered representations;
5. offers the record to every eligible sink;
6. updates sink and formatting records;
7. releases ingress storage.

The processor uses shared Solar execution rather than an unconditional
dedicated logging thread. Phase 9 defines the final execution registration.

One sink failure does not prevent attempts to serve other sinks.

The processor never invokes application domain behavior.

## 27. Sink Concept

A log sink is statically configured infrastructure that consumes immutable
records.

A sink may:

- render and write text;
- retain complete encoded records;
- forward a binary representation;
- submit records to a bounded transport adapter;
- write a persistent log stream;
- expose focused sink records.

A sink must declare:

- identity and descriptor;
- accepted encodings or renderer;
- filter policy;
- admission and backpressure policy;
- flush behavior;
- panic safety;
- lifecycle dependencies such as a transport-owning service.

Sinks do not subscribe application behavior to logs.

## 28. Sink Routing

Illustrative configuration:

```cpp
using Logging = solar::log::Configuration<
    solar::log::Sinks<
        solar::log::To<
            ConsoleSink,
            solar::log::MinimumLevel<solar::log::Level::Debug>,
            solar::log::format::Compact>,

        solar::log::To<
            RetainedHistory,
            solar::log::MinimumLevel<solar::log::Level::Notice>>,

        solar::log::To<
            RemoteLogAdapter,
            solar::log::MinimumLevel<solar::log::Level::Info>,
            solar::log::format::Encoded>>>;
```

The exact blueprint spelling may normalize into Phase 1 logging configuration,
but sink entries are not mixed into source contribution catalogs.

A transport-owning service remains a component. The sink is a leaf adapter
that depends on and calls that service through its static typed API.

## 29. Formatting And Writers

Rendering occurs only when a sink requires text or another transformed output.

Initial renderers may include:

- compact text;
- detailed text;
- JSON lines;
- RFC 5424 syslog;
- native encoded Solar package;
- native or converted Zephyr dictionary package.

If several sinks request the same rendered representation, the processor may
render once into shared bounded scratch and offer that immutable output to each
sink during the same record processing step.

Slow or asynchronous writers own independent bounded admission. They copy the
representation they retain. A sink cannot retain processor scratch by pointer.

Formatting failure is isolated to sinks requiring that representation.

## 30. Retained History

Log history is an optional sink owned by Logging, separate from the pending
ingress ring.

History uses a bounded variable-length byte ring with:

- complete records only;
- explicit level/source/domain retention filter;
- drop-oldest complete-record default;
- cursor and sequence gap reporting;
- caller-owned paged query output;
- optional critical reservation;
- no automatic persistence.

Ingress capacity protects producer-to-processor flow. History capacity protects
post-processing diagnostic retention. They are not the same storage or policy.

History is not enabled merely because Remote exists.

## 31. Zephyr Integration Models Considered

### 31.1 Zephyr-owned core with Solar backend

```text
Solar API -> Zephyr core -> Solar backend -> Solar sinks
```

This preserves Zephyr's default queue but makes typed Solar source selection
awkward, places Zephyr representation at the architecture center, and normally
requires another copy for Solar retention or async routing.

### 31.2 Solar core with Zephyr backend import

```text
Solar API -> Solar core
Zephyr API -> Zephyr core -> Solar backend -> Solar core
```

This preserves clean Solar-native sources but double-buffers imported messages
and gives two processing schedules before one canonical stream.

### 31.3 Solar custom frontend

```text
Solar API -> Solar core
Zephyr API -> Solar frontend -> Solar core
```

This is accepted because Zephyr 4.4 provides an official custom frontend,
frontend-only operation, frontend runtime filtering, packaged arguments, and
panic notification.

The Zephyr adapter remains isolated so version-specific package details do not
leak into Solar's public API or sink concepts.

## 32. Zephyr Frontend Capture

The frontend implements the official Zephyr callbacks:

- initialization;
- generic message capture;
- optional optimized zero-, one-, and two-argument capture;
- panic notification.

The generic callback receives no completed Zephyr timestamp, so Solar captures
its configured timestamp immediately in the callback.

The callback receives package memory whose producer lifetime ends on return.
Solar therefore:

1. checks the Zephyr frontend runtime filter already applied by Zephyr;
2. computes the self-contained copied package size;
3. reserves one complete Solar ingress record;
4. copies/converts transient strings through supported `cbprintf` package APIs;
5. copies hexdump data when present;
6. normalizes source, level, context, and timestamp;
7. commits the record;
8. returns without sink processing.

Copy or conversion failure updates frontend accounting and drops the complete
message.

## 33. Zephyr Sources And Domains

Zephyr source identity is represented as a tagged pair:

```text
Zephyr domain ID + Zephyr source ID
```

The linked Zephyr source descriptor supplies the immutable module or instance
name. Solar does not pretend a Zephyr source is a Solar component.

Single-domain Zephyr sources normally use domain zero. Multi-domain source and
timestamp behavior remains explicit; Solar does not claim total ordering across
unsynchronized remote clocks.

Runtime inspection can enumerate linked Zephyr source descriptors independently
from the C++ component source catalog.

## 34. Zephyr Formatting And Dictionary Output

Imported records preserve self-contained `cbprintf` packages and optional
hexdumps until a sink chooses representation.

Text sinks use supported Zephyr package/output APIs rather than first formatting
into temporary text during frontend capture.

Encoded sinks may forward dictionary-compatible data associated with the exact
firmware build dictionary. A dictionary is build-specific and is never treated
as a stable cross-build log schema.

Remote dictionary negotiation belongs to Phase 10. A client without the correct
dictionary may request device-rendered text instead.

## 35. Redirected Print And Hexdump

When `CONFIG_LOG_PRINTK` is enabled, redirected `printk` data enters the same
Solar frontend and stream. It is marked with redirected-print origin and raw or
platform level semantics.

Redirected print cannot generally provide typed Solar source or correlation.

Hexdump records retain:

- source and level;
- static label where supplied;
- copied bounded bytes;
- original and retained lengths;
- truncation flag.

Hexdump payload limits are Kconfig hard ceilings and may differ for thread and
ISR capture.

## 36. Event-Generated Logs

The Phase 6 event processor uses an internal logging bridge:

```text
EventRecord
    -> event logging policy
    -> internal LogRecord capture
```

It never calls the public `solar::log` frontend.

An event-generated log preserves:

- source from the event occurrence;
- event stable/local identity where configured;
- event occurrence sequence;
- correlation;
- aggregation count;
- selected event-to-log renderer identity;
- event-derived origin.

Logging policy may be always, never, mandatory, forbidden, first, every Nth,
rate-limited, after consecutive occurrences, or on recovery/transition as
accepted by Phase 6.

Mandatory and forbidden event policy outrank call-site `LogIntent`.

Logging infrastructure failures do not emit events automatically. Explicit
failure adapters must use events whose logging policy is forbidden to prevent
recursion.

## 37. Early Boot

The static ingress and minimum frontend accounting are available from zeroed
static storage before the Logging facility lifecycle hook runs.

Early Zephyr and Solar logs may therefore be captured before ordinary component
initialization. Early records may have:

- raw-cycle or unavailable timestamps;
- no thread identity;
- default runtime filtering;
- no active sinks until facility initialization.

Logging facility initialization validates the bound catalog, initializes sinks,
installs effective runtime filters, and schedules processing of accepted early
records.

If logging is compiled out, non-fatal calls become documented filtered/no-op
frontends with no runtime storage. Fatal policy remains available independently
of text logging.

## 38. Lifecycle

### 38.1 Inclusion

Logging is a Kconfig-selected built-in. When logging is enabled, its core
capture facility is part of the effective system even when no application
component contributes a source. Kconfig and typed configuration determine
whether Zephyr frontend integration, sinks, history, event adapters, and Remote
exposure are retained.

When logging is disabled, the documented compile-time filtering and fatal
policy rules apply. A log call is not used as an implicit graph-discovery
mechanism.

### 38.2 Dependencies

The Logging core is early infrastructure, not an implicit dependency edge from
every ordinary component. Its ingress is available before ordinary component
initialization.

Sinks are leaf adapters. A sink that requires a transport or Remote depends on
that provider and activates only after the provider is ready; Logging itself
does not depend on the sink transport. Remote may consume Logging, but Logging
never depends on Remote. This one-way relationship prevents the
Logging/Remote lifecycle cycle while preserving early capture.

### 38.3 Availability

Capture remains available through:

- early boot;
- dependent component init;
- start;
- running operation;
- boot rollback;
- dependent component stop;
- dependent component deinit;
- final Logging drain.

### 38.4 Start

The facility starts its shared-executor processing registration after sink
initialization. No dedicated service thread is inherent to logging.

## 39. Flush

Normal flush is explicit:

```cpp
auto result = solar::log::flush(timeout);
```

Flush:

- establishes a target accepted sequence;
- processes or waits for all eligible records through that sequence;
- invokes sink flush operations;
- returns per-sink and aggregate completion information;
- has a finite timeout;
- does not claim physical durability unless a sink contract does.

New concurrent records after the target sequence do not indefinitely extend one
flush call.

A non-waiting `try_flush` may report busy. ISR flush is unsupported outside the
panic path.

## 40. Panic Mode

Panic mode is one-way for the current boot.

The first transition:

1. atomically marks panic mode;
2. prevents dependence on the normal executor;
3. notifies each sink of panic transition;
4. synchronously drains pending complete records;
5. flushes panic-safe sinks;
6. changes subsequent capture to synchronous emergency routing where possible.

Panic-safe sinks must not require:

- dynamic allocation;
- normal scheduler progress;
- interrupts that may be disabled;
- an unbounded lock;
- asynchronous transport completion.

Unsafe sinks are marked unavailable and skipped. Their failure cannot block the
fatal policy indefinitely.

Zephyr `log_panic()` invokes the Solar frontend panic callback. Solar-initiated
panic coordinates with Zephyr exactly once and prevents callback recursion
through the panic state latch.

## 41. Shutdown

Phase 3 contains normal deferred execution before component stop hooks. Logging
therefore follows the same shutdown shape as events:

1. normal logging processor work is contained;
2. dependent components stop and deinitialize while capture remains open;
3. Logging performs a final bounded synchronous drain;
4. shutdown-safe sinks flush;
5. asynchronous sink queues are cancelled or drained by declared policy;
6. capture closes;
7. Logging deinitializes before its transport dependencies.

Stop/deinit logs remain capturable without keeping arbitrary application worker
execution alive.

## 42. Remote Boundary

Remote consumes logs through an explicit adapter. It never becomes canonical
log storage.

Logging owns:

- source and domain identity;
- record capture and sequence;
- optional retained history;
- rendering and encoded representations;
- sink and loss accounting.

Remote owns:

- exposure registration;
- session subscriptions;
- authorization;
- per-session level/source/domain filters;
- per-session queue and backpressure;
- text versus encoded preference;
- dictionary negotiation;
- slow-client and disconnect behavior.

A slow or disconnected client cannot block producers or the core log processor.
Remote may retrieve retained history through bounded page APIs when exposure
explicitly allows it.

## 43. Focused Inspection

Compile-time inspection exposes immutable:

- Solar source descriptors;
- domain descriptors;
- sink descriptors;
- route and filter policy;
- retained callsite metadata when configured.

Runtime inspection exposes bounded copies of:

- facility state;
- ingress occupancy and high-water mark;
- per-level capture/filter/drop/truncation counters;
- per-source counters when enabled;
- per-sink accepted, filtered, failed, queued, and dropped counters;
- panic state;
- last capture, render, sink, and flush failure;
- retained history pages.

Likely focused APIs include:

```cpp
solar::log::records::facility();
solar::log::records::source<RemoteService>();
solar::log::records::sink<ConsoleSink>();
solar::log::history::read(cursor, destination);
```

There is no universal all-system snapshot.

## 44. Failure Accounting

The facility records at least:

- attempted calls;
- compile-time and runtime filtered records where compiled accounting permits;
- captured records by level and origin;
- ingress capacity rejection;
- reserve use;
- oversize rejection;
- argument encoding failure;
- truncation count and bytes;
- Zephyr package conversion failure;
- history eviction;
- renderer failure;
- per-sink rejection and failure;
- flush timeout;
- panic sink unavailability;
- last failure reason and timestamp;
- known preceding loss.

Logging failure accounting is canonical logging state. Optional metrics may
expose selected values through storage-free views or explicit adapters.

The facility never logs its own ordinary accounting failure through the same
path.

## 45. Configuration

### 45.1 Kconfig ownership

Kconfig owns build capability, Zephyr integration, hard ceilings, and defaults
such as:

- Solar logging inclusion;
- Zephyr `CONFIG_LOG` and custom frontend selection;
- frontend-only mode;
- redirected `printk` support;
- global compiled maximum level;
- runtime filtering support;
- ingress byte capacity;
- maximum Solar package, copied string, text, and hexdump sizes;
- Warning/Error reserve and fatal emergency capacity;
- timestamp width and backend;
- thread/core/source-location metadata retention;
- history capability and hard size ceiling;
- renderer and dictionary capability;
- default overflow and wait policy;
- panic and fatal defaults;
- focused accounting inclusion.

Likely Solar symbols include:

```text
CONFIG_SOLAR_LOG
CONFIG_SOLAR_LOG_LEVEL
CONFIG_SOLAR_LOG_INGRESS_BYTES
CONFIG_SOLAR_LOG_MAX_RECORD_BYTES
CONFIG_SOLAR_LOG_ELEVATED_RESERVE_BYTES
CONFIG_SOLAR_LOG_EMERGENCY_BYTES
CONFIG_SOLAR_LOG_MAX_STRING_BYTES
CONFIG_SOLAR_LOG_MAX_HEXDUMP_BYTES
CONFIG_SOLAR_LOG_HISTORY_BYTES
CONFIG_SOLAR_LOG_RUNTIME_FILTERING
CONFIG_SOLAR_LOG_SOURCE_LOCATION
CONFIG_SOLAR_LOG_ACCOUNTING
```

Exact symbols are finalized during implementation. There is no C++ fallback
configuration header.

### 45.2 C++ ownership

C++ declarations own:

- source and domain membership;
- source descriptor and default domain;
- source/domain compile thresholds below Kconfig maximum;
- sink declarations and dependencies;
- routes and per-sink filters;
- renderer and encoded output policy;
- history retention policy;
- per-level overflow/wait policy within Kconfig ceilings;
- event-to-log adapters;
- explicit Remote exposure;
- fatal policy override where Kconfig permits it.

### 45.3 Precedence

```text
explicit source, route, or sink policy
    > logging blueprint configuration
    > Kconfig default
```

C++ policy cannot restore compile-removed levels, exceed byte ceilings, enable
an excluded renderer, replace another Zephyr custom frontend, or select an
unsupported panic capability.

## 46. Resource Accounting

The effective system derives logging-owned resources from Kconfig and the bound
configuration:

- one variable-length ingress byte ring;
- elevated and emergency reservations;
- one shared processor execution registration;
- optional retained history bytes;
- exact sink state and optional sink queue storage;
- bounded renderer scratch shared by compatible routes;
- source/domain runtime filters when enabled;
- callsite and dictionary metadata according to retention policy;
- focused accounting according to Kconfig;
- no Zephyr default deferred log buffer in frontend-only mode.

Sinks that require their own asynchronous queues expose those exact costs.
Remote per-session storage belongs to Remote rather than Logging.

No dynamic allocation is required by canonical capture, routing, or formatting.

## 47. Complete Example

```cpp
struct ProtocolParserLog
{
    static constexpr solar::log::SourceDescriptor descriptor{
        .name = "remote.protocol.parser",
    };
};

struct RemoteService
{
    using LogSources = solar::log::Sources<ProtocolParserLog>;

    static solar::Result<void> accept(SessionId session);
};

solar::Result<void> RemoteService::accept(SessionId session)
{
    solar::log::notice<RemoteService>(
        "Session {} connected", session);

    return {};
}

struct ConsoleSink
{
    static constexpr solar::log::SinkDescriptor descriptor{
        .name = "console",
    };

    using PanicSafety = solar::log::panic::Safe;
};

using RobotBlueprint = solar::Blueprint<
    solar::Services<RemoteService>,

    solar::log::Configuration<
        solar::log::CompileLevel<solar::log::Level::Debug>,
        solar::log::SourceLevel<
            RemoteService,
            solar::log::Level::Debug>,
        solar::log::Sinks<
            solar::log::To<
                ConsoleSink,
                solar::log::MinimumLevel<solar::log::Level::Info>,
                solar::log::format::Compact>,
            solar::log::To<
                RetainedHistory,
                solar::log::MinimumLevel<solar::log::Level::Notice>>>>;

using RobotSystem = solar::System<RobotBlueprint>;

SOLAR_BIND_SYSTEM(RobotSystem);
```

Ordinary source use:

```cpp
solar::log::debug<RemoteService>(
    "Decoded frame type {:#x}", type);

solar::log::notice<ProtocolParserLog>(
    "Negotiated protocol version {}", version);

solar::log::warn<
    RemoteService,
    solar::log::domain::Transport>(
        "TX queue has {} pending frames", pending);
```

Zephyr-native modules continue using:

```cpp
LOG_INF("USB device configured");
```

Both paths enter the same canonical Solar stream.

## 48. Include Direction

Component headers that only serve as sources need no logging alias or logging
header beyond what their inline implementation directly uses.

Non-component source declaration headers include only Solar logging descriptor
and contribution headers.

Definitions calling bound global logging APIs normally live in source files
that include the completed application root:

```cpp
// services/remote_service.cpp
#include "app/system.hpp"

solar::Result<void> RemoteService::start_session(SessionId id)
{
    solar::log::notice<RemoteService>(
        "Starting session {}", id);

    return {};
}
```

Lazy inline bound calls may use the Phase 1 defaulted function-template pattern.
Ordinary reusable headers do not include the application root.

Zephyr modules remain independent and use their normal `LOG_MODULE_REGISTER`,
`LOG_MODULE_DECLARE`, and `LOG_*` APIs.

## 49. Compile-Time Validation

Effective-system validation rejects:

- typed use of an unregistered non-component source;
- duplicate source or domain type, name, local identity, or stable identity;
- missing or invalid source/domain descriptor;
- malformed format literal;
- format/argument count mismatch;
- unsupported or unbounded retained argument;
- runtime string passed through a static-literal-only frontend;
- source or domain compile level above the Kconfig maximum;
- invalid sink descriptor or route;
- duplicate logical sink route without an explicit route tag;
- unsupported renderer or payload encoding;
- sink queue or history capacity beyond Kconfig ceiling;
- waiting policy on ISR topology;
- non-panic-safe sink declared mandatory during panic;
- event log adapter referencing an unregistered event, source, or renderer;
- Remote encoded exposure without stable source/build dictionary identity;
- another custom Zephyr frontend selected with frontend-only Solar ownership;
- Solar logging configuration while required Kconfig capability is disabled;
- generated lifecycle dependency cycles.

Diagnostics identify the source, domain, callsite, sink, route, format argument,
and violated capability rather than failing only in package encoding internals.

## 50. Runtime Failure Behavior

Runtime failures remain possible after valid compilation:

- ingress capacity exhausted;
- bounded wait timeout;
- no-wait reservation contention;
- dynamic text or copied string exceeds policy;
- timestamp unavailable or degraded;
- Zephyr package conversion fails;
- renderer rejects or truncates output;
- retained history evicts records;
- sink queue rejects a record;
- transport write or persistent store fails;
- flush times out;
- a panic-unsafe sink becomes unavailable;
- emergency capacity is exhausted.

Successful capture means the canonical record was accepted. It does not promise
that every optional sink later delivered it.

Sink failures do not retroactively change producer results and do not prevent
attempts to serve other sinks.

## 51. Migration Direction

The current Solar logger provides useful seeds:

- typed levels;
- source and category metadata;
- static sink lists;
- filter, formatter, and writer separation;
- fixed test ring writer;
- per-sink accounting;
- global static methods.

The target replaces or reforms:

- logger types selected by `System` runtime configuration;
- `NullLogger` as a hidden application logger implementation;
- nested `Logger::Log` aliases;
- source-private category lists;
- direct synchronous sink fan-out;
- call-site `snprintf` formatting;
- fixed stack text buffer for every call;
- raw transient message/source/category pointers in records;
- FNV `__PRETTY_FUNCTION__` callsite identity;
- sink objects owning direct transport instances;
- byte-overwriting text ring as retained log storage;
- ignored truncation and ambiguous formatting failure;
- independent Solar logs that do not ingest Zephyr ecosystem logs.

Migration should proceed through:

1. source/domain descriptors and global frontends;
2. canonical record and variable ingress;
3. Solar typed package encoding;
4. deferred processor and existing writer adapters;
5. Zephyr custom frontend capture;
6. sink routing and history;
7. panic/fatal and Remote adapters;
8. removal of direct logger/store aliases.

## 52. Verification Requirements

The implementation must eventually cover:

- component sources without logger aliases;
- contributed non-component sources;
- owner and registration origin preservation;
- source/domain duplicate diagnostics;
- Debug, Info, Notice, Warning, Error, and Fatal ordering;
- Zephyr level mapping and Notice preservation;
- common, domain, correlation, and non-waiting frontends;
- typed receipt and error behavior;
- compile-time format validation;
- all supported scalar and custom bounded arguments;
- static literal and copied transient string lifetime;
- malformed, unsupported, and oversized package rejection;
- compile-time filtering body elimination;
- documented function argument evaluation behavior;
- `enabled<Source, Level>` expensive-argument guard;
- ingress aggregate and independent sink filtering;
- runtime source/domain/sink filter mutation;
- Zephyr frontend filter synchronization;
- concurrent thread producers;
- nested and preempted producer reservation;
- valid and invalid ISR frontends;
- complete-record variable ring wrap;
- elevated and emergency capacity isolation;
- drop-newest and explicit overwrite behavior;
- finite wait and no-wait paths;
- text, string, and hexdump truncation flags;
- Solar argument, Zephyr package, copied text, and hexdump encodings;
- sequence and timestamp behavior across origins;
- Zephyr generic and optimized frontend callbacks;
- transient Zephyr string package conversion;
- Zephyr module and instance source normalization;
- redirected `printk` capture;
- device text rendering and dictionary output;
- all eligible sinks attempted after failure;
- shared rendering scratch without retained dangling views;
- bounded asynchronous sink admission;
- retained complete-record history and stale cursors;
- event-derived source, correlation, event ID, and sequence preservation;
- event/log recursion prevention;
- early boot capture before facility init;
- normal flush target-sequence behavior;
- flush timeout and durability reporting;
- shutdown capture and final synchronous drain;
- Zephyr-initiated and Solar-initiated panic transition;
- panic reentrancy latch;
- panic-safe and panic-unsafe sink behavior;
- emergency fatal capture and terminal policy;
- disabled logging no-storage behavior;
- focused source, sink, facility, and history records;
- explicit Remote exposure without session coupling;
- exact static resource accounting;
- Kconfig and typed policy precedence.

Host tests should use deterministic clocks, record rings, formatters, and sinks.
Zephyr integration tests must exercise real Zephyr 4.4 frontend packages,
runtime filtering, module and instance sources, transient strings, ISR logging,
hexdumps, redirected `printk`, panic, and supported SDK targets.

Compile-fail tests are required for source, domain, format, argument, sink,
capacity, ISR, event adapter, Remote schema, and Kconfig conflicts.

## 53. Deferred Capabilities

The following remain deliberate later work:

- backend-import compatibility mode;
- coexistence with another custom Zephyr frontend;
- complete `std::format` grammar;
- arbitrary user-defined unbounded formatting;
- runtime source registration;
- dynamic sink registration;
- trace/span context beyond one correlation ID;
- persistent crash-dump extraction across reboot;
- multi-core or multi-domain total timestamp normalization;
- host dictionary transfer and generated decoder tooling;
- compressed log package transport;
- runtime callsite dictionary unloading;
- automatic log-to-event conversion;
- application log subscribers;
- C++ reflection-derived formatters and source descriptors.

Deferred capabilities must preserve bounded capture, one canonical Solar stream,
source identity, complete records, and panic safety.

## 54. Rejected Alternatives

### 54.1 Zephyr owns the canonical Solar logging architecture

Rejected because Solar source, domain, correlation, history, Remote, and sink
semantics would be constrained by Zephyr backend representation and lifecycle.

### 54.2 Import all Zephyr logs through a backend into a second core

Rejected as the primary mode because imported records are buffered twice and
processed by two schedules before reaching one canonical stream.

### 54.3 Ignore Zephyr-native logs

Rejected because drivers and Zephyr subsystems are a major part of real system
diagnosis.

### 54.4 Send all Solar calls through one Zephyr module

Rejected because it erases typed Solar source identity or requires hidden
prefix parsing and duplicate source encoding.

### 54.5 Require one Zephyr module macro per Solar component

Rejected because component types are already registered sources and ordinary
Solar logging should not require macro ceremony in every implementation file.

### 54.6 Eagerly format at the call site

Rejected because producer and ISR cost would depend on text formatting, every
sink would receive only one representation, and Remote could not use compact
encoded arguments.

### 54.7 Store raw argument pointers

Rejected because deferred processing outlives producer stack and transient
object lifetimes.

### 54.8 Promise full standard formatting immediately

Rejected because target allocation, library completeness, code size, and
type-erased deferred rendering must be proven first. The initial checked subset
keeps the contract honest.

### 54.9 Keep printf syntax as Solar's permanent API

Rejected because the typed C++ frontend can provide clearer compile-time
argument validation and modern brace formatting while still preserving Zephyr
`cbprintf` packages for imported logs.

### 54.10 Categories private to each source

Rejected because global typed domains compose better with filtering, imported
Zephyr sources, events, inspection, and external schemas.

### 54.11 Make domain mandatory on every call

Rejected because source plus level is sufficient for most logs. Sources may
declare a default domain.

### 54.12 Omit Notice

Rejected because normal but operationally significant information deserves a
filter level separate from routine Info and degraded Warning.

### 54.13 Treat Fatal as ordinary Error text

Rejected because a fatal API should have terminal, panic, and emergency-drain
semantics. Recoverable failures use Error.

### 54.14 Let fatal return and continue by default

Rejected because it contradicts the API meaning and encourages unsafe control
flow after a declared unrecoverable invariant failure.

### 54.15 Make every log call void

Rejected because capture assurance may matter. Returning a non-`nodiscard`
expected result preserves both concise best effort and explicit checking.

### 54.16 Claim function filtering suppresses argument evaluation

Rejected because C++ evaluates ordinary function arguments before the call.
`enabled` guards or an optional macro are required for that guarantee.

### 54.17 One fixed-size slot per log

Rejected because argument packages and copied data vary greatly and fixed slots
waste scarce memory or impose an artificially tiny record limit.

### 54.18 Byte-by-byte overwrite ring

Rejected because it can retain partial messages and cannot report complete
record eviction or sequence gaps coherently.

### 54.19 Let Debug consume emergency capacity

Rejected because a log flood must not prevent warning, error, or fatal capture.

### 54.20 Log an error when logging fails

Rejected because unavailable logging capacity would recurse. Focused counters
and latches own logging infrastructure failure truth.

### 54.21 Invoke sinks in producer context

Rejected for normal operation because producer latency would depend on routing,
formatting, transport, and storage. Panic is the explicit synchronous exception.

### 54.22 Share one sink configuration between events and logs

Rejected because event and log records have different identity, retention,
rendering, overflow, and routing semantics even when they share a physical
writer.

### 54.23 Allow sinks to invoke application behavior

Rejected because logging would become a hidden control bus and diagnostic
configuration could alter application correctness.

### 54.24 Automatically convert logs into events

Rejected because human text lacks stable structured schema and would create
recursion hazards. Important facts should originate as events.

### 54.25 Make Remote the log ring

Rejected because canonical diagnostics must survive absent or disconnected
sessions, and session backpressure belongs to Remote.

### 54.26 Automatically expose all logs through Remote

Rejected because registration is not authorization, session capacity, routing,
or dictionary compatibility.

## 55. Accepted Decisions

1. Logs are bounded human-oriented diagnostic explanations.
2. Logs remain distinct from events, metrics, parameters, and bus messages.
3. Solar owns the canonical log core and stream.
4. Zephyr-native logs are included in the canonical stream.
5. Solar uses a Zephyr custom frontend in frontend-only mode.
6. Zephyr's normal deferred buffer is absent in the primary Solar mode.
7. A backend-import compatibility mode is deferred.
8. The common API is `solar::log::<level><Source>(...)`.
9. Ordinary calls require no logger alias or object.
10. Non-fatal calls return non-`nodiscard` `Result<Receipt, Error>`.
11. `try_` frontends are strictly non-waiting.
12. Initial levels are Debug, Info, Notice, Warning, Error, and Fatal.
13. Notice means normal but operationally significant information.
14. Notice filters between Info and Warning.
15. Zephyr Info remains Info rather than being promoted to Notice.
16. Fatal is a terminal API with panic and emergency-drain semantics.
17. Production fatal policy defaults to Zephyr kernel panic.
18. Every effective Solar component is automatically a log source.
19. Non-component sources use the conventional `LogSources` contribution.
20. Source ownership and registration origin are preserved.
21. Typed calls reject unregistered non-component sources.
22. Source identity follows Phase 2 local/stable/name separation.
23. `__PRETTY_FUNCTION__` hashes are not stable source or callsite identity.
24. Domains classify subject independently from source ownership.
25. Domains are optional, typed, and extensible.
26. Domains replace source-private free-form category lists.
27. Zephyr sources form an immutable linked platform catalog extension.
28. Every canonical record has sequence, timestamp, source, domain, level,
    context, correlation, origin, encoding, and payload length.
29. All accepted origins share one boot-local capture sequence.
30. Timestamp is captured at the original call or frontend callback.
31. Initial payload encodings include Solar arguments, Zephyr packages, text,
    and hexdumps.
32. `LogRecordView` normalizes metadata without forcing eager text formatting.
33. Solar-native calls use compile-time-checked brace formatting.
34. The initial format grammar is an allocation-free bounded subset.
35. Solar does not promise complete `std::format` support initially.
36. Native arguments are retained by value or bounded copy.
37. Raw transient pointers and unbounded objects are not retained.
38. Zephyr `cbprintf` packages are copied into self-contained records.
39. Callsite identity is build-local unless an explicit external dictionary
    requires stable mapping.
40. Compile-time filtering removes capture and formatting implementation.
41. Ordinary function filtering cannot suppress argument evaluation.
42. `enabled<Source, Level>` is the exact guard for expensive arguments.
43. Runtime ingress filtering avoids allocation when no destination accepts.
44. Every sink applies its own independent filter.
45. Runtime filtering cannot restore compile-removed logs.
46. Zephyr frontend runtime filters mirror effective Solar capture demand.
47. Canonical ingress is one bounded variable-length MPSC record ring.
48. Admission and eviction operate on complete records.
49. No sink or formatter executes under producer storage synchronization.
50. Warning/Error and Fatal receive bounded reserved capacity.
51. Notice uses ordinary capacity by default and may be elevated explicitly.
52. Default overflow drops the newest record.
53. Ordinary thread capture is non-waiting by default.
54. Waiting is finite and explicitly configured.
55. Oversize structured packages are rejected rather than corrupted.
56. Text, bounded strings, and hexdumps may truncate only at valid boundaries.
57. Truncation is always flagged and counted.
58. Solar ISR logging uses explicit `try_*_isr` frontends.
59. ISR capture formats nothing and calls no sinks.
60. Zephyr-native ISR logs enter the same frontend constraints.
61. Deferred processing uses shared Solar execution.
62. The Logging facility does not inherently own a dedicated service thread.
63. Sinks are statically configured infrastructure.
64. Sinks may render, retain, write, persist, or forward records.
65. Sinks must not invoke application domain behavior.
66. Slow sinks use independent bounded admission.
67. One sink failure does not prevent attempts to serve other sinks.
68. Compatible text routes may share one bounded render result per record.
69. Retained history is separate from pending ingress.
70. History retains complete records and reports sequence gaps.
71. The Zephyr frontend copies package data before callback return.
72. Zephyr source identity preserves domain and source ID.
73. Zephyr dictionary output is tied to one exact firmware build.
74. Redirected `printk` may enter the unified stream with distinct origin.
75. Event-generated logs use an internal non-recursive bridge.
76. Event-generated logs preserve event identity, sequence, source, and
    correlation.
77. Mandatory and forbidden event logging outrank call-site intent.
78. Logging failures do not automatically emit events.
79. Static ingress supports early-boot capture before facility init.
80. Disabled non-fatal logging requires no runtime storage.
81. Fatal policy remains available when ordinary text logging is disabled.
82. Logging is a Kconfig-selected early built-in when enabled.
83. Logging remains available through dependent init, rollback, stop, and
    deinit.
84. Transport-dependent sinks are leaf adapters activated after their
    providers; Logging does not depend on those providers.
85. Normal flush has a finite target sequence and timeout.
86. Flush does not claim physical durability beyond sink contracts.
87. Panic mode is one-way for the current boot.
88. Panic drains synchronously only through panic-safe sinks.
89. Panic-unsafe sinks are skipped without blocking fatal action.
90. Zephyr- and Solar-initiated panic share one reentrancy latch.
91. Shutdown performs a final bounded synchronous drain.
92. Remote consumes logs through an explicit adapter.
93. Remote owns session policy and cannot block canonical producers.
94. Registration never implies Remote exposure.
95. Focused records own capture, filter, drop, truncation, sink, flush, and
    panic truth.
96. Logging never recursively reports ordinary logging infrastructure failure.
97. Kconfig owns Zephyr integration, capability, hard ceilings, and defaults.
98. C++ types own source, domain, route, sink, renderer, and adapter policy.
99. Explicit policy overrides logging configuration, then Kconfig default.
100. Canonical logging capture, routing, and formatting require no dynamic
     allocation.

## 56. Primary References

The Zephyr integration decisions were validated against the local Zephyr 4.4.0
source and official Zephyr documentation:

- [Zephyr Logging](https://docs.zephyrproject.org/latest/services/logging/index.html)
- [Zephyr Logger Backend Interface](https://docs.zephyrproject.org/latest/doxygen/html/group__log__backend.html)
- [Zephyr Formatted Output and cbprintf Packaging](https://docs.zephyrproject.org/latest/services/formatted_output.html)
- local `zephyrproject/zephyr/include/zephyr/logging/log_frontend.h`
- local `zephyrproject/zephyr/include/zephyr/logging/log_msg.h`
- local `zephyrproject/zephyr/subsys/logging/Kconfig.mode`

The integration layer must continue to verify its assumptions against every
supported Zephyr upgrade.

## 57. Open Questions

There are no blocking open questions for Phase 9.

Later specifications and implementation must refine without changing this
contract:

- exact Kconfig symbol names and numeric ceilings;
- concrete variable-length MPSC ring implementation;
- final bounded brace-format grammar and renderer internals;
- generated callsite and host dictionary representation;
- exact Zephyr package conversion flags for every supported configuration;
- shared-executor registration from Phase 9;
- physical transport sink and queue concepts;
- Remote encoded-log negotiation and session schema in Phase 10;
- persistent crash history format;
- multi-domain timestamp normalization policy;
- controlled in-process reboot handling for panic and history state.

These are extensions of the accepted frontend-owned canonical stream, typed
source model, retained-argument records, sink isolation, and panic contract.
