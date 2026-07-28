# Python Host Stack Redesign

Date: 2026-07-28

Status: accepted implementation plan

## Purpose

This document defines the redesign of Solar's Python Remote and Station stack.
The implementation should preserve Solar's existing protocol and manifest
strengths while making the public Python interface typed, composable, and
natural to use.

The controlling ownership boundary is:

```text
solar_remote        protocol, manifests, schemas, sessions, typed values
solar_station       transports, discovery, supervision, routing, persistence
application tools   commands, presentation, and project workflows
```

Concrete serial and TCP ownership belongs to Station. `solar_remote` consumes
an already-open ordered asynchronous byte stream and does not select devices,
parse connection URLs, reconnect physical links, or depend on pySerial.

## 1. Remote Protocol Runtime

`solar_remote` retains:

- COBS and CRC framing;
- protocol envelopes, correlation, fragmentation, and cancellation;
- manifest parsing, validation, compatibility, and caching inputs;
- CBOR and packed codecs;
- subscriptions, inbound credit, and session epochs;
- generated and dynamic Python models; and
- typed protocol, schema, operation, and session errors.

It removes:

- concrete serial and TCP transports;
- USB and simulator discovery;
- physical target URL selection;
- reconnection policy; and
- the pySerial dependency.

The protocol boundary accepts an already-open channel:

```python
class AsyncByteChannel(Protocol):
    async def receive(self, maximum: int) -> bytes: ...
    async def send(self, data: bytes) -> None: ...
```

Station owns channel closure. A Remote session owns only protocol startup,
receive dispatch, output serialization, and protocol shutdown.

## 2. Station Connection Ownership

Station owns connector implementations and target discovery:

```text
solar_station/
├── connectors/
│   ├── base.py
│   ├── serial.py
│   └── tcp.py
└── discovery/
    ├── usb.py
    ├── bridge.py
    └── simulator.py
```

A connector opens a target and yields a connected channel carrying canonical
target identity, transport kind, device identity, connection time, and
generation. The connection supervisor selects a target, owns the connector,
attaches a fresh Remote session, and treats every reconnect as a new session
epoch.

Console transport remains separate and Station-owned.

## 3. Typed Manifest Model

Manifest parsing produces immutable descriptors rather than public
`dict[str, Any]` records:

- `SchemaDescriptor`;
- `FieldDescriptor`;
- `EnumValueDescriptor`;
- `DataEndpoint`;
- `ActionEndpoint`;
- `TopicEndpoint`;
- `StreamEndpoint`;
- `LinkDescriptor`;
- `CapabilityDescriptor`; and
- `InStreamGroupDescriptor`.

Every catalog supports constant-time lookup by stable ID and name:

```python
manifest.data["cockpit.mode"]
manifest.data.by_id(0x4200)
manifest.schemas["cockpit.ModeValue"]
```

Endpoint descriptors directly expose supported capabilities, schemas, policy,
and permissions. Callers do not scan manifest lists.

## 4. Typed Values

One model registry maps schema IDs to Python types and owns encode/decode:

```python
registry.type_for(schema_id)
registry.decode(schema_id, payload)
registry.encode(schema_id, value)
```

Generated mode uses normal dataclasses and enums. Dynamic mode creates
equivalent cached runtime dataclasses and enum classes. Required fields always
exist. Optional fields are represented by `T | None`. Closed enums reject
unknown values; open enums preserve them according to the manifest contract.

SDK and Station operations return these models end-to-end instead of
permanently normalizing them into dictionaries.

## 5. Endpoint And Operation API

Canonical public operation vocabulary is:

- `get`;
- `set`;
- `call`;
- `subscribe`;
- `open_input`; and
- `send`.

Protocol-internal query, update, and action terminology does not leak into the
ordinary application API.

Dynamic access:

```python
mode = station.robot.data["cockpit.mode"]
value = await mode.get()
await mode.set(ModeValue(mode=OperatingMode.MANUAL))
```

Generated applications may bind a namespaced facade:

```python
robot = ApplicationRobot.bind(station)
await robot.cockpit.mode.set(ModeValue(mode=OperatingMode.MANUAL))
```

Generated endpoints expose only statically supported operations. Dynamic
endpoints raise a typed `UnsupportedOperation` when a capability is absent.

## 6. Continuous Frames

All continuous delivery uses one generic type:

```python
@dataclass(frozen=True, slots=True)
class Frame(Generic[T]):
    value: T
    endpoint: Endpoint[T]
    sequence: int
    received_monotonic_ns: int
    received_wall_ns: int
    session_id: str
    loss_count: int
```

Subscriptions are generic async iterators and async context managers. They
expose closed state, close reason, queue capacity and policy, and cumulative
loss. No layer may silently discard an event.

Station persistence IDs remain recording metadata rather than fields in the
common live frame.

## 7. Station Public API

Station's public client is organized into typed resources:

```python
async with StationClient() as station:
    await station.robot.data["cockpit.mode"].set(...)

    source = station.sources["imu.euler"]
    await source.configure(frequency=100)
    async with source.subscribe() as frames:
        async for frame in frames:
            ...

    async with station.inputs[
        "cockpit.manual.drive.differential"
    ].open(frequency=50) as drive:
        await drive.send(DifferentialCommand(...))
```

The primary namespaces are:

- `station.robot`;
- `station.sources`;
- `station.inputs`;
- `station.logs`;
- `station.recordings`; and
- `station.connection`.

Status, source, recording, connection, and ping results are dataclasses.
Stringly typed `request(operation, **arguments)` remains an internal IPC
mechanism.

Numeric input handles remain an IPC detail. Public callers receive a typed,
context-managed input producer carrying endpoint, negotiated policy, credit,
closed state, and close reason.

## 8. Routing, Backpressure, And Persistence

Live fan-out must not wait for SQLite:

```text
Remote frame
   ├── immediate selective client fan-out
   └── bounded persistence admission
          → dedicated database worker
          → batched transaction
```

The writer batches by size or deadline and flushes when a recording stops.
Persistence overload is visible in recording loss metadata without delaying
manual control or live visualization.

All queues have an explicit policy: drop oldest, drop newest, disconnect, or
block. Loss is always observable.

For manual control, stale values are more dangerous than missing intermediate
values. Control endpoints should use a window of one or an explicit
latest-command policy unless their domain requires every command.

## 9. Ping/Pong

Remote gains a dedicated correlated ping and pong on the protocol-control lane.
The exchange carries a nonce, echoed host monotonic timestamp, and firmware
receive/send timestamps. The host reports round-trip and remote processing
time without requiring synchronized clocks.

Station exposes `station.connection.ping()`. Project tools expose repeated
sampling with minimum, median, mean, p95, maximum, and loss. A separate local
Station IPC measurement distinguishes process overhead from robot-link delay.

True control latency is measured separately by sequence IDs acknowledged at
the application point where a command is applied.

## 10. Application Tooling

Application tools consume the typed Station API and retain only command
declarations, human input syntax, completion, rendering, and composed
workflows.

Schema lookup, capability filtering, handle bookkeeping, and wire-shaped
dictionary checks move out of the application.

Expected command cleanup includes:

- merging stream listing and configuration;
- using one universal live-watch path;
- keeping log history separate from live source consumption;
- replacing generic open/send/close ceremony with context-owning project
  workflows; and
- adding endpoint inspection and ping.

## 11. Implementation Sequence

1. Land typed manifest descriptors and indexed catalogs.
2. Land the model registry and typed dynamic codecs.
3. Add endpoint objects and `Frame[T]`.
4. Move physical transports and discovery into Station.
5. Attach Remote sessions to Station-owned channels.
6. Add typed Station resources and context-managed input producers.
7. Move persistence to a bounded batched worker.
8. Add Remote ping/pong through firmware, Python, Station, and tooling.
9. Update generated clients to share the same endpoint abstractions.
10. Simplify application tooling and delete redundant schema plumbing.
11. Remove superseded compatibility APIs before the first stable release.

The packages are pre-1.0. Prefer one deliberate, documented breaking redesign
over permanent compatibility layers that preserve the weaker API.
