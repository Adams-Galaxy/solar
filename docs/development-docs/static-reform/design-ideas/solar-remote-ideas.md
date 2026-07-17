Remote feels like the point where Solar stops being only an internal orchestration layer and becomes an observable system.

I would frame it as two layers:

* **Remote facility**: protocol primitives, registries, buffering, framing, subscriptions, request tracking, encoding, and transport-independent dispatch.
* **Remote service**: owns the transport lifecycle, receives bytes, decodes frames, runs asynchronous work, and drains outbound queues.

That split is important because things like logging from an ISR, publishing telemetry, or registering an RPC endpoint should not depend on UART, USB CDC, or TCP directly.

The facility is the protocol engine.

The service is the thing that moves bytes.

# 1. Core shape

Conceptually:

```cpp
using Remote = solar::facilities::Remote<
    solar::remote::Codec<solar::remote::BinaryCodec>,
    solar::remote::Registry<
        RpcEndpoints,
        Topics,
        Streams>,
    solar::remote::Buffers<
        RxBuffer,
        TxBuffer,
        IsrLogBuffer>>;
```

And then:

```cpp
using Services = solar::Services<
    solar::services::Remote<board::UsbCdc>>;
```

Or:

```cpp
using Services = solar::Services<
    solar::services::Remote<board::Uart<1>>,
    solar::services::Remote<board::TcpServer<9000>>>;
```

The same protocol may run over multiple transports.

That suggests a useful abstraction:

```cpp
template<typename Transport>
struct RemoteService;
```

where `Transport` provides something like:

```cpp
struct Transport {
    static Status init();
    static Result<std::size_t> read(std::span<std::byte>);
    static Result<std::size_t> write(std::span<const std::byte>);
    static bool connected();
};
```

The protocol itself should not care whether the bytes come from USB, UART, TCP, or something stranger later.

# 2. Do not make the wire protocol type-dependent

Internally, Solar should absolutely use types.

Externally, the wire should use stable numeric identities.

For example:

```cpp
struct GetParameter {
    static constexpr solar::remote::MethodId id = 0x0102;
    static constexpr std::string_view name = "parameter.get";

    using Request = ParameterGetRequest;
    using Response = ParameterGetResponse;
};
```

Inside firmware:

```cpp
Remote::registerMethod<GetParameter>();
```

On the wire:

```text
method_id = 0x0102
correlation_id = 42
payload = ...
```

Do not serialize C++ type names or hashes derived from compiler strings. Those are unstable across compilers, builds, optimization modes, and refactors.

The good pattern is:

> Type identity in C++, stable explicit IDs on the wire, human-readable names in metadata.

That mirrors the event bus design nicely.

# 3. Remote definitions should be typed descriptors

For RPC:

```cpp
namespace rpc {

struct GetStatus {
    static constexpr remote::MethodId id = 0x0001;
    static constexpr std::string_view name = "system.status.get";

    using Request = Empty;
    using Response = SystemStatus;
};

struct SetParameter {
    static constexpr remote::MethodId id = 0x0002;
    static constexpr std::string_view name = "parameter.set";

    using Request = ParameterSetRequest;
    using Response = ParameterSetResponse;
};

}
```

Handler:

```cpp
struct GetStatusHandler {
    static remote::Result<SystemStatus>
    handle(const rpc::GetStatus::Request&) {
        return SystemStatus{
            .state = solar::SystemState::Running,
            .uptime_ms = solar::time::uptimeMs(),
        };
    }
};
```

Registration:

```cpp
using RpcEndpoints = solar::remote::Rpc<
    solar::remote::Endpoint<
        rpc::GetStatus,
        GetStatusHandler>,

    solar::remote::Endpoint<
        rpc::SetParameter,
        SetParameterHandler,
        solar::remote::Execution<
            solar::remote::WorkQueue<ControlQueue>>>>;
```

The endpoint descriptor can carry execution policy, permissions, timeout, and serialization behavior.

# 4. Request and response should be protocol concepts, not transport concepts

A request frame should contain at least:

```cpp
struct RequestHeader {
    FrameType type;
    MethodId method;
    CorrelationId correlation;
    std::uint16_t payload_size;
};
```

A response:

```cpp
struct ResponseHeader {
    FrameType type;
    MethodId method;
    CorrelationId correlation;
    RemoteStatus status;
    std::uint16_t payload_size;
};
```

The correlation ID is what lets you support several in-flight requests at once.

For example:

```text
request 41: parameter.get
request 42: system.status.get
response 42: ...
response 41: ...
```

Responses do not need to arrive in request order.

That becomes especially important over TCP or when handlers are dispatched to different queues.

# 5. Support synchronous and asynchronous handlers

Some handlers are trivial:

```cpp
static Response handle(const Request&);
```

Others may need to suspend, wait for hardware, or run on a work queue.

You could support several endpoint execution styles.

Inline:

```cpp
solar::remote::Inline
```

Deferred to a work queue:

```cpp
solar::remote::WorkQueue<ControlQueue>
```

Owned by a service:

```cpp
solar::remote::ServiceMailbox<NavigationService>
```

Long-running asynchronous completion:

```cpp
struct CalibrateImuHandler {
    static void handle(
        const Request& request,
        remote::Responder<rpc::CalibrateImu> responder) {

        CalibrationService::begin(
            [responder](auto result) mutable {
                responder.reply(result);
            });
    }
};
```

The responder holds the correlation ID and route back to the client.

Something like:

```cpp
template<typename Method>
class Responder {
public:
    Status reply(
        const typename Method::Response& response);

    Status fail(RemoteError error);

private:
    CorrelationId correlation_;
    SessionId session_;
};
```

This avoids forcing every RPC into a blocking call.

# 6. Pub/sub should use typed topics

You can mirror the event bus:

```cpp
namespace topics {

struct Logs {
    static constexpr remote::TopicId id = 0x1001;
    static constexpr std::string_view name = "logs";
    using Message = LogRecord;
};

struct Pose {
    static constexpr remote::TopicId id = 0x1002;
    static constexpr std::string_view name = "robot.pose";
    using Message = RobotPose;
};

struct Metrics {
    static constexpr remote::TopicId id = 0x1003;
    static constexpr std::string_view name = "system.metrics";
    using Message = MetricsSnapshot;
};

}
```

Publish:

```cpp
solar::remote::publish<topics::Pose>(pose);
```

On the wire:

```text
frame_type = publish
topic_id = 0x1002
sequence = 1542
timestamp = ...
payload = ...
```

Subscription requests can also be part of the protocol:

```text
subscribe(topic_id=0x1002)
unsubscribe(topic_id=0x1002)
```

Solar then tracks subscriptions per remote session.

# 7. Distinguish topics from streams

They may both use pub/sub on the wire, but their delivery guarantees differ.

A normal topic may represent discrete occurrences:

```cpp
struct Faults {
    using Message = FaultEvent;
};
```

A stream may represent high-rate continuous data:

```cpp
struct ImuSamples {
    using Sample = ImuSample;
    static constexpr auto semantics =
        remote::StreamSemantics::LossTolerant;
};
```

I would model streams separately because they need extra policy:

* packet batching
* sequence numbers
* timestamps
* loss detection
* backpressure
* sampling
* downsampling
* bandwidth limits
* drop policy

For example:

```cpp
using Streams = solar::remote::Streams<
    solar::remote::Stream<
        streams::Imu,
        solar::remote::Batch<16>,
        solar::remote::RateLimit<500_Hz>,
        solar::remote::Overflow<
            solar::remote::DropOldest>>,

    solar::remote::Stream<
        streams::Lidar,
        solar::remote::LatestOnly,
        solar::remote::RateLimit<20_Hz>>>;
```

A high-rate stream should not share the same buffering assumptions as sparse events.

# 8. Streaming frames should be batch-oriented

Sending one frame per sample is often wasteful.

For an IMU stream, prefer:

```cpp
struct ImuBatch {
    std::uint32_t base_timestamp_us;
    std::uint16_t sample_period_us;
    std::uint8_t count;
    std::array<ImuSample, 16> samples;
};
```

Then one frame carries sixteen samples.

Benefits:

* less framing overhead
* fewer transport writes
* easier bandwidth control
* better CPU efficiency

For irregular samples, include per-sample timestamp deltas.

# 9. Every stream needs an overflow policy

The transport will eventually be slower than the producer.

You need to decide what happens.

For pose:

```cpp
LatestOnly
```

For logs:

```cpp
DropOldest
```

For commands:

```cpp
RejectNewest
```

For safety faults:

```cpp
NeverDrop
```

though “never drop” means you must reserve bounded storage or escalate to a fault when capacity is exhausted.

A stream descriptor might express:

```cpp
struct LidarStream {
    static constexpr remote::OverflowPolicy overflow =
        remote::OverflowPolicy::DropOldest;

    static constexpr std::size_t capacity = 4;
};
```

The protocol should expose dropped counts so the host knows the stream was lossy.

```cpp
struct StreamFrameHeader {
    TopicId topic;
    std::uint32_t sequence;
    std::uint32_t dropped_since_last;
    Timestamp timestamp;
};
```

# 10. Dynamic-length logs should not force dynamic allocation

Logs are an interesting special case because the payload length varies.

You have several options.

## Fixed maximum record

```cpp
struct LogRecord {
    LogLevel level;
    ComponentId component;
    Timestamp timestamp;
    std::uint16_t message_length;
    std::array<char, 128> message;
};
```

Simple, but wastes space and truncates long messages.

## Header plus variable payload in a ring buffer

This is probably better.

```cpp
struct LogRecordHeader {
    LogLevel level;
    ComponentId component;
    Timestamp timestamp;
    std::uint16_t payload_size;
};
```

Then store:

```text
[header][payload bytes]
```

inside a byte-oriented ring buffer.

That lets each log record use exactly as much storage as needed.

For example:

```cpp
RemoteLogs::write(
    LogRecordHeader{...},
    std::as_bytes(std::span(message)));
```

The ring buffer can support wraparound and dropping complete oldest records.

This is much better than a queue of fixed 256-byte objects when most logs are 30 bytes.

# 11. Structured logs are better than only strings

You can still support formatted strings:

```cpp
solar::log::info<Motor>(
    "target speed {}", speed);
```

But Remote can also expose structured records:

```cpp
struct MotorCommanded {
    static constexpr LogEventId id = 0x2001;

    int requested_speed;
    int applied_speed;
};
```

Then:

```cpp
solar::log::event<MotorCommanded>({
    .requested_speed = requested,
    .applied_speed = applied
});
```

On the host, it may render:

```text
Motor commanded: requested=120 applied=100
```

Structured logs are smaller, machine-readable, and easier to graph.

I would support both:

* textual logs for developers
* structured diagnostic events for tooling

# 12. ISR logging needs a separate fast path

An ISR must not:

* allocate
* format complex strings
* block on a mutex
* wait for transport
* invoke arbitrary subscribers

So the ISR logging API should be constrained.

For example:

```cpp
solar::log::isr<events::EncoderOverflow>({
    .channel = 2
});
```

or:

```cpp
solar::log::isr(
    LogLevel::Warn,
    component_id,
    "encoder overflow");
```

Internally it should write a compact record to a lock-free or IRQ-safe ring buffer:

```cpp
struct IsrLogRecord {
    Timestamp timestamp;
    LogEventId event;
    std::uint32_t arg0;
    std::uint32_t arg1;
};
```

Then the Remote service or logging task drains and expands it later.

For dynamic strings from ISR, I would either forbid them or cap them very tightly.

# 13. Framing should survive byte-stream transports

UART and USB CDC are byte streams. TCP is also logically a byte stream.

You need explicit framing.

A frame might look like:

```cpp
struct FrameHeader {
    std::uint16_t magic;
    std::uint8_t version;
    FrameType type;
    std::uint16_t flags;
    std::uint16_t header_size;
    std::uint32_t payload_size;
    std::uint32_t sequence;
    std::uint32_t checksum;
};
```

Then payload follows.

You also need a way to recover after corruption or dropped bytes.

Common approaches:

* magic bytes plus length plus CRC
* COBS framing
* SLIP framing
* length-delimited frames with resynchronization marker

For UART and USB CDC, COBS is particularly attractive because it gives clear packet boundaries without ambiguous delimiter bytes.

Conceptually:

```text
[COBS encoded frame][0x00]
```

Inside:

```text
[version][type][id][correlation][length][payload][crc]
```

For TCP, you may use the same framing for consistency even though TCP preserves order and reliability.

Using one framing layer across all transports simplifies the host implementation.

# 14. Separate framing from payload encoding

These are different concerns.

Framing answers:

* where does the message start?
* how long is it?
* is it intact?
* what kind of frame is it?

Encoding answers:

* how is `Pose` serialized?
* how are strings represented?
* how are enums encoded?
* how are arrays encoded?

So:

```cpp
using Protocol = remote::Protocol<
    remote::Framing<remote::CobsCrc32>,
    remote::Encoding<remote::BinaryCodec>>;
```

You could later support:

```cpp
remote::Encoding<CborCodec>
remote::Encoding<JsonCodec>
```

for debugging or interoperability, while retaining the same request/response semantics.

# 15. Prefer a compact explicit binary schema

Because you control both Solar and its host tooling, a compact binary schema makes sense.

You do not need full reflection machinery immediately.

A message can expose fields:

```cpp
struct SetParameterRequest {
    ParameterId parameter;
    ValueType type;
    std::span<const std::byte> value;
};
```

For strongly typed messages, you might define serialization manually:

```cpp
template<>
struct remote::Codec<SystemStatus> {
    static Status encode(
        Encoder& out,
        const SystemStatus& value);

    static Result<SystemStatus>
    decode(Decoder& in);
};
```

Or with field descriptors:

```cpp
struct SystemStatus {
    SystemState state;
    std::uint64_t uptime_ms;

    using Schema = remote::Fields<
        remote::Field<1, &SystemStatus::state>,
        remote::Field<2, &SystemStatus::uptime_ms>>;
};
```

That gives you stable field IDs and forward compatibility.

Field IDs are much better than relying only on member order if you expect the protocol to evolve.

# 16. Versioning needs to exist from day one

At minimum, every connection should negotiate:

```cpp
struct Hello {
    std::uint16_t protocol_major;
    std::uint16_t protocol_minor;
    std::uint64_t schema_hash;
    std::uint64_t capabilities;
};
```

The host sends or receives a handshake.

This allows both sides to determine:

* protocol compatibility
* available methods
* available topics
* supported features
* schema mismatch

A useful model:

* major version mismatch: incompatible
* minor version mismatch: compatible if required features exist
* schema hash mismatch: introspection or fallback may be needed

# 17. Introspection is extremely valuable

Since Solar already has a type-level graph, Remote should be able to expose it.

RPC methods could include:

```text
system.describe
rpc.list
topic.list
parameter.list
component.list
stream.list
```

For example:

```cpp
struct MethodDescriptor {
    MethodId id;
    std::string_view name;
    TypeId request_type;
    TypeId response_type;
    Permission permission;
};
```

And:

```cpp
struct TopicDescriptor {
    TopicId id;
    std::string_view name;
    TypeId payload_type;
    bool stream;
    RemoteAccess access;
};
```

This means a generic host application can discover what the firmware supports rather than requiring every UI to be hardcoded.

# 18. Sessions should be explicit

UART may have one peer.

TCP may have several.

USB CDC may reconnect.

So Remote should distinguish the protocol facility from connection sessions.

```cpp
template<typename Transport>
struct RemoteSession;
```

Each session owns:

* subscription set
* authentication or permission state
* outbound queue
* request correlations
* transport state
* stream rate limits

A topic publish then fans out only to subscribed sessions.

```cpp
Remote::publish<Pose>(pose);
```

internally becomes:

```text
for each connected session:
    if subscribed to Pose:
        enqueue according to session policy
```

This matters even if the first version only supports one session. The shape should not assume global subscription state.

# 19. Backpressure should be per session

A slow TCP client should not block the whole system.

Each session needs its own outbound buffering policy.

For example:

```cpp
using TcpRemote = RemoteService<
    TcpTransport,
    remote::Sessions<4>,
    remote::TxBuffer<4096>,
    remote::SlowClientPolicy<
        remote::Disconnect>>;
```

Possible policies:

```cpp
DropLowPriority
DropStreams
Disconnect
BackpressureProducer
Fault
```

For most embedded systems:

* responses should be retained
* critical faults should be retained
* logs may drop oldest
* high-rate streams should drop or downsample
* low-priority telemetry should go first

You could assign message priorities:

```cpp
enum class RemotePriority {
    Critical,
    Response,
    Event,
    Log,
    Stream
};
```

The outbound scheduler drains higher-priority frames first.

# 20. Avoid head-of-line blocking

Suppose a large LiDAR frame is queued ahead of a small RPC response.

You do not want the control response waiting behind megabytes of telemetry.

You can solve that with:

* separate queues by priority
* frame fragmentation
* a weighted scheduler
* stream throttling

For example:

```text
response queue
critical event queue
log queue
stream queue
```

The Remote service chooses from them each cycle.

For large stream frames, fragmentation helps:

```cpp
struct FragmentHeader {
    MessageId message;
    std::uint16_t fragment_index;
    std::uint16_t fragment_count;
};
```

Then small control traffic can be interleaved between fragments.

# 21. Request cancellation and timeouts

Long-running requests may need cancellation.

For example:

```text
calibration.start
firmware.upload
map.export
```

A protocol-level cancel frame can reference the correlation ID:

```cpp
struct CancelRequest {
    CorrelationId correlation;
};
```

The handler may then observe:

```cpp
responder.cancelRequested()
```

Similarly, the host should impose a timeout, but firmware can also expose expected timing metadata.

# 22. Permissions belong on endpoints and topics

Even without full authentication initially, define access policy now.

For example:

```cpp
struct Reboot {
    using Access =
        remote::access::Privileged;
};
```

Or:

```cpp
struct Logs {
    using Access =
        remote::access::ReadOnly;
};
```

Potential policies:

```cpp
Public
ReadOnly
ReadWrite
Privileged
LocalOnly
NotExposed
```

That will matter once TCP or remote network access appears.

# 23. Bridge the internal bus and Remote carefully

You will probably want to expose selected internal events remotely.

Do not automatically export every Solar bus event.

Instead:

```cpp
using RemoteTopics = solar::remote::Topics<
    solar::remote::Expose<
        events::BatteryLow,
        topics::BatteryLow>,

    solar::remote::Expose<
        events::PoseUpdated,
        topics::Pose>>;
```

This gives you explicit control.

Internally:

```cpp
solar::bus::emit(events::PoseUpdated{pose});
```

The bridge subscriber converts it into:

```cpp
solar::remote::publish<topics::Pose>(pose);
```

You may also expose remote incoming publishes to the local bus, but only through explicit adapters.

# 24. Logs should probably be a topic, but with a dedicated storage path

From the protocol perspective:

```cpp
struct Logs {
    static constexpr TopicId id = ...;
    using Message = LogRecordView;
};
```

From the implementation perspective, logs should have their own variable-record ring buffer because:

* they may originate before Remote is connected
* they may originate from ISR context
* they have variable length
* they may need filtering
* they may be retained temporarily

The Remote service then drains the log buffer into publish frames for subscribed sessions.

That is a good example where the external abstraction and internal machinery should differ.

# 25. A plausible public API

RPC definition:

```cpp
struct GetSystemStatus {
    static constexpr remote::MethodId id = 0x0001;
    static constexpr std::string_view name =
        "system.status.get";

    using Request = remote::Empty;
    using Response = SystemStatus;
};
```

Handler:

```cpp
struct GetSystemStatusHandler {
    static remote::Result<SystemStatus>
    handle(const remote::Empty&) {
        return SystemStatus{
            .state = solar::state(),
            .uptime = solar::time::uptime()
        };
    }
};
```

Topic:

```cpp
struct PoseTopic {
    static constexpr remote::TopicId id = 0x1001;
    static constexpr std::string_view name =
        "robot.pose";

    using Message = Pose;
};
```

Stream:

```cpp
struct ImuStream {
    static constexpr remote::TopicId id = 0x2001;
    static constexpr std::string_view name =
        "imu.samples";

    using Sample = ImuSample;
};
```

Registry:

```cpp
using RemoteRegistry = solar::remote::Registry<
    solar::remote::Rpc<
        solar::remote::Endpoint<
            GetSystemStatus,
            GetSystemStatusHandler>>,

    solar::remote::Topics<
        PoseTopic,
        LogsTopic>,

    solar::remote::Streams<
        solar::remote::Stream<
            ImuStream,
            solar::remote::Batch<16>,
            solar::remote::DropOldest,
            solar::remote::RateLimit<500_Hz>>>>;
```

Facility:

```cpp
using RemoteFacility =
    solar::facilities::Remote<
        RemoteRegistry,
        solar::remote::Protocol<
            solar::remote::CobsFraming,
            solar::remote::BinaryCodec>,
        solar::remote::Buffers<
            solar::remote::RxBytes<2048>,
            solar::remote::TxBytes<4096>,
            solar::remote::LogBytes<8192>>>;
```

Service:

```cpp
using RemoteService =
    solar::services::Remote<
        board::UsbCdc,
        RemoteFacility>;
```

# 26. What I would implement first

I would build Remote in this order:

1. Transport concept
2. Framing layer
3. Stable numeric IDs
4. Request/response with correlation IDs
5. Static RPC registry
6. Single-session outbound queue
7. Typed pub/sub topics
8. Subscription tracking
9. Log topic with variable-length ring storage
10. Streaming with sequence numbers
11. Drop counters and rate limiting
12. Introspection
13. Multiple sessions
14. Permissions
15. Async responders and cancellation

The first usable version can be surprisingly small:

```text
COBS framing
binary codec
request/response
topic subscribe/unsubscribe
publish
logs
```

Everything else can grow around that core.

# 27. The architectural rules I would keep

Remote should preserve a few hard boundaries.

**Types define local meaning.** RPCs, topics, streams, and payloads are C++ types.

**Numeric IDs define wire identity.** IDs remain stable across builds and refactors.

**The transport only moves bytes.** It does not know about methods, logs, or topics.

**The facility owns protocol state.** Registries, framing, sessions, queues, and serialization live there.

**The service owns asynchronous execution.** It drains transports, dispatches requests, and sends queued frames.

**No dynamic allocation is required.** Variable-sized payloads can live in byte rings and bounded frame buffers.

**Requests and streams have different reliability needs.** Do not let high-rate telemetry starve control traffic.

**ISR producers only enqueue compact data.** Formatting and transport work happen later.

**Remote exposure is explicit.** Internal bus events, parameters, and system actions are not automatically public.

That shape makes Remote feel like a first-class Solar subsystem rather than a UART parser that gradually acquires barnacles.
