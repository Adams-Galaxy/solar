# Remote Protocol And System Integration

Date: 2026-07-15

Status: accepted design

Owning phase: Phase 10

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

## 1. Purpose

This specification defines Remote as Solar's typed external protocol plane.

Remote is how a host discovers, queries, observes, streams, configures, and
controls a Solar system without an in-firmware text CLI. It projects explicitly
selected system capabilities through a stable binary protocol while preserving
the ownership, synchronization, lifecycle, and policy boundaries of every
source subsystem.

It establishes:

- one static Remote facility derived from the bound system catalogs;
- one transport-independent Remote service by default;
- replaceable asynchronous links for UART, USB CDC, TCP, and tests;
- a callback-to-event-to-service execution path that remains safe for ISR, DMA,
  interrupt-driven, and socket-backed transports;
- independent protocol-engine and application-action execution policies;
- typed Data, capability, Action, Topic, Stream, and Schema declarations;
- query, update, watch, outbound-stream, and inbound-stream capabilities;
- owned coherent acquisition rather than references to live state;
- producer-pushed, periodically polled, and buffer-loaned stream acquisition;
- deterministic CBOR for ordinary structured payloads;
- explicit packed encoding for high-rate streams;
- COBS framing, CRC32C integrity, version negotiation, schema identity, and
  byte-stream resynchronization;
- sessions, authorization, correlation, cancellation, retries, duplicate
  suppression, and typed errors;
- bounded per-session admission, priorities, backpressure, fragmentation, and
  loss reporting;
- explicit adapters for parameters, metrics, observability events, logs, bus
  messages, lifecycle, graph, Kernel, and Execution facts;
- generated host manifests and optional bounded runtime introspection;
- a deterministic in-memory test boundary requiring no physical transport;
- migration from Solar's current combined synchronous Remote service.

The ordinary application paths remain compact:

```cpp
solar::remote::write<ImuTelemetry>(sample);
solar::remote::publish<GoalChanged>(goal);
```

Host interaction remains typed:

```text
query imu.telemetry
stream imu.telemetry --rate 200
action imu.calibrate --samples 500
set parameters.drive.kp 1.4
```

Those commands are rendered by host tooling from the generated manifest. They
are not parsed as text by firmware.

## 2. Non-Goals

Remote is not:

- a UART parser with subsystem behavior attached to it;
- a text shell, terminal, or formatted serial console;
- a second application bus;
- a generic mutable object registry;
- canonical storage for parameters, metrics, events, logs, or component state;
- permission to expose every registered declaration automatically;
- a universal system snapshot;
- an unbounded RPC server;
- an unbounded telemetry queue;
- a heap-backed future, promise, or coroutine runtime;
- a guarantee that every stream sample reaches every client;
- an encryption protocol by itself;
- a replacement for TLS or another authenticated transport;
- a reason to run protocol parsing, CBOR decoding, or application code in ISR
  or driver callback context;
- a reason to hold a source lock while encoding or transmitting;
- a way to serialize raw C++ object layout, compiler type names, pointers,
  references, or borrowed storage;
- a replacement for focused subsystem inspection APIs;
- a health or supervisory policy engine.

Remote reports protocol and delivery facts. It does not decide whether an
application component is healthy or whether the robot should continue running.

## 3. Canonical Vocabulary

- **Schema**: the stable external description and codec contract for one owned
  C++ value type;
- **Data**: a named remotely addressable source or sink of typed values;
- **capability**: one supported interaction with Data, such as Query, Update,
  Watch, OutStream, or InStream;
- **Action**: an explicit request/response operation with typed Request,
  Response, and Error schemas;
- **Topic**: an advanced standalone discrete device-to-host publication;
- **Stream**: an advanced standalone sustained sequence of homogeneous values;
- **query**: one host-initiated coherent Data read;
- **update**: one host-initiated validated Data replacement or mutation;
- **watch**: discrete subscription to selected changes or occurrences;
- **outbound stream**: sustained device-to-host sample flow;
- **inbound stream**: sustained host-to-device sample or chunk flow;
- **acquisition**: obtaining an owned coherent value from its source before
  Remote encoding or delivery;
- **push acquisition**: the source submits a value when it exists;
- **poll acquisition**: a Remote-owned execution registration calls a reader at
  an effective subscription rate;
- **loaned acquisition**: a producer fills a bounded staging lease and commits
  it when complete;
- **facility**: the static catalog, schema, dispatch, adapter, and global API
  layer;
- **Remote service**: the lifecycle-owning protocol engine and session executor;
- **link**: a typed adapter between the Remote service and one asynchronous byte
  transport;
- **transport owner**: the device, service, facility, or platform type that owns
  physical transport configuration and driver state;
- **session**: one boot-local authenticated connection state belonging to one
  link and one client;
- **frame**: one integrity-checked, COBS-delimited protocol unit;
- **message**: one logical protocol payload, possibly carried by several
  fragments;
- **lane**: one independently bounded per-session output priority class;
- **request ID**: a monotonically advancing session-local host identifier used
  for correlation and duplicate suppression;
- **buffer lease**: a move-only, generation-checked right to fill or consume one
  statically allocated buffer slot;
- **manifest**: the generated host-readable description of the effective Remote
  protocol surface for one firmware build.

The wire protocol may use the term **call** for invoking an Action. The public
C++ declaration is still an Action.

## 4. Architectural Decision

### 4.1 Three layers

Remote has three deliberate layers:

```text
                    bound system catalogs
                            |
                            v
                 +--------------------+
                 |  Remote facility   |
                 | schemas / dispatch |
                 | adapters / APIs    |
                 +----------+---------+
                            |
                            v
                 +--------------------+
                 |  Remote service    |
                 | sessions / engine  |
                 | queues / buffers   |
                 +----+----------+----+
                      |          |
                +-----+--+    +--+------+
                | USB link|    | TCP link|
                +--------+    +---------+
```

The facility is the static semantic plane. The service is the runtime protocol
plane. Links are transport adaptation.

### 4.2 Facility ownership

The Remote facility owns or derives:

- the effective Schema, Data, Action, Topic, Stream, link, and exposure
  catalogs;
- immutable external descriptors and local dispatch indices;
- schema codecs and compatibility metadata;
- Action and Data-capability dispatch tables;
- publication and acquisition frontend APIs;
- explicit subsystem adapter normalization;
- the effective manifest description;
- focused global Remote query entry points.

The facility has no thread and no user-created object. Its implementation may
use type-owned static objects for storage that genuinely requires objects.

### 4.3 Service ownership

The Remote service owns:

- service thread or selected engine execution registration;
- event admission and wakeup state;
- protocol parser and encoder state;
- link-local connection and session slots;
- request slots, cancellation state, response cache, and correlation;
- session authorization grants;
- subscriptions, negotiated rates, cursors, and drop accounting;
- per-session output lanes and scheduler state;
- frame, fragment, request, response, and inbound-value pools;
- transport-independent timeouts, retry, and duplicate bookkeeping;
- focused link, session, request, and delivery records.

The service does not own canonical application values.

### 4.4 Link ownership

A link owns or references:

- one transport owner's asynchronous interface;
- transport RX buffers or RX buffer leases;
- transport TX in-flight state;
- connection detection and transport faults;
- transport-specific initialization and shutdown adaptation;
- static configuration such as device identity, TCP port, or maximum sessions.

A link does not own schemas, Action dispatch, subscriptions, authorization
policy, or canonical Remote catalogs.

### 4.5 Source ownership remains intact

Parameters own parameter values and persistence. Metrics own instruments and
reducers. Events own event capture and history. Logging owns log ingress and
history. Components own application state. Remote only acquires coherent copies
or consumes explicit source records through adapters.

### 4.6 No context or runtime root

Remote code does not use `ContextT::SystemType`, receive a system object, or
look up dependencies through a runtime context.

The ordinary application includes the headers for the static types it calls.
The composition root binds the system once, and the generic catalog collector
derives Remote's effective static surface.

## 5. System Composition And Catalogs

### 5.1 Reserved contribution aliases

Remote reserves these ergonomic component-local aliases:

```cpp
RemoteData
RemoteActions
RemoteTopics
RemoteStreams
RemoteLinks
```

Each alias is optional and independently collected through the generic Phase 2
contribution protocol.

Example:

```cpp
struct Imu
{
    using RemoteData = solar::remote::ContributeData<ImuTelemetry>;
    using RemoteActions = solar::remote::ContributeActions<CalibrateImu>;
};
```

Components do not need a combined `Contributions` alias for this ordinary path.

### 5.2 Catalog kinds

Remote defines distinct catalog kinds for:

- schema types;
- Data declarations;
- Actions;
- standalone Topics;
- standalone Streams;
- links;
- normalized exposure adapters;
- generated protocol endpoints.

Authored catalogs preserve owner and origin. Generated protocol endpoints retain
the authored declaration that caused them to exist.

### 5.3 Data normalization

A Data declaration may expand into protocol operations for Query, Update,
Watch, OutStream, and InStream. Those generated entries do not become authored
Actions, Topics, or Streams and do not erase the parent Data identity.

The manifest and inspection surfaces can show both:

```text
Data: imu.telemetry
  Query
  OutStream
```

### 5.4 Standalone advanced declarations

Standalone Topics and Streams remain public for protocol-native capabilities
that do not naturally describe one queryable Data resource.

Examples include:

- one-shot operational notices;
- a structured trace feed;
- a chunked transfer protocol;
- a source that intentionally has no queryable current value.

The common path should prefer Data plus capabilities where one stable semantic
resource is both queryable and streamable.

### 5.5 Remote configuration

Exact architecture belongs in typed Remote configuration:

```cpp
using RemoteConfiguration = solar::remote::Configuration<
    solar::remote::Engine<solar::remote::DedicatedService>,
    solar::remote::DefaultActionExecution<
        solar::execution::SystemWorkQueue>,
    solar::remote::Expose<
        RemoteParameters,
        RemoteMetrics,
        RemoteLogs>>;
```

Kconfig determines capability inclusion and hard ceilings. Typed configuration
selects actual links, exposures, targets, policies, and declarations within
those ceilings.

### 5.6 Automatic effective facility and service

When Remote is enabled and the effective link catalog is non-empty:

- the Remote facility is an effective built-in facility;
- the Remote service is an effective built-in service component;
- both appear in compile-time graph and focused inspection;
- the service's thread, stack, queues, and buffers are visible resource costs.

The user does not manually list either built-in type. Explicit link declarations
and Kconfig make their inclusion intentional.

When no link and no in-memory test engine are selected, no Remote service,
session storage, or transport buffer is emitted.

## 6. Remote Service Execution

### 6.1 Two execution decisions

Remote separates:

1. protocol-engine execution; and
2. decoded Action, query, update, and inbound-consumer execution.

Changing one does not silently change the other.

### 6.2 Dedicated service default

The default protocol engine is one dedicated nonessential Solar service thread.
It owns an explicit Kconfig-selected stack and priority and participates in
normal service lifecycle.

The service waits for small bounded events such as:

```text
LinkConnected
LinkDisconnected
RxReady
TxComplete
LinkFault
PublicationReady
ResponseReady
TimeoutExpired
StopRequested
```

Event entries carry local IDs, generations, lease handles, lengths, and compact
status. They do not copy arbitrary frames or application payloads through the
event queue.

### 6.3 Callback rule

Driver, DMA, UART interrupt, socket-service, and timer callbacks may:

- commit bytes to transport-owned RX storage;
- update one transport-local atomic or bounded record;
- enqueue or coalesce a small event with no wait;
- wake the Remote service.

They may not:

- COBS-decode a complete protocol message;
- CBOR-decode application values;
- invoke an Action or source reader;
- fan out publications to sessions;
- format logs;
- wait for Remote storage;
- perform blocking transport output.

### 6.4 Bounded pump

The engine is implemented as a deterministic bounded `pump()` state machine.
One invocation processes configured budgets for:

- link events;
- RX bytes or complete frames;
- request dispatch;
- completed responses;
- publication fan-out;
- timeout work;
- output scheduling;
- transport submission.

If work remains, the dedicated service loops or yields according to policy.
No one busy link may consume an unbounded service iteration.

### 6.5 Workqueue engine alternative

Typed configuration may place the protocol pump on a Phase 9 execution target:

```cpp
solar::remote::EngineExecution<
    solar::execution::On<CommunicationsQueue>>
```

Callbacks then submit one coalesced Remote pump registration. The pump never
blocks waiting for another event and resubmits itself when a bounded budget
leaves work pending.

This mode removes the dedicated Remote thread but does not create an implicit
private workqueue.

### 6.6 Fairness

The service processes links and sessions with bounded round-robin budgets.

Fairness does not promise equal bandwidth. Protocol control and accepted RPC
responses intentionally outrank telemetry and bulk streams.

## 7. Link Contract

### 7.1 Link is an asynchronous adapter

A valid link exposes transport-independent operations equivalent to:

```cpp
static solar::Result<void, LinkError> open(LinkEventSink sink);
static solar::Result<TxDisposition, LinkError> try_transmit(TxLease lease);
static void close();
```

The exact helper concepts may vary, but the semantics are fixed:

- `open` arms asynchronous reception and connection reporting;
- received bytes are committed through an RX lease before `RxReady` is raised;
- `try_transmit` never waits for physical completion;
- accepted TX storage remains leased until `TxComplete` or fault;
- `Busy` retains ownership with the Remote service;
- `close` prevents new callbacks before transport storage is released.

### 7.2 No artificial synchronous transport concept

Remote does not require a transport to provide one static combination of
`read`, `write`, `available`, and `flush`.

UART polling, interrupt UART, DMA UART, USB CDC's UART facade, non-blocking
sockets, Zephyr socket services, RTIO-backed links, and in-memory tests have
different native completion models. Links normalize events and buffer ownership,
not mechanics.

### 7.3 RX ownership

An RX event identifies bytes whose lifetime remains valid until the Remote
service releases the RX lease or advances the transport ring.

The service may feed partial spans into an incremental COBS decoder. Frame
boundaries need not align with DMA, FIFO, USB packet, socket read, or test input
boundaries.

### 7.4 TX ownership

The Remote service submits complete framed byte spans through a move-only TX
lease. A link may transfer them through DMA, FIFO interrupts, non-blocking socket
writes, or a test sink.

Short writes advance the same lease. They do not cause frame re-encoding.

### 7.5 Multiple links

Each link has distinct:

- transport state;
- connection and session slots;
- RX parser state;
- TX in-flight leases;
- link records and fault state;
- trust and authentication policy.

All links share one canonical schema, Data, Action, Topic, Stream, and exposure
surface for the firmware build.

### 7.6 Blocking transports

A transport that only offers blocking operations is not directly a valid
Remote link. Its adapter must provide asynchronous completion through an
explicit transport-owned executor or service.

That additional execution remains visible and is not hidden inside the Remote
facility.

## 8. End-To-End Asynchronous Path

### 8.1 RX

```text
driver or socket callback
    -> commit RX bytes
    -> enqueue RxReady(link, lease)
    -> Remote service wakes
    -> incremental framing and integrity check
    -> authorization and bounded decode
    -> schedule application execution
```

### 8.2 TX

```text
application acquisition or completed Action
    -> Remote-owned staging
    -> enqueue/coalesce ready event
    -> per-session selection and encoding
    -> priority lane
    -> frame and fragment scheduler
    -> link.try_transmit(lease)
    -> TxComplete releases lease
```

### 8.3 Producer independence

`publish` and `write` do not call a link. A slow, disconnected, or absent link
cannot extend producer execution beyond the selected bounded admission path.

## 9. Protocol Layering

Remote separates these layers:

```text
transport bytes
    -> COBS framing
    -> CRC32C integrity
    -> fixed protocol envelope
    -> optional fragmentation
    -> message kind and target
    -> CBOR or packed payload codec
    -> typed schema
```

No payload codec is responsible for byte-stream resynchronization. No link is
responsible for application schema dispatch.

## 10. Framing And Integrity

### 10.1 COBS framing

The initial protocol uses standard zero-delimited COBS on UART, USB CDC, TCP,
and test links.

Using one frame boundary across all initial links keeps one host parser and one
set of protocol golden vectors. A future datagram link may omit outer COBS only
through explicit negotiated framing capability.

### 10.2 CRC32C

Each decoded frame ends with CRC32C over the protocol envelope and payload
before COBS encoding.

CRC failure:

- discards the complete frame;
- increments focused link and session integrity records where a session can be
  identified safely;
- never dispatches a partially decoded request;
- resumes at the next COBS delimiter.

CRC provides accidental-corruption detection. It is not authentication.

### 10.3 Envelope semantics

The versioned envelope carries at least:

- protocol major and minor;
- frame kind;
- flags and header length;
- session epoch or connection generation;
- per-direction frame sequence;
- typed target ID;
- request ID where applicable;
- payload length;
- fragment identity, index, and count where applicable.

The initial envelope uses a fixed documented byte order and bounded integer
widths. Exact offsets are frozen with protocol version 1 golden vectors during
implementation and cannot follow native C++ structure layout.

### 10.4 Frame and message limits

Frame payload length is bounded by link and system hard ceilings. Logical
messages may be larger only when the endpoint explicitly permits bounded
fragmentation.

Malformed, oversized, inconsistent, or incomplete messages are rejected before
typed dispatch.

## 11. Handshake, Versions, And Capabilities

### 11.1 Fresh session handshake

Every connection or explicit UART session reset begins with:

```text
ClientHello -> ServerHello -> authentication -> Ready
```

Application requests, subscriptions, and inbound streams are rejected before
Ready.

### 11.2 Hello metadata

Negotiation includes:

- protocol major and minor;
- frame and logical-message limits;
- supported framing and payload codecs;
- schema SHA-256 digest;
- firmware build identity;
- session epoch and nonce;
- feature capabilities;
- runtime introspection availability;
- authentication mechanism and initial grants;
- keepalive and timeout constraints.

### 11.3 Compatibility

- a protocol-major mismatch rejects the session;
- minor differences are accepted only through explicit capability negotiation;
- an unknown mandatory capability rejects the session;
- a schema mismatch does not silently reinterpret payloads;
- a client with the matching generated manifest may proceed directly;
- a mismatched client may use runtime introspection only when enabled and when
  the required schemas remain compatible.

### 11.4 Reconnect

Reconnect creates a fresh session with:

- no subscriptions;
- no inherited permissions beyond fresh authentication;
- no in-flight requests;
- no retained stream credits;
- new request and frame sequence space;
- no at-most-once guarantee across the old and new sessions.

## 12. Schemas And Payload Encoding

### 12.1 Explicit schema contract

Every external payload has:

- an explicit typed `TypeId`;
- stable name and description;
- schema version;
- bounded encoded and decoded size;
- numbered fields;
- field type, optionality, default, and bounds;
- enum or unit metadata where relevant;
- one supported codec declaration.

Raw C++ layout is never a schema.

### 12.2 Deterministic CBOR default

Ordinary structured payloads use deterministic CBOR with integer field keys.

Initial restrictions include:

- no indefinite-length strings, arrays, or maps;
- bounded nesting;
- bounded strings, byte strings, and collections;
- no unregistered semantic tags;
- preferred deterministic integer and floating encodings;
- unknown optional fields may be skipped;
- missing required fields fail decoding;
- duplicate field keys fail decoding;
- trailing unconsumed payload data fails decoding.

Solar uses Zephyr's zcbor capability rather than maintaining a second general
CBOR implementation.

### 12.3 Packed stream codec

High-rate Streams may explicitly use `remote::Packed`.

Packed schemas define:

- exact field order;
- exact integer widths and signedness;
- exact floating representation;
- fixed byte order;
- optional presence representation;
- sample stride;
- batch header and timestamp-delta representation;
- schema version and maximum batch size.

Packed is never inferred from a trivially copyable C++ type.

### 12.4 Identity domains

Remote uses distinct typed 32-bit external identity domains for:

- Schema types;
- Data;
- Actions;
- standalone Topics;
- standalone Streams.

Data capability frames carry a Data ID plus an operation kind. Querying and
streaming the same Data therefore do not require unrelated external IDs.

Field IDs are local to one Schema and use a compact unsigned domain.

### 12.5 Explicit stable IDs

Externally visible IDs are explicit or controlled by a versioned manifest.
They are not derived from:

- C++ type names;
- compiler pretty-function text;
- source paths;
- names through FNV or another unchecked hash;
- local catalog order;
- schema version arithmetic.

### 12.6 Evolution

Compatible evolution may:

- add optional fields with new field IDs;
- widen documented accepted enum knowledge while retaining unknown handling;
- add capabilities or new endpoints;
- increase limits only when negotiation preserves the old bound.

Compatible evolution retains the stable identity and increments schema
metadata where useful.

Breaking reinterpretation, required-field replacement, incompatible packed
layout, or changed semantic meaning requires a new Schema or endpoint identity.

## 13. Data And Capabilities

### 13.1 Data declaration

Data gives one stable external identity to a semantically coherent value source
or sink independently from how it is communicated.

```cpp
struct ImuTelemetry
{
    static constexpr solar::remote::Descriptor descriptor{
        .id = solar::remote::DataId{0x2001},
        .name = "imu.telemetry",
        .description = "Latest calibrated body-frame IMU sample",
    };

    using Value = ImuSample;

    using Capabilities = solar::remote::Capabilities<
        solar::remote::Query<&Imu::read_latest>,
        solar::remote::OutStream<
            solar::remote::Push,
            solar::remote::Packed,
            solar::remote::MaxRate<500_Hz>,
            solar::remote::Batch<16>,
            solar::remote::DropOldest>>;
};
```

The declaration is static metadata and policy. It is not a runtime Data object.

### 13.2 Initial capabilities

The initial Data capability vocabulary is:

- `Query<Reader>`: acquire one coherent value for the host;
- `Update<Writer>`: apply one validated host-provided value;
- `Watch<Policy...>`: publish discrete changes or occurrences;
- `OutStream<Acquisition, Policy...>`: send sustained device-produced values;
- `InStream<Consumer, Policy...>`: receive sustained host-produced values.

A declaration may support any valid subset.

### 13.3 Capability independence

Capabilities on one Data identity may use different acquisition and execution
policies.

For example:

- Query may call a mutex-protected canonical reader;
- OutStream may receive values pushed directly by a control loop;
- Update may execute on a configuration queue;
- Watch may consume an existing parameter change sequence;
- InStream may deliver decoded setpoints to a service-owned mailbox.

No capability silently supplies another capability's storage or synchronization.

### 13.4 Different meaning requires different Data

`ImuSummary`, `RawImuSamples`, and `ImuCalibrationState` should be separate Data
declarations when their values, rates, authorization, or semantic meaning differ.

Data is not a container for loosely related fields merely because they originate
from the same component.

## 14. The Acquisition Boundary

### 14.1 Fundamental invariant

Remote may encode only:

- an owned coherent value;
- an immutable value with a lifetime that covers the complete operation; or
- a move-only generation-checked buffer lease.

Remote never serializes a borrowed reference to mutable system state.

### 14.2 Separate acquisition from transmission

Every exported value follows:

```text
source state
    -> source-owned synchronization
    -> owned coherent acquisition
    -> Remote staging
    -> encoding and fan-out
    -> per-session queue
    -> framed link transmission
```

Source synchronization ends before Remote encoding, session locking, queueing,
or transport submission begins.

### 14.3 No address registration

Declarations equivalent to these are rejected:

```cpp
remote::Query<&Imu::latest_value>
remote::Stream<&ControlLoop::live_state>
```

An address does not explain atomicity, lock ownership, lifetime, consistency,
availability, execution context, or failure.

### 14.4 Source owns synchronization

The source reader or writer owns whether coherent access uses:

- a lock-free atomic operation;
- a mutex and bounded wait;
- a spinlock around a very short fixed-size copy;
- a source-owned queue;
- a double or multiple buffer with valid ownership transfer;
- a subsystem-specific transactional API;
- another explicitly documented safe mechanism.

Remote does not infer a synchronization policy from `Value` traits.

### 14.5 Lock rule

Remote does not hold any session, request, lane, manifest, or link lock while:

- calling a source reader or writer;
- invoking an Action;
- invoking an inbound consumer;
- waiting on application execution;
- acquiring an application mutex;
- encoding under a source-owned lock.

This is a hard lock-ordering boundary.

## 15. Query Acquisition

### 15.1 Reader forms

A Query reader returns an owned value:

```cpp
static solar::Result<ImuSample, ReadError> read_latest();
```

or writes directly into caller-owned destination storage:

```cpp
static solar::Result<void, ReadError>
read_latest(ImuSample &destination);
```

The direct destination form avoids another large-value copy while preserving
ownership.

### 15.2 Rejected reader returns

The initial Query concept rejects unleased:

- `T&` and `const T&`;
- raw pointers;
- `std::span`;
- `std::string_view` into mutable storage;
- iterators into live containers;
- handles whose lifetime is undocumented.

Bounded owning strings, arrays, byte buffers, and aggregate values are valid.

### 15.3 Query execution

A query follows:

1. reserve request, decoded-value, and response capacity;
2. validate session state, capability, and authorization;
3. decode bounded query arguments where the capability defines any;
4. submit the reader to its effective execution target;
5. acquire one coherent value under source synchronization;
6. release source synchronization;
7. encode into Remote-owned response storage;
8. enqueue `ResponseReady` for the Remote service;
9. deliver or retain the response according to request semantics.

### 15.4 Query execution target

Query execution follows the same precedence as Action execution:

```text
capability execution
    > Data declaration default
    > Remote typed configuration
    > Remote Kconfig default
```

An explicitly lock-free, bounded reader may select `remote::Inline`. The normal
default remains the system workqueue so an application lock cannot block the
protocol service.

### 15.5 Read errors

Reader errors remain typed at the source boundary and are mapped into a stable
external error schema.

At minimum Remote distinguishes:

- unavailable before source readiness;
- busy or synchronization timeout;
- unsupported in the current source mode;
- stale where the source can prove staleness;
- source-specific structured failure;
- internal capacity or execution rejection.

### 15.6 Query metadata

A query result envelope may carry:

- source sequence;
- source timestamp;
- acquisition timestamp;
- freshness or quality flags;
- schema version.

When the source returns only `Value`, Remote records acquisition completion time
and does not invent a source timestamp or sequence.

### 15.7 Explicit latest-published query

Rapid prototypes may opt into:

```cpp
remote::Query<remote::LatestPublished>
```

This makes the most recently accepted pushed sample the declared query source.
It is never inferred. Querying before the first accepted publication returns
`Unavailable`, and the result metadata reports the publication timestamp and
sequence.

Standard subsystem adapters use canonical subsystem readers instead.

When `LatestPublished` is selected, every accepted pushed value updates that
declared query source even when no session is currently subscribed. In that
case `NoSubscribers` means no live fan-out occurred, not that the latest value
was discarded.

## 16. Outbound Stream Acquisition

### 16.1 Acquisition mode is mandatory

Every outbound Stream declares exactly one initial acquisition mode:

- `Push`;
- `Poll<Reader, Target...>`;
- `Loaned<PoolPolicy...>`.

There is no implicit polling of a variable and no hidden callback registration.

### 16.2 Push acquisition

The producing context submits the value when it exists:

```cpp
void ControlLoop::step()
{
    TracePoint point = calculate_trace();

    ControlState::store(point);
    (void)solar::remote::write<ControlTrace>(point);
}
```

`write` performs bounded stream admission and wakeup only. It does not:

- encode CBOR or packed batches;
- iterate sessions;
- check authorization per session;
- fragment messages;
- call a link;
- wait for buffer space.

Push is the normal path for control loops, sensor completion, event-like data,
and values available only at a source-defined instant.

### 16.3 Poll acquisition

Poll acquisition binds a reader to a Phase 9 execution target:

```cpp
solar::remote::OutStream<
    solar::remote::Poll<
        &PoseEstimator::read,
        solar::execution::On<TelemetryQueue>>,
    solar::remote::MaxRate<100_Hz>>
```

The effective poll registration:

- activates when the first authorized session subscribes;
- stops when the last subscription disappears;
- samples once at the highest effective requested rate;
- clamps every request to declared minimum and maximum periods;
- fans one acquired value out to eligible sessions;
- independently downsamples for slower sessions;
- permits at most one read in flight;
- records and skips a release when the prior read remains active;
- never accumulates an unbounded backlog of sample requests.

Poll registration is infrastructure work, not an application task.

### 16.4 Query and poll interaction

A Query and Poll capability may call the same source reader. The source remains
responsible for safe concurrent calls unless the Data declaration serializes
them through one target.

Remote does not automatically answer Query from the last polled sample. That
optimization requires explicit `LatestPublished` or another declared cache.

### 16.5 Loaned acquisition

Large, DMA-produced, or expensive-to-copy payloads may use a bounded staging
lease:

```cpp
auto lease = solar::remote::try_loan<LidarChunks>();

if (lease)
{
    LidarDma::start(lease->data(), lease->capacity());
}
```

Completion commits the produced length and metadata:

```cpp
solar::remote::commit(
    std::move(lease),
    produced_bytes,
    remote::Timestamp{capture_time});
```

A lease is move-only, generation checked, and belongs to one declared Stream.

### 16.6 Lease containment

A producer lease is released after bounded Remote ingestion or encoding. It is
not retained directly by an arbitrary number of session queues and is never
held indefinitely by one slow transport.

If Remote cannot accept a committed lease under the Stream policy, it records a
drop and releases the lease promptly.

End-to-end zero-copy across multiple sessions is not an initial guarantee.

## 17. Push Storage And Publication API

### 17.1 Static ingress storage

Each pushed Data or standalone Stream owns only the ingress storage selected by
its policy. Initial policies include:

- `Latest`: one pending coherent value; a newer value replaces it;
- `Queue<N>`: retain up to `N` complete values;
- `Batch<N>`: encode up to `N` accepted values in one stream message;
- `LoanedPool<N, Bytes>`: `N` bounded producer staging leases;
- `SingleProducer`: permit a specialized SPSC path;
- `MultipleProducers`: require safe MPSC admission.

Unused policy storage is absent.

### 17.2 Ordinary write

```cpp
solar::remote::write<DataOrStream>(value);
```

Ordinary write is thread-safe for the declared producer policy, non-waiting,
and bounded. It accepts an owned copy or move into ingress storage.

### 17.3 ISR write

```cpp
solar::remote::write_from_isr<DataOrStream>(value);
```

The ISR API is available only when:

- the value and selected storage have an ISR-safe bounded copy path;
- the stream declares ISR admission;
- no mutex, allocation, blocking execution, or deferred destructor is required;
- the supported platform memory-ordering contract is satisfied.

Calling ordinary `write` from ISR context is not silently accepted.

### 17.4 Write result

Write returns expected-style structured outcome.

Successful dispositions include:

- `Accepted`;
- `ReplacedOlder`;
- `Coalesced`;
- `NoSubscribers`.

Failures include:

- facility or Stream not ready;
- queue full under reject policy;
- unsupported execution context;
- invalid or oversized value;
- committed lease generation mismatch;
- stream admission closed during shutdown.

`NoSubscribers` is not a broad error.

### 17.5 Advisory interest

```cpp
if (solar::remote::interested<ControlTrace>())
{
    (void)solar::remote::write<ControlTrace>(build_expensive_trace());
}
```

Interest is a cheap advisory atomic observation. It may change immediately and
must not affect application correctness.

For `LatestPublished`, interest remains true while publication admission is
open because every accepted write contributes to the declared query source.

### 17.6 Wakeup coalescing

Ingress storage tracks whether a `PublicationReady` event is already pending.
Many accepted samples may therefore cause one Remote service wakeup without
losing the Stream's own sequence or admission semantics.

## 18. Topics, Watch, And Discrete Publication

### 18.1 Topic semantics

A Topic represents a discrete typed device-to-host publication rather than a
high-rate sample sequence.

```cpp
solar::remote::publish<GoalChanged>(goal);
```

The same acquisition rule applies: `publish` copies or moves an owned value into
bounded Topic ingress. It does not borrow live state.

### 18.2 Watch capability

`Watch` is the Data-oriented common frontend for discrete change publication.
It normalizes to Topic-like subscription and delivery machinery while retaining
the parent Data ID and capability identity.

### 18.3 Topic queueing

Initial Topic policies include:

- `Latest`: retain only the newest pending state-like change;
- `Queue<N, DropOldest>`;
- `Queue<N, DropNewest>`;
- `Queue<N, Reject>` where caller-visible rejection is acceptable.

`NeverDrop` is not a valid unqualified policy. Guaranteed admission requires
reserved capacity and an explicit exhaustion action.

### 18.4 No generic host publish

The initial protocol does not allow a host to publish arbitrary Topics into the
firmware.

Discrete host intent uses an Action or Update. Sustained host data uses
InStream. Explicit bus bridges use one of those mechanisms and remain
authorized adapters.

## 19. Inbound Data

### 19.1 Update

Update represents one validated request/response mutation of Data.

The writer receives an owned decoded value and invokes the source's canonical
setter or transaction API. Remote never writes a registered memory address.

### 19.2 InStream use cases

InStream is intended for:

- teleoperation setpoints;
- trajectory point sequences;
- map or grid uploads;
- firmware or asset chunks;
- simulation sensor injection;
- other sustained or bulk host-produced values.

One-off commands remain Actions.

### 19.3 Inbound ownership path

```text
link frame
    -> verify and reassemble
    -> authorize
    -> decode into Remote-owned value slot
    -> admit against stream credits
    -> schedule declared consumer
    -> consumer completes or transfers ownership
    -> release slot and return credit
```

The host cannot overwrite an in-flight value slot.

### 19.4 Consumer execution

An InStream consumer selects `Inline`, `On<Target>`, a typed service mailbox, or
another explicit asynchronous target under the same execution principles as an
Action.

Inline is permitted only for bounded non-blocking consumers.

### 19.5 Credits and admission

Inbound Streams advertise bounded credits derived from actually available
decode and consumer slots.

A host may send only within granted credits. Credit violation is a protocol
error and may close the stream or session according to security policy.

### 19.6 Reliability profiles

Initial inbound profiles are:

- `Latest`: suitable for replaceable teleoperation state;
- `ReliableWindow<N>`: ordered, acknowledged bounded chunks;
- `RejectWhenBusy`: no queueing beyond the active consumer slot.

Reliable inbound delivery acknowledges acceptance into owned firmware storage,
not successful application use unless the endpoint explicitly defines
completion acknowledgement.

### 19.7 Duplex Data

Duplex Data is modeled as paired OutStream and InStream capabilities under one
Data identity.

The directions retain independent:

- sequence spaces;
- credits;
- rates;
- codecs where explicitly compatible;
- authorization;
- loss and completion records.

There is no one ambiguous shared duplex queue.

## 20. Actions

### 20.1 Public model

An Action is a typed request/response operation:

```cpp
struct CalibrateImu
{
    static constexpr solar::remote::Descriptor descriptor{
        .id = solar::remote::ActionId{0x0102},
        .name = "imu.calibrate",
        .description = "Run stationary IMU calibration",
    };

    using Request = CalibrationOptions;
    using Response = CalibrationReport;
    using Error = CalibrationError;
    using Access = solar::remote::Requires<permission::Control>;

    static solar::Result<Response, Error>
    execute(const Request &request);
};
```

### 20.2 Defaults

An omitted Action member means:

- Request: `remote::Empty`;
- Response: `remote::Empty`;
- Error: `solar::Status`;
- Access: no application grant beyond the link's explicit baseline policy;
- timeout: Remote configured default;
- execution: effective Remote Action execution target.

No Action declaration defaults to privileged access.

### 20.3 Request construction

Incoming arguments are decoded into one owned bounded Request object according
to its Schema.

Remote does not map wire fields directly onto arbitrary C++ function parameters.
The Request type is the stable wire contract and may define a validation hook.

### 20.4 Accepted returns

Initial synchronous Action returns are:

- `Response`;
- `solar::Result<Response, Error>`;
- `void` when Response is Empty;
- `solar::Result<void, Error>` when Response is Empty.

Boolean and arbitrary result-like returns are rejected.

### 20.5 Domain and protocol errors

Action `Error` describes an accepted Action's domain failure.

Protocol errors remain separate and include malformed input, unknown target,
unauthorized access, unsupported capability, capacity rejection, timeout,
cancellation, and session loss.

## 21. Action And Capability Execution

### 21.1 Effective target precedence

Application execution follows:

```text
Action or capability target
    > Data or adapter target
    > Remote typed configuration
    > Kconfig default
```

The initial Kconfig default is Zephyr's system workqueue.

This protects the Remote protocol service from casually blocking application
code without creating a hidden executor or private stack.

### 21.2 Inline

```cpp
using Execution = solar::remote::Inline;
```

Inline invokes the reader, writer, consumer, or Action on the Remote service or
pump context. It is valid only for bounded, non-blocking, lock-disciplined work.

Inline is explicit rather than the ordinary default.

### 21.3 Workqueue-deferred

```cpp
using Execution = solar::remote::On<ControlQueue>;
```

Remote retains the decoded Request or value slot, cancellation state, timeout,
authorization result, and response reservation while Phase 9 owns submission
to the selected target.

### 21.4 Typed mailbox

```cpp
using Execution = solar::remote::Mailbox<NavigationCommands>;
```

Mailbox execution is available when the named application or service type
exposes a compatible typed bounded mailbox contract.

Remote copies or moves the decoded value into that mailbox and retains request
correlation according to the mailbox's response contract. Remote does not fake
a general service mailbox with a workqueue and does not create one implicitly.

### 21.5 Asynchronous responder

An asynchronous Action may accept a move-only responder:

```cpp
static void execute(
    const StartCalibration::Request &request,
    solar::remote::Responder<StartCalibration> responder);
```

The responder:

- references a pre-reserved response slot;
- is bound to one session generation and request ID;
- may complete exactly once with success or domain failure;
- observes cancellation and timeout;
- rejects completion after session reset, timeout finalization, or generation
  reuse;
- does not own a heap promise.

### 21.6 No arbitrary abort

Remote may cancel pending execution and signal a cancellation token to running
cooperative work. It never asynchronously aborts arbitrary C++ code or one
shared workqueue handler.

## 22. Requests, Correlation, And Duplicate Suppression

### 22.1 Admission before dispatch

Before accepting execution, Remote reserves:

- one request slot;
- decoded Request or inbound value capacity;
- response or error capacity;
- execution admission;
- timeout and cancellation state.

If those cannot be reserved, the Action does not run.

### 22.2 Request IDs

Each session uses monotonically advancing nonzero request IDs selected by the
host. An ID is never reused within one session. Exhausting the request-ID space
requires a fresh session before another Action can be admitted. Responses may
complete out of order and always carry the originating ID.

### 22.3 At-most-once within one session

Remote maintains a bounded duplicate-suppression window independent from the
response payload cache.

- a duplicate pending ID refers to the existing request;
- a duplicate completed ID replays the cached response when available;
- a recognized duplicate whose response was evicted returns
  `DuplicateResponseExpired` and never re-executes;
- an ID older than the retained acceptance window is rejected as expired;
- reconnect begins a new session and does not preserve this guarantee.

This provides at-most-once accepted execution within one live session without
claiming exactly-once delivery.

### 22.4 Response acknowledgement

The host acknowledges completed Action responses. Acknowledgement permits early
response-cache release but does not erase the duplicate-suppression window.

If a response is lost, the host retries the same request ID. It does not invent
a new ID for a side-effecting retry.

### 22.5 Timeout

Timeout policy belongs to the Action or generated capability operation, with
Remote configuration providing defaults and hard ceilings.

Timeout:

- closes response admission for that generation;
- requests cooperative cancellation;
- reports a stable timeout error;
- does not prove arbitrary running code stopped;
- rejects a late asynchronous response.

### 22.6 Cancellation

A host cancellation references one request ID.

- pending work is cancelled when the execution foundation can prove removal;
- running cooperative work observes a stop token or responder state;
- a non-cooperative running Action may finish internally after the host receives
  cancellation or timeout;
- late output is discarded safely;
- cancellation is idempotent.

### 22.7 Session loss

Disconnect requests cancellation for every active request, closes all response
slots, invalidates responders by generation, and releases session-owned decoded
storage when containment is proven.

Application work that cannot be synchronously cancelled follows its declared
dependency-preservation and stop policy.

## 23. Sessions And Authorization

### 23.1 Session ownership

Each session owns:

- link and connection generation;
- handshake and authentication state;
- permission grants;
- request ID window and response cache;
- subscriptions and requested rates;
- inbound credits;
- output lanes;
- frame sequences and stream cursors;
- timeout and keepalive state;
- focused delivery and protocol records.

### 23.2 Initial permission vocabulary

The initial independent grant vocabulary is:

- `Observe`: query, inspect, watch, or stream permitted read-only data;
- `Configure`: update externally configurable values;
- `Control`: invoke behavior or provide live control inputs;
- `Admin`: lifecycle control, persistence administration, security-sensitive
  operations, or similarly exceptional authority.

Grants are not automatically a hierarchy. A policy may map a role to several
grants explicitly.

### 23.3 Endpoint access

Actions and capabilities declare required grants:

```cpp
using Access = solar::remote::Requires<
    permission::Observe,
    permission::Control>;
```

Authorization occurs before application payload decoding where the envelope
contains enough target information to decide safely.

### 23.4 Link trust is explicit

A local USB or test link is not silently privileged. Its link policy may
explicitly grant a development role after connection.

A TCP link must not default to privileged unauthenticated control. TLS or
another transport security mechanism may establish identity, while Remote maps
that identity to grants.

Permissions do not provide confidentiality or cryptographic authenticity by
themselves.

### 23.5 No ambient session object

Ordinary Action, reader, writer, and consumer APIs receive no mutable session
object.

An endpoint that genuinely requires metadata may explicitly request a focused
immutable `CallInfo` containing permitted fields such as principal, request ID,
deadline, or correlation. This is not a system context and does not provide raw
queue or transport access.

## 24. Subscriptions And Rates

### 24.1 Per-session state

Subscriptions are always session-local. One client's filter, rate, codec,
cursor, or backpressure cannot alter another client's state.

### 24.2 Subscription negotiation

A subscription request may include:

- Data capability, Topic, or Stream ID;
- requested rate or minimum interval;
- requested batch size;
- supported codec where the endpoint permits choice;
- starting mode such as live-only or retained-page-then-live;
- endpoint-specific filters;
- requested reliability profile.

The response returns the effective bounded policy rather than assuming every
request was accepted exactly.

### 24.3 Rate clamping

Effective rate is constrained by:

- endpoint minimum and maximum;
- acquisition capability;
- Kconfig hard ceiling;
- typed system policy;
- authorization role;
- current resource availability.

Remote never asks a source to sample faster than its declared maximum.

### 24.4 Unsubscribe

Unsubscribe removes future fan-out and releases session-specific queued
telemetry. It does not remove canonical source history or abort a shared source
acquisition still needed by other sessions.

### 24.5 Poll activation

The first effective subscription activates one poll registration. The final
unsubscribe, disconnect, or shutdown deactivates and synchronizes it according
to Phase 9 execution rules.

## 25. Per-Session Queues And Backpressure

### 25.1 Separate lanes

Each session initially has independently bounded lanes for:

1. protocol control and handshake;
2. accepted Action responses and capability results;
3. important discrete publications;
4. ordinary telemetry, metrics, events, and logs;
5. bulk and high-rate stream fragments.

The exact implementation may combine storage pools, but admission and scheduling
guarantees remain distinguishable.

### 25.2 Response reservation

Accepted requests reserve enough control or response capacity to produce one
bounded success or failure response unless the link disconnects.

Telemetry may not consume this reservation.

### 25.3 Weighted scheduler

Output selection uses strict protection for protocol liveness and accepted
responses plus bounded weighted service for lower lanes.

Bulk traffic cannot permanently starve important publications, while continuous
important traffic cannot make all telemetry permanently impossible without a
recorded policy decision.

### 25.4 Slow-client policy

Endpoint and link policy may select:

- replace older pending value;
- drop oldest;
- drop newest;
- reduce effective rate;
- terminate one subscription;
- disconnect the session.

No policy may block the system producer waiting for a client.

### 25.5 Canonical source independence

Per-session queues contain delivery staging, encoded messages, or source
cursors. They do not become canonical event history, log history, metric state,
parameter storage, or application state.

### 25.6 Disconnect containment

Disconnect releases only that session's lanes, subscriptions, credits,
responses, and fragments. Other links and sessions continue independently.

## 26. Sequence, Time, Batching, Fragmentation, And Loss

### 26.1 Sequences

Remote maintains distinct sequence domains for:

- link frames per direction;
- Topic or Watch occurrences;
- outbound Stream samples;
- inbound Stream samples;
- retained-source cursors where exposed.

Sequence wrap behavior is fixed by the declared width and interpreted modulo
that width. Reconnect resets session-local sequence state but not canonical
source sequences.

### 26.2 Timestamps

Pushed values may provide a source timestamp. Polled values may return source
time or receive acquisition-completion time. Remote transmission time remains
separate.

Batch payloads may encode one base timestamp plus bounded deltas.

### 26.3 Loss reporting

Every lossy Topic, Watch, or Stream report can include:

- first and last sample sequence;
- samples represented;
- samples dropped before Remote admission;
- samples dropped during per-session fan-out;
- samples skipped by rate selection;
- source loss where the adapter can report it;
- discontinuity after reconnect or cursor reset.

Remote never labels a lossy stream reliable merely because TCP delivered the
frames it accepted.

### 26.4 Batching

Batching is endpoint policy constrained by:

- maximum samples;
- maximum encoded bytes;
- maximum batching latency;
- session-requested rate;
- frame and fragment limits.

A batch contains complete sample boundaries. Overflow never emits half a typed
sample.

### 26.5 Fragmentation

Fragmentation is permitted only for endpoints with a bounded maximum logical
message size.

Each fragment identifies:

- logical message;
- endpoint and sequence;
- fragment index and count;
- total logical size where required;
- integrity through the enclosing frame CRC.

Reassembly uses statically bounded slots and timeouts.

### 26.6 Head-of-line prevention

The scheduler may interleave fragments from a large bulk message with protocol,
response, and important lanes.

A large map or grid should normally be represented as revisioned tiles or
chunks over a Stream rather than one maximum-sized monolithic response.

## 27. Explicit Subsystem Exposure

### 27.1 Adapter rule

Registration in an owning subsystem never implies Remote exposure.

Every exposure adapter owns:

- selection of source declarations;
- stable external identity mapping;
- Schema and codec selection;
- Data capabilities or Action surface;
- authorization;
- rate, batching, queue, and history policy;
- source error mapping;
- source-to-Remote acquisition semantics.

The adapter references canonical source APIs and storage. It does not recreate
them.

### 27.2 Parameters

A parameter adapter may contribute:

- Query through `solar::parameters::get<P>()`;
- Update through `solar::parameters::set<P>()` with external origin;
- Watch through committed parameter revisions;
- bounded descriptor and persistence-state queries;
- separately authorized save or reset Actions.

Exposure requires parameter external eligibility, explicit adapter selection,
stable external identity, bounded codec, and write authorization.

Remote never bypasses validation, read-only policy, transactions, persistence,
or change hooks.

### 27.3 Metrics

A metric adapter may contribute:

- Query for selected scalar views;
- Poll OutStream for selected readings or groups;
- descriptor Data;
- separately authorized reset Actions.

Metrics retain instrument, reducer, epoch, and synchronization ownership.
Remote rate policy determines when it acquires copies, not how metrics update.

### 27.4 Observability events

An event adapter may contribute:

- live Watch or OutStream publication;
- bounded retained-history page Actions;
- event Schema and descriptor Data;
- filters permitted by the exposure policy.

Event capture and history remain canonical in Events. Remote sessions own their
own cursors and losses.

### 27.5 Logging

A log adapter may contribute:

- live encoded or rendered OutStream;
- per-session source, domain, and level filters;
- dictionary negotiation Data and Actions;
- bounded history page Actions when Logging history exists.

Logging owns canonical records, rendering capability, source identity, and
history. Remote owns subscriptions, dictionary compatibility, session queues,
and slow-client behavior.

Firmware does not format an interactive CLI around the log stream.

### 27.6 Bus

A bus adapter may:

- translate selected bus messages into Watch or OutStream values;
- translate an authorized Action, Update, or InStream consumer into one selected
  bus emission;
- expose focused delivery counters as Data.

The bridge owns external schema, authorization, rate, and stable identity. It
never serializes a local message ID or C++ payload accidentally.

### 27.7 Lifecycle

A lifecycle adapter may expose:

- focused system state Data;
- component state Data;
- bounded boot-report page Actions;
- separately authorized stop or future reboot Actions.

Read access and lifecycle control use different grants. Remote never exports raw
internal record layout.

### 27.8 Graph and descriptors

A graph adapter may expose bounded pages of:

- components;
- dependencies;
- owners and origins;
- effective facilities and services;
- selected leaf catalogs.

These are immutable descriptor projections, not a runtime object graph.

### 27.9 Kernel and Execution

Focused adapters may expose selected:

- service and thread diagnostics;
- executor records;
- execution registration records;
- queue-target availability;
- task timing and failure facts.

Unavailability remains explicit. Remote does not claim facts Zephyr cannot
provide and does not automatically make tasks remotely invocable.

### 27.10 Development exposure packs

Solar may provide explicit convenience packs for prototyping:

```cpp
solar::remote::ExposeDevelopment<
    solar::remote::Parameters<DriveKp, DriveKi>,
    solar::remote::Metrics<ControlMetrics>,
    solar::remote::Logs<>,
    solar::remote::Lifecycle<solar::remote::ReadOnly>>
```

A convenience pack expands into ordinary explicit adapters. It does not select
every registered declaration, exceed external eligibility, or grant control by
default.

## 28. Manifest And Host Tooling

### 28.1 Authoritative source

The bound effective C++ catalogs and explicit stable IDs are authoritative for
the firmware protocol surface.

There is no separately authored YAML protocol registry that can drift from the
compiled system.

### 28.2 Build artifact

The build emits normalized immutable Remote descriptors into a dedicated ELF
manifest section or equivalent deterministic build artifact.

A post-link tool produces at least:

- a compact canonical host manifest, initially CBOR;
- a human-reviewable JSON form;
- generated checked constants and client schema bindings where requested;
- the SHA-256 digest used by the handshake.

The artifact describes the exact bound firmware, including generated Data
capability endpoints and standard subsystem adapters.

### 28.3 Manifest content

The manifest contains bounded metadata for:

- protocol version and capabilities;
- firmware build identity;
- Schema types and fields;
- Data and capabilities;
- Actions, Request, Response, Error, timeout, and access;
- Topics and Streams;
- links and negotiated link limits where useful;
- parameters, metrics, events, logs, lifecycle, graph, and execution exposures;
- units, enums, descriptions, defaults, ranges, and codec policy;
- stable IDs and schema versions.

### 28.4 Host-generated CLI

Host tooling renders commands, completion, validation, tables, JSON, files, and
stream visualization from the manifest.

Firmware has no line parser, command tokenizer, terminal history, ANSI output,
or text-oriented command registry.

### 28.5 Host SDKs

The manifest may generate typed Python, Rust, TypeScript, or C++ host bindings.
Generated host code is a derived artifact and does not replace stable protocol
identity.

## 29. Runtime Introspection

### 29.1 Optional capability

Runtime introspection is a Kconfig-selected protocol capability. It is valuable
for development and schema-mismatch recovery but is not required in every
production build.

### 29.2 Bounded paging

Introspection uses typed bounded page Actions for:

- protocol summary;
- Schema types and fields;
- Data and capabilities;
- Actions;
- Topics and Streams;
- selected system exposure descriptors.

There is no response containing the entire system manifest unless it fits one
declared bound.

### 29.3 Same descriptors

Runtime introspection and generated host manifests consume the same normalized
descriptors. Remote does not maintain a second mutable descriptor registry.

### 29.4 No universal runtime snapshot

Introspection describes declarations and focused records. It does not assemble
all component state, parameter values, metrics, logs, events, queues, and
sessions into one universal snapshot.

## 30. Lifecycle

### 30.1 Dependencies

The Remote service depends on:

- every effective link transport owner needed to open a link;
- selected engine execution target when not dedicated;
- explicit adapters whose runtime acquisition requires a component to be
  initialized before Remote admission;
- required protocol facilities such as time or authentication providers.

An exposure reference alone does not necessarily make the Remote service start
after every source component. Capability availability may instead report
`NotReady` until the source reaches its valid lifecycle state.

### 30.2 Initialization

Remote initialization:

1. validates effective catalogs and generated local indices;
2. initializes static event, frame, request, response, and session storage;
3. initializes ingress state for effective Topics and Streams;
4. initializes focused records;
5. prepares link adapters without accepting external application traffic;
6. initializes dedicated service or engine registration state.

### 30.3 Start

Remote start:

1. opens effective links and arms RX;
2. starts the dedicated service or activates the pump registration;
3. permits handshake and core protocol traffic;
4. opens application Action, query, update, subscription, and inbound admission
   when the system enters its valid Running window.

A thread merely existing does not imply links or sessions are ready.

### 30.4 Early records

Logs and events captured before Remote starts remain available only through the
owning subsystem's configured retained history. Remote does not create an
implicit duplicate boot history.

### 30.5 Stop admission order

Shutdown:

1. closes new application requests, subscriptions, updates, and inbound data;
2. stops poll acquisition and publication admission;
3. requests cancellation for active Actions and consumers;
4. emits a bounded session-closing notice when capacity and link state permit;
5. drains or cancels accepted control responses by declared policy;
6. closes sessions and invalidates responder generations;
7. closes links and prevents further callbacks;
8. stops or synchronizes engine execution;
9. releases Remote-owned runtime storage;
10. deinitializes before transport dependencies.

### 30.6 Stop timeout

A stop timeout is reported through lifecycle and Remote records. Forced
containment follows the service and executor rules from Phases 3 and 9.

Remote does not keep arbitrary application dependencies alive merely to finish
telemetry.

### 30.7 Repeated boot

The initial lifecycle policy rejects repeated boot. Remote therefore does not
promise complete in-process session, sequence, lease, and transport reset for a
controlled reboot until that broader policy is designed.

## 31. Synchronization And Thread Context

### 31.1 Thread-context protocol work

Framing, CRC verification, CBOR, packed encoding, Action dispatch, manifest
queries, subscription mutation, and session queue scheduling are thread-context
operations.

### 31.2 ISR-safe frontends

Only explicitly documented operations are ISR-safe:

- selected link event admission;
- `write_from_isr` and `publish_from_isr` for compatible storage;
- selected loan commit operations;
- focused atomic interest checks;
- selected compact record increments.

### 31.3 Source mutexes

Source mutexes are acquired only by source-owned readers, writers, Actions, or
consumers on valid thread execution targets.

Remote releases all internal locks before invoking them.

### 31.4 Spinlock copies

Spinlock-backed acquisition is appropriate only for short fixed-size copies
whose worst-case interrupt-disabled duration is acceptable and documented.

Remote does not automatically choose spinlocks for trivially copyable values.

### 31.5 Atomic acquisition

Atomic readers are treated as lock-free only when the selected type and target
provide the required lock-free guarantee. A type merely being trivially
copyable is insufficient.

### 31.6 Ingress concurrency

Stream and Topic declarations state whether ingress is single-producer or
multi-producer.

SPSC storage may be used only for one producer context and one Remote consumer.
MPSC or externally serialized storage is required otherwise.

### 31.7 Memory ordering

Lease commit and event wakeup establish visibility of completed payload writes
before the Remote service consumes them. SMP and cache-maintenance requirements
are part of the selected queue, DMA, or link policy.

## 32. Focused Records And Queries

### 32.1 Immutable descriptors

Compile-time queries expose immutable bounded views of:

- Schemas;
- Data and capabilities;
- Actions;
- Topics and Streams;
- links;
- adapters and provenance;
- protocol and policy configuration.

### 32.2 Link records

Each link record may contain:

- state and connection count;
- RX and TX bytes and frames;
- framing, CRC, malformed, and oversize failures;
- transport busy and fault counts;
- current and maximum sessions;
- last fault and transition time;
- RX and TX high-water marks.

### 32.3 Session records

Each session record may contain:

- link and generation;
- handshake and authentication state;
- grants;
- active request and subscription counts;
- per-lane occupancy and high-water marks;
- dropped, skipped, and disconnected counts;
- last frame and keepalive times;
- schema and capability negotiation result.

Principal identity is exposed only through an appropriately protected focused
query.

### 32.4 Action and request records

Focused records may contain:

- calls accepted, rejected, completed, failed, timed out, and cancelled;
- duplicate replays and expired duplicates;
- execution-target rejections;
- active and maximum request slots;
- latency summaries where enabled;
- last typed failure mapping.

They do not retain arbitrary Request or Response payloads by default.

### 32.5 Acquisition records

Each Query, Watch, or Stream may expose:

- acquisition attempts and successes;
- busy, unavailable, and reader failures;
- push admissions, replacements, coalescing, and drops;
- poll releases, skipped in-flight releases, and effective rate;
- loan pool use, high-water mark, and generation failures;
- latest accepted source sequence and timestamp;
- interested session count.

### 32.6 Synchronization

Mutable records are copied coherently under focused synchronization. Query APIs
do not expose mutable references after a lock is released.

Record synchronization is never held while calling source code or a link.

## 33. Configuration

### 33.1 Kconfig ownership

Kconfig owns build capabilities and hard ceilings including:

- Remote inclusion;
- dedicated-service versus workqueue-engine capability;
- default dedicated service stack and priority;
- default Action and capability execution on the system workqueue;
- maximum links and sessions;
- maximum frame and logical message size;
- maximum fragments and reassembly slots;
- event queue capacity;
- frame and TX lease pool capacities;
- request slots, response slots, response cache, and duplicate window;
- per-session lane ceilings;
- inbound decode slots and credits;
- default timeout and keepalive ceilings;
- zcbor, COBS, CRC32C, and packed codec inclusion;
- runtime introspection inclusion;
- focused record detail;
- authentication and secure-link integration capabilities;
- ISR publication capability;
- maximum static payload alignment and loan pool size.

Solar has no fallback `config.hpp` for these symbols.

### 33.2 Typed configuration ownership

C++ configuration owns:

- actual link declarations;
- Data, Action, Topic, Stream, and Schema membership;
- explicit exposure adapters;
- actual execution target types;
- access requirements;
- endpoint rates, batches, queues, and overflow;
- Action and acquisition timeouts within ceilings;
- link trust and authentication policy;
- source reader, writer, and consumer operations;
- lifecycle dependencies;
- stop and slow-client policy.

### 33.3 Precedence

For overridable policy:

```text
endpoint or capability
    > adapter or Data declaration
    > Remote typed configuration
    > Kconfig default
```

Typed configuration cannot enable compiled-out capability or exceed a hard
Kconfig ceiling.

### 33.4 Compile diagnostics

Invalid combinations fail near the declaration and name the endpoint, policy,
required capability, and limiting ceiling.

## 34. Resource Accounting

### 34.1 No dynamic allocation

The initial core Remote path requires no dynamic allocation after boot.

### 34.2 Per-facility cost

The facility owns immutable descriptor and dispatch tables plus frontend state
only for effective declarations.

### 34.3 Per-service cost

Dedicated service cost includes:

- one thread control block and stack;
- one bounded event queue;
- shared frame, request, response, decode, and reassembly pools;
- timeout and scheduler state;
- focused service records.

Workqueue engine mode omits the dedicated thread and stack but retains its
bounded pump registration and protocol storage.

### 34.4 Per-link cost

Each link adds:

- link adapter and transport state;
- RX parser and buffer state;
- TX in-flight state;
- configured session slots;
- focused link records.

The schema and dispatch catalogs are not duplicated per link.

### 34.5 Per-session cost

Each session adds bounded:

- handshake and permission state;
- subscription representation;
- request and duplicate state;
- lane metadata and configured storage share;
- inbound credit and stream cursors;
- focused records.

### 34.6 Per-endpoint cost

An endpoint pays only for its selected acquisition, queue, batching, cache,
history cursor, or codec state. Query-only Data has no stream ingress queue.

### 34.7 Static report

The build should report Remote static storage grouped by:

- service core;
- links;
- sessions;
- requests and responses;
- frame and fragment pools;
- individual Data, Topic, and Stream ingress;
- optional introspection and records.

## 35. Include Direction

Ordinary component headers include Solar Remote declarations and the direct
application types they call. They never include the composition root.

```text
imu.hpp
  -> solar/remote/data.hpp
  -> solar/remote/action.hpp
  -> application value schemas

remote_config.hpp
  -> component and adapter declarations
  -> board link declarations

system.hpp
  -> application components
  -> remote_config.hpp
  -> solar/system.hpp
```

The composition root binds the system after every component and Remote
configuration declaration is complete.

Generated manifests consume the bound effective catalog after binding. They do
not need to be included by ordinary component headers.

## 36. Complete Application Example

### 36.1 Value schema

```cpp
struct ImuSample
{
    float ax;
    float ay;
    float az;
    float gx;
    float gy;
    float gz;
};

template<>
struct solar::remote::Schema<ImuSample>
{
    static constexpr solar::remote::Descriptor descriptor{
        .id = solar::remote::TypeId{0x3001},
        .name = "robot.ImuSample",
    };

    using Fields = solar::remote::Fields<
        solar::remote::Field<1, &ImuSample::ax>,
        solar::remote::Field<2, &ImuSample::ay>,
        solar::remote::Field<3, &ImuSample::az>,
        solar::remote::Field<4, &ImuSample::gx>,
        solar::remote::Field<5, &ImuSample::gy>,
        solar::remote::Field<6, &ImuSample::gz>>;
};
```

### 36.2 Queryable and streamable Data

```cpp
struct ImuTelemetry
{
    static constexpr solar::remote::Descriptor descriptor{
        .id = solar::remote::DataId{0x2001},
        .name = "imu.telemetry",
    };

    using Value = ImuSample;

    using Capabilities = solar::remote::Capabilities<
        solar::remote::Query<&Imu::read_latest>,
        solar::remote::OutStream<
            solar::remote::Push,
            solar::remote::Packed,
            solar::remote::MaxRate<500_Hz>,
            solar::remote::Batch<16>,
            solar::remote::Queue<32>,
            solar::remote::SingleProducer,
            solar::remote::DropOldest>>;
};
```

### 36.3 Action

```cpp
struct CalibrateImu
{
    static constexpr solar::remote::Descriptor descriptor{
        .id = solar::remote::ActionId{0x0102},
        .name = "imu.calibrate",
    };

    using Request = CalibrationOptions;
    using Response = CalibrationReport;
    using Error = CalibrationError;
    using Access = solar::remote::Requires<
        solar::remote::permission::Control>;
    using Execution = solar::remote::On<CalibrationQueue>;

    static solar::Result<Response, Error>
    execute(const Request &request);
};
```

### 36.4 Component contribution

```cpp
struct Imu
{
    static solar::Result<ImuSample, ImuReadError> read_latest();

    using RemoteData = solar::remote::ContributeData<ImuTelemetry>;
    using RemoteActions = solar::remote::ContributeActions<CalibrateImu>;
};
```

### 36.5 Producer path

```cpp
void Imu::on_sample(const ImuSample &sample)
{
    store_latest(sample);
    (void)solar::remote::write<ImuTelemetry>(sample);
}
```

The source stores canonical state through its own synchronization and submits
the already coherent local sample to Remote. Remote does not reread live state
for this stream occurrence.

### 36.6 Links and exposures

```cpp
using DebugUsb = solar::remote::Link<
    "debug-usb",
    board::UsbCdc0,
    solar::remote::Sessions<1>,
    solar::remote::Trust<DevelopmentRole>>;

using NetworkTcp = solar::remote::Link<
    "network-tcp",
    board::TcpServer<9000>,
    solar::remote::Sessions<4>,
    solar::remote::Authenticate<DeviceCredentials>>;

struct RobotRemote
{
    using RemoteLinks = solar::remote::ContributeLinks<
        DebugUsb,
        NetworkTcp>;

    using Configuration = solar::remote::Configuration<
        solar::remote::Engine<solar::remote::DedicatedService>,
        solar::remote::DefaultActionExecution<
            solar::execution::SystemWorkQueue>,
        solar::remote::Expose<
            solar::remote::Parameters<DriveKp, DriveKi>,
            solar::remote::Metrics<ControlMetrics>,
            solar::remote::Logs<>,
            solar::remote::Lifecycle<solar::remote::ReadOnly>>>;
};
```

Final helper names and template parameter ordering may be refined during
implementation without changing ownership or semantics.

## 37. In-Memory Test Boundary

### 37.1 No physical transport required

The protocol engine exposes a deterministic test link that can:

- create and close sessions;
- ingest arbitrary byte fragments;
- advance virtual time;
- execute one bounded pump budget;
- extract transmitted byte fragments;
- inject transport busy, short write, disconnect, and fault outcomes;
- inspect focused records.

The test link uses the same COBS, CRC, envelope, codec, dispatch, session,
backpressure, and acquisition machinery as physical links.

### 37.2 Deterministic execution

Tests may configure inline Action and acquisition execution explicitly. This
does not change the production default.

Asynchronous responder, workqueue, cancellation, and timeout tests use controlled
test executors and virtual time rather than sleeping on wall-clock time.

### 37.3 Host interoperability

Protocol golden tests run the generated host codec against firmware vectors for:

- every frame kind;
- CBOR schemas;
- packed batches;
- fragmentation and reassembly;
- malformed and truncated input;
- version and capability negotiation;
- duplicate requests and response acknowledgement.

## 38. Compile-Time Validation

Solar validates at binding after all contributions and adapters are known.

Required diagnostics include:

- duplicate stable ID in one identity domain;
- missing explicit external identity;
- name collision where names must be unique;
- missing or invalid Schema;
- unbounded field or payload;
- duplicate field ID;
- unsupported CBOR or packed field type;
- invalid packed layout or batch bound;
- Query reader returning borrowed mutable storage;
- Query reader or writer return type mismatch;
- Stream without one acquisition mode;
- Push Stream without valid ingress policy;
- ISR frontend selected with non-ISR-safe storage or value;
- SPSC policy with an incompatible declared producer model;
- Poll Stream without a valid execution target;
- Poll rate above Kconfig or source ceiling;
- Loaned pool exceeding static size or alignment ceiling;
- Action Request, Response, or Error without Schema;
- Action handler signature or return mismatch;
- asynchronous Action without a bounded response slot;
- unknown execution target or missing dependency;
- mailbox target without a compatible typed mailbox;
- Data capability collision;
- explicit adapter referencing an unregistered source;
- parameter exposure without external eligibility;
- exposure requiring a disabled subsystem or codec;
- privileged operation without explicit Access;
- inbound Stream without bounded credits and consumer admission;
- fragmented endpoint exceeding reassembly ceilings;
- link requiring unsupported Zephyr transport capability;
- typed configuration exceeding a Kconfig hard ceiling;
- Remote enabled with no effective link when a production service is required.

Diagnostics should name the authored owner, declaration, generated capability,
required feature, and relevant policy source.

## 39. Runtime Errors

### 39.1 Error layers

Remote preserves distinct runtime error layers:

- link errors;
- framing and integrity errors;
- protocol and session errors;
- authorization errors;
- admission and backpressure errors;
- decode and schema errors;
- execution submission errors;
- acquisition errors;
- Action domain errors;
- cancellation and timeout;
- lifecycle and shutdown errors.

### 39.2 Stable protocol errors

Initial stable protocol errors include concepts equivalent to:

```text
UnsupportedVersion
UnsupportedCapability
SchemaMismatch
MalformedFrame
IntegrityFailure
OversizedFrame
OversizedMessage
FragmentRejected
UnknownTarget
UnsupportedOperation
DecodeFailure
Unauthorized
NotReady
Busy
NoCapacity
RateRejected
CreditViolation
RequestExpired
DuplicateResponseExpired
Cancelled
TimedOut
SessionClosing
InternalFailure
```

Exact numeric values are frozen in the protocol manifest and golden tests.

### 39.3 Error mapping

Typed source and Action errors map explicitly into external schemas. Mapping may
be intentionally lossy for security or compatibility, but the loss is declared.

An arbitrary errno, C++ enum representation, or `Status` integer is not emitted
without a stable protocol mapping.

### 39.4 Malformed-client policy

Malformed input increments focused records and receives a bounded error only
when doing so is safe and cannot amplify traffic.

Repeated malformed, unauthorized, credit-violating, or resource-exhausting
behavior may disconnect the session according to link security policy.

## 40. Verification Requirements

Implementation must test:

- compilation on supported Zephyr/SDK and native simulation toolchains;
- no dynamic allocation in core paths;
- facility and service omission when unused;
- one shared catalog across several links;
- UART-like arbitrary RX fragmentation;
- USB-like packet boundaries unrelated to frames;
- TCP short reads and writes;
- COBS resynchronization after inserted, removed, and corrupted bytes;
- CRC32C golden vectors and rejection;
- protocol major, minor, capability, and schema negotiation;
- deterministic CBOR and packed encoding vectors;
- unknown optional and missing required fields;
- explicit stable IDs independent from type names and catalog order;
- Action success, domain failure, and protocol failure;
- out-of-order response completion;
- system-workqueue, named-workqueue, inline, mailbox, and responder execution;
- request capacity reservation before dispatch;
- duplicate pending, duplicate completed, expired duplicate, and response ack;
- pending and running cancellation;
- timeout and late responder rejection;
- reconnect generation invalidation;
- permission grants and denial before application execution;
- Query by-value and destination-reader acquisition;
- mutex, atomic, spinlock, and source-specific reader integration;
- proof that source locks are released before encoding;
- rejection of borrowed reader returns;
- Push Stream from ordinary thread context;
- Push Stream from ISR where permitted;
- producer replacement, coalescing, queue overflow, and no-subscriber outcomes;
- Poll activation, effective-rate aggregation, downsampling, skipped overlap, and
  final unsubscribe containment;
- loan acquire, commit, abandon, overflow, generation failure, DMA-style
  completion, and prompt release under a slow session;
- Query and Push Stream on one Data identity;
- explicit LatestPublished before and after first publication;
- inbound credit, ownership, consumer backpressure, ordering, and violation;
- paired duplex flow independence;
- per-session subscription isolation;
- per-lane priority and response reservation;
- large fragmented bulk data unable to block control responses;
- slow-client drop, throttle, subscription-close, and disconnect policies;
- sequence, timestamp, batch, and loss metadata;
- parameter, metric, event, log, bus, lifecycle, graph, Kernel, and Execution
  adapters using canonical APIs;
- runtime introspection enabled and omitted builds;
- manifest digest agreement with generated host tooling;
- lifecycle start, partial link failure, shutdown, and stop timeout;
- compile-fail diagnostics for every invalid contract in Section 38;
- native and target static resource reports.

Malformed-frame and decoder fuzzing must be included. Fuzz inputs may never
escape declared buffers, invoke an unauthorized endpoint, or create unbounded
work.

## 41. Migration From Current Solar

### 41.1 Replace rather than preserve

The current Remote implementation is a prototype reference and is not the base
architecture for this design.

Its combined synchronous transport loop, `ContextT::SystemType` access,
name-derived IDs, hand-written general codec, fixed stack frame buffers, and
service-owned catalog construction are removed.

### 41.2 Planned replacement

Migration should:

1. replace `RemoteTransport` synchronous requirements with asynchronous links;
2. replace the monolithic `services::Remote<Transport>` with the facility,
   effective Remote service, and link catalogs;
3. replace `RemoteMethods` with `RemoteActions`;
4. add `RemoteData` and `RemoteLinks` contribution aliases;
5. retain advanced `RemoteTopics` and `RemoteStreams` under the new contracts;
6. remove `Observables` as a separate vague protocol concept;
7. replace FNV name-derived IDs with explicit stable typed IDs;
8. replace raw descriptor arrays assembled in the transport service with
   generic normalized catalogs;
9. adopt zcbor-backed deterministic CBOR and explicit packed Stream codecs;
10. replace hand-written COBS and CRC16 with validated Zephyr COBS and CRC32C
    integration;
11. introduce the deterministic in-memory engine before physical links;
12. add the dedicated service and bounded event path;
13. add Action execution and request ownership;
14. add Data Query and Push acquisition before Poll and Loaned acquisition;
15. add sessions, subscriptions, lanes, authorization, and adapters;
16. replace the generated core manifest with the effective post-binding build
    manifest;
17. migrate firmware endpoints without maintaining two public Remote systems.

### 41.3 No indefinite compatibility layer

Temporary aliases may help one migration slice compile, but the old method,
observable, context, synchronous transport, and name-hash APIs do not remain as
parallel supported architecture.

## 42. Initial Required Capability

The first complete Remote implementation must include:

- static facility catalogs and effective service derivation;
- one dedicated service engine and in-memory test engine;
- asynchronous link contract;
- UART/USB-appropriate byte-stream test adapter shape;
- COBS and CRC32C framing;
- version, capability, build, and schema handshake;
- deterministic CBOR Schema support;
- explicit stable IDs and generated manifest;
- one-session and multi-session storage model;
- Data with Query, Update, Watch, OutStream, and InStream contracts;
- Push and Poll acquisition;
- basic Loaned acquisition and generation-safe ownership;
- Actions with system-workqueue default and explicit Inline/On target;
- typed mailbox contract integration where a mailbox exists;
- asynchronous responder;
- correlation, timeout, cancellation, duplicate suppression, and response ack;
- Observe, Configure, Control, and Admin grants;
- Topics and Streams with subscriptions;
- per-session lanes and slow-client isolation;
- sequence, timestamp, batch, drop, and fragmentation support;
- explicit parameter, metrics, event, log, bus, lifecycle, graph, Kernel, and
  Execution adapter contracts;
- focused records and bounded runtime introspection option;
- static resource reporting and compile-fail validation.

Physical TCP, TLS, complete host SDK generation, and every convenience adapter
may land in later implementation slices, but their architectural contracts are
fixed here.

## 43. Deferred Extensions

These capabilities are compatible with the design but are deferred:

- protocol transport over datagrams, CAN, Bluetooth, or shared memory;
- negotiated omission of COBS on packet-preserving links;
- compression;
- end-to-end zero-copy across selected single-session links;
- scatter/gather TX leases;
- richer RTIO-native links where platform support is mature;
- dynamic session role changes after authentication;
- resumable sessions across reconnect;
- cross-session exactly-once commands;
- durable command journals;
- remote file-system semantics;
- generated graphical dashboards;
- standardized firmware update protocol;
- schema migration translators on the device;
- host-to-device standalone Topic publication;
- richer per-principal quotas;
- certificate and credential provisioning Actions;
- runtime-selected alternate payload codecs;
- C++ reflection-based automatic field declaration when a supported future
  language baseline makes it appropriate.

Deferred does not permit an initial implementation to violate stable identity,
ownership, boundedness, authorization, or slow-client isolation.

## 44. Rejected Alternatives

### 44.1 One Remote service per transport

Rejected because it duplicates protocol state, catalogs, scheduling policy, and
fan-out while making cross-link behavior inconsistent.

One Remote service owns the protocol plane; several links adapt transports.

### 44.2 Facility without a service

Rejected as the default because protocol RX, timeouts, output scheduling, and
transport completion deserve explicit sustained execution and lifecycle.

The workqueue engine remains an explicit alternative.

### 44.3 Hidden private workqueue

Rejected because every thread and stack must remain visible. The service thread
is explicit, and application execution defaults to Zephyr's existing system
workqueue.

### 44.4 Run Actions inline by default

Rejected because an ordinary application lock or slow handler could block RX,
acknowledgements, timeouts, and every session.

Inline remains an explicit optimization.

### 44.5 One synchronous transport concept

Rejected because polling UART, interrupt UART, DMA UART, USB CDC, sockets, and
tests do not share truthful synchronous completion semantics.

### 44.6 Parse in transport callbacks

Rejected because callback and ISR work must remain bounded and cannot safely
invoke arbitrary protocol or application logic.

### 44.7 Pass raw addresses to Query

Rejected because an address provides no synchronization, coherence, lifetime,
execution, or failure contract.

### 44.8 Return references from readers

Rejected because source synchronization normally ends before Remote encoding
and transmission complete.

### 44.9 Hold a source lock during encoding

Rejected because CBOR, fragmentation, session fan-out, queueing, and transport
latency must never extend an application critical section.

### 44.10 Remote polls every stream

Rejected because many values exist only at sensor, DMA, control-loop, event, or
producer-defined completion times.

Push, Poll, and Loaned acquisition are explicit.

### 44.11 Every stream is producer pushed

Rejected because passive state, metrics, and low-rate diagnostics benefit from
subscription-activated coherent polling.

### 44.12 End-to-end borrowed zero-copy

Rejected initially because one slow session could retain source or DMA storage
and block the producer. Loaned staging has bounded release semantics.

### 44.13 Query always returns the latest stream value

Rejected because stream ingress is not automatically canonical source state.
`LatestPublished` is explicit.

### 44.14 Treat Data and communication as the same concept

Rejected because one semantic value may be queryable, watchable, streamable,
updatable, or ingestible through independent policies.

### 44.15 Keep the public term Method

Rejected because Method describes C++ implementation rather than host-visible
intent. The public declaration is Action; the wire operation is a call.

### 44.16 Force all incoming data through Actions

Rejected because sustained setpoints, trajectories, maps, and chunks require
credit-based Stream admission rather than one RPC response per sample.

### 44.17 Generic host Topic publication

Rejected initially because discrete commands use Actions or Update and sustained
input uses InStream with clearer authorization and backpressure.

### 44.18 One FIFO for all output

Rejected because a bulk frame could block cancellation, handshake, or accepted
Action responses.

### 44.19 Block producers for reliable delivery

Rejected because client availability cannot become application timing or
correctness dependency.

### 44.20 Call every TCP frame reliable telemetry

Rejected because transport delivery says nothing about samples dropped before
Remote admission, rate selection, or per-session fan-out.

### 44.21 One global subscription state

Rejected because each connection has independent authorization, rates, queues,
and lifecycle.

### 44.22 Preserve subscriptions across reconnect

Rejected initially because reconnect creates a new authenticated session and
must not inherit stale authority, credits, or request state.

### 44.23 Retry with a new request ID

Rejected for side-effecting operations because it can execute the Action twice.
Retries reuse the same session-local ID.

### 44.24 Claim exactly-once across reconnect

Rejected because that requires durable identity and command journaling outside
the initial bounded in-memory session model.

### 44.25 Mix Action domain errors with protocol errors

Rejected because malformed input and authorization failure differ fundamentally
from an accepted calibration or control operation failing.

### 44.26 Use JSON as the firmware protocol

Rejected as the default because deterministic CBOR provides compact typed
structure and mature host interoperability without firmware text parsing.

JSON remains a host rendering and manifest-review format.

### 44.27 Use packed encoding for everything

Rejected because ordinary schemas need optional fields and compatible evolution.
Packed is explicit for high-rate bounded Streams.

### 44.28 Serialize C++ object representation

Rejected because padding, endianness, ABI, compiler, and lifetime are not stable
wire contracts.

### 44.29 Name-derived external IDs

Rejected because renaming, collisions, and implementation details must not
silently break protocol identity.

### 44.30 Make Remote canonical storage

Rejected because each owning subsystem already defines synchronization,
retention, mutation, and lifecycle truth.

### 44.31 Automatically expose every registered declaration

Rejected because registration is not external identity, codec availability,
authorization, rate policy, or resource consent.

### 44.32 Firmware text CLI

Rejected because host tooling can generate richer command, validation, stream,
and visualization experiences from the typed manifest without burdening
firmware with parsing and formatting.

### 44.33 Universal Remote snapshot

Rejected because system facts have different owners, synchronization, costs,
availability, and update rates.

### 44.34 Separate hand-authored protocol registry

Rejected because it can drift from the bound system catalogs. The host manifest
is generated from the effective compiled surface.

## 45. Accepted Decisions

1. Remote is Solar's typed external protocol plane.
2. Firmware has no native text CLI for Remote operations.
3. Remote has one static facility, one protocol service by default, and one or
   more asynchronous links.
4. The facility owns catalogs, schemas, dispatch, adapters, and global APIs.
5. The service owns sessions, protocol runtime, queues, buffers, and scheduling.
6. Links own or reference transport adaptation and in-flight I/O state.
7. Source subsystems retain canonical state and history.
8. Remote uses no system object, runtime context, or `ContextT::SystemType`.
9. The effective Remote facility and service are derived when enabled and used.
10. The Remote service is a visible lifecycle component with visible resources.
11. A dedicated nonessential service thread is the default engine execution.
12. A bounded coalesced workqueue pump is an explicit engine alternative.
13. Protocol-engine execution and application execution are independent.
14. Driver and socket callbacks only commit storage, enqueue compact events, and
    wake the engine.
15. Protocol parsing and application dispatch never run in ISR by default.
16. One bounded deterministic pump underlies dedicated, workqueue, and test
    execution.
17. Links normalize asynchronous events and ownership, not transport mechanics.
18. Remote does not require synchronous `read`, `write`, `available`, and
    `flush` operations.
19. Multiple links share one canonical protocol and schema surface.
20. Slow or disconnected links never block system producers.
21. Remote uses COBS framing across initial byte-stream links.
22. Remote uses CRC32C for accidental-corruption detection.
23. Framing, envelope, fragmentation, payload codec, and typed Schema remain
    separate protocol layers.
24. Protocol major, minor, capabilities, limits, build identity, and schema
    digest are negotiated during handshake.
25. Reconnect creates a fresh session.
26. Deterministic CBOR with integer field IDs is the ordinary payload codec.
27. Zephyr zcbor is the initial CBOR foundation.
28. Explicit packed encoding is available for high-rate Streams.
29. Packed encoding is never inferred from C++ object layout.
30. All payloads have bounded explicit Schemas.
31. Schema, Data, Action, Topic, and Stream use distinct typed external ID
    domains.
32. Data capabilities share one Data ID plus operation kind.
33. External IDs are explicit or manifest controlled, never unchecked name
    hashes or compiler type names.
34. Compatible schema evolution may retain identity; breaking meaning requires
    new identity.
35. `RemoteData`, `RemoteActions`, `RemoteTopics`, `RemoteStreams`, and
    `RemoteLinks` are reserved ergonomic aliases.
36. Action replaces Method as the public RPC declaration term.
37. Data separates semantic value identity from communication capability.
38. Initial Data capabilities are Query, Update, Watch, OutStream, and InStream.
39. Standalone Topics and Streams remain available for advanced protocol-native
    declarations.
40. Standalone initial Topics are device-to-host only.
41. Discrete host intent uses Action or Update.
42. Sustained host data uses InStream.
43. Remote may encode only owned coherent values or generation-checked leases.
44. Remote never serializes a borrowed reference to mutable state.
45. Acquisition and transmission are separate stages.
46. The source owns its synchronization strategy.
47. Remote holds no internal lock while invoking source or application code.
48. Source locks end before encoding, fan-out, queueing, or transport.
49. Query readers return owned values or write into caller-owned storage.
50. Unleased references, pointers, spans, and mutable views are rejected.
51. Query execution follows endpoint, Data, typed configuration, and Kconfig
    precedence.
52. The initial default for Action and capability execution is Zephyr's system
    workqueue.
53. Inline application execution is explicit.
54. Named workqueue, typed mailbox, and asynchronous responder execution are
    supported.
55. Remote owns request payload, response state, timeout, authorization,
    cancellation, and correlation around deferred execution.
56. Remote never asynchronously aborts arbitrary C++ code.
57. Outbound Streams explicitly select Push, Poll, or Loaned acquisition.
58. Push is the normal source-driven stream path.
59. Push admission performs no encoding, session fan-out, or transport I/O.
60. Poll acquisition is subscription activated and uses Phase 9 execution.
61. One Poll reader invocation serves eligible sessions at the highest effective
    rate, with independent downsampling.
62. Poll acquisition permits at most one read in flight and skips overlap.
63. Loaned acquisition is bounded, move-only, and generation checked.
64. Slow sessions cannot retain producer leases indefinitely.
65. End-to-end zero-copy is not an initial guarantee.
66. Push storage explicitly selects Latest, Queue, Batch, LoanedPool, and
    producer concurrency policy as needed.
67. Ordinary write is bounded, non-waiting, and thread-safe for its policy.
68. ISR write is a separate constrained API.
69. Publication outcomes distinguish accepted, replaced, coalesced, dropped,
    and no-subscriber behavior.
70. Interest checks are advisory only.
71. `LatestPublished` query behavior is explicit and never inferred.
72. Inbound values occupy Remote-owned slots until consumer completion or
    ownership transfer.
73. Inbound Streams use bounded credits.
74. Duplex Data is paired one-way flow with independent sequence and
    backpressure.
75. Actions use typed Request, Response, and Error schemas.
76. Remote decodes one owned Request object rather than arbitrary parameter
    lists.
77. Protocol errors remain separate from Action domain errors.
78. Capacity is reserved before an Action or capability operation runs.
79. Request IDs are monotonically advancing and session local.
80. Duplicate suppression provides at-most-once accepted execution within one
    live session.
81. Duplicate response eviction never permits silent re-execution.
82. Response acknowledgement may release cached payload while retaining
    duplicate knowledge.
83. Side-effecting retries reuse the same request ID.
84. Cancellation is cooperative and idempotent.
85. Timeout closes response admission but does not claim arbitrary code stopped.
86. Session generations invalidate late responders safely.
87. Initial grants are Observe, Configure, Control, and Admin.
88. Grants are explicit independent capabilities rather than an implicit
    hierarchy.
89. Link trust and authentication policy are explicit.
90. Permissions are not encryption or authentication by themselves.
91. Ordinary handlers receive no ambient mutable session object.
92. Subscriptions, rates, filters, credits, and queues are per session.
93. Poll acquisition activates on first subscription and stops after the last.
94. Protocol, response, important, telemetry, and bulk output have protected
    bounded lanes.
95. Accepted responses reserve capacity before dispatch.
96. Large Stream fragments cannot block protocol control or accepted responses.
97. Every lossy path reports sequence and loss information where available.
98. Transport reliability does not erase source or Remote drops.
99. Large maps and grids should use bounded revisioned chunks or tiles.
100. Every subsystem exposure is explicit.
101. Exposure adapters own external schema, identity, authorization, rate, and
     source error mapping.
102. Parameters are accessed through canonical get, set, transaction, and
     persistence APIs.
103. Metrics remain passive and canonically owned by Metrics.
104. Event and log capture and history remain in their owning subsystems.
105. Bus bridges never serialize accidental local identities or payload layout.
106. Lifecycle read and control exposure use separate authorization.
107. Tasks are not automatically remotely invocable.
108. Development convenience packs expand to ordinary explicit adapters.
109. The host manifest is generated from the effective bound catalogs.
110. The manifest is emitted in canonical machine-readable and reviewable forms.
111. Firmware and host compare a SHA-256 schema digest.
112. Runtime introspection is optional, bounded, paged, and derived from the same
     descriptors.
113. Host tooling owns CLI rendering, completion, validation, and visualization.
114. There is no universal Remote snapshot.
115. Core Remote runtime uses no dynamic allocation after boot.
116. Unused link, session, endpoint, acquisition, and introspection storage is
     absent where compile-time selection permits.
117. An in-memory link tests the complete protocol without physical hardware.
118. Golden vectors, compile-fail tests, malformed-input fuzzing, concurrency,
     slow-client, and lifecycle tests are required.
119. The current monolithic Remote implementation is replaced rather than
     preserved as a parallel architecture.
120. Supported Zephyr upgrades must revalidate COBS, zcbor, UART, socket-service,
     queue, workqueue, atomic, DMA, and memory-ordering assumptions.

## 46. Primary References

The platform integration decisions were checked against local Zephyr 4.4.0
source and official documentation:

- [Zephyr UART](https://docs.zephyrproject.org/latest/hardware/peripherals/uart.html)
- [Zephyr Async UART API](https://docs.zephyrproject.org/latest/doxygen/html/group__uart__async.html)
- [Zephyr USB CDC ACM](https://docs.zephyrproject.org/latest/services/connectivity/usb/device_next/cdc_acm.html)
- [Zephyr BSD Sockets](https://docs.zephyrproject.org/latest/connectivity/networking/api/sockets.html)
- [Zephyr Socket Services](https://docs.zephyrproject.org/latest/services/connectivity/networking/api/socket_service.html)
- [Zephyr Message Queues](https://docs.zephyrproject.org/latest/kernel/services/data_passing/message_queues.html)
- [Zephyr Ring Buffers](https://docs.zephyrproject.org/latest/kernel/data_structures/ring_buffers.html)
- [Zephyr SPSC Packet Buffers](https://docs.zephyrproject.org/latest/kernel/data_structures/spsc_pbuf.html)
- [Zephyr Workqueue Threads](https://docs.zephyrproject.org/latest/kernel/services/threads/workqueue.html)
- [Zephyr Mutexes](https://docs.zephyrproject.org/latest/kernel/services/synchronization/mutexes.html)
- [Zephyr Spinlocks](https://docs.zephyrproject.org/latest/doxygen/html/group__spinlock__apis.html)
- [Zephyr COBS API](https://docs.zephyrproject.org/latest/doxygen/html/group__cobs.html)
- [Zephyr CBOR](https://docs.zephyrproject.org/latest/services/serialization/cbor.html)
- [Zephyr CRC API](https://docs.zephyrproject.org/latest/doxygen/html/group__crc.html)
- [Zephyr RTIO](https://docs.zephyrproject.org/latest/services/rtio/index.html)
- [RFC 8949: Concise Binary Object Representation](https://datatracker.ietf.org/doc/html/rfc8949)
- local `zephyrproject/zephyr/include/zephyr/data/cobs.h`
- local `zephyrproject/zephyr/include/zephyr/sys/crc.h`
- local `zephyrproject/zephyr/include/zephyr/sys/ring_buffer.h`
- local `zephyrproject/zephyr/include/zephyr/sys/spsc_pbuf.h`
- local `zephyrproject/zephyr/include/zephyr/sys/mpsc_pbuf.h`
- local `zephyrproject/zephyr/include/zephyr/net/socket_service.h`
- local `zephyrproject/zephyr/include/zephyr/drivers/uart.h`

Implementation must revalidate API availability and semantics against each
supported Zephyr release, especially CDC ACM completion behavior, UART callback
exclusivity, short socket writes, COBS streaming behavior, zcbor configuration,
queue ISR rules, and SMP/cache visibility for DMA and SPSC storage.

## 47. Open Questions

There are no blocking architectural questions for Phase 11.

Implementation may refine without changing this contract:

- final helper-template parameter ordering;
- exact local dispatch-index widths;
- exact protocol version 1 envelope offsets and flag allocation;
- fixed initial frame and message ceiling values;
- exact CRC32C Zephyr helper selected;
- generated manifest file extensions;
- host SDK language order;
- exact compact Schema field declaration helpers;
- exact request duplicate-window width and response-cache eviction defaults;
- exact lane scheduler weights;
- exact default service stack and priority;
- exact poll-rate aggregation rounding;
- exact loan pool implementation chosen for SPSC, MPSC, DMA, and cached memory;
- additional focused record fields available on each board;
- compatibility aliases used only during migration.

These are implementation details around the accepted facility/service/link
split, async-first engine, Data and Action model, owned acquisition boundary,
stable protocol, bounded sessions, and explicit subsystem integration.
