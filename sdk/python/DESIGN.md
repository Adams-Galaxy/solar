# Solar Remote Python SDK Design Notes

Date: 2026-07-28

Status: superseded boundary notes

The current cross-package design is canonicalized in
`docs/development-docs/python-host-stack-redesign.md`. In particular, that
document supersedes the concrete-transport ownership described below:
`solar_remote` consumes an already-open byte channel, while `solar_station`
owns serial, TCP, USB discovery, physical closure, and reconnection.

## 1. Product Boundary

The package is the reusable, project-neutral host implementation of Solar
Remote. It owns:

- protocol framing and messages;
- session negotiation and compatibility;
- manifest loading, validation, and caching;
- request correlation and cancellation;
- subscriptions, streaming, credits, and backpressure;
- generated and dynamic client models;
- an already-open asynchronous byte-channel boundary.

It does not own:

- an interactive CLI or terminal presentation;
- Argon integration;
- NATS or application routing policy;
- robot-specific endpoint vocabulary;
- concrete serial/TCP transport and discovery;
- process and reconnection supervision;
- console-log presentation; or
- a host daemon.

A C++ host SDK is deferred until there is a concrete need.

## 2. Async-First Contract

All I/O and session APIs are asynchronous from the first implementation. Sync
wrappers are not part of the initial contract.

The intended user experience is equivalent to:

```python
async with solar_remote.AsyncSession(channel) as client:
    value = await client.get("imu.euler")

    async for sample in client.stream("imu.euler", frequency=10):
        ...
```

The implementation uses one asyncio event loop for its public behavior. A
session owns a receive task, bounded inbound dispatch, request futures,
subscription iterators, and serialized output. Cancellation and disconnect
must complete outstanding operations predictably rather than leave hidden
tasks alive.

Automatic reconnect, if offered, is an explicit policy. Callers must be able to
observe reconnects, new session epochs, lost subscriptions, and retry decisions.

## 3. Transport Contract

Protocol code consumes an already-open asynchronous ordered byte stream:
operations equivalent to:

```python
class AsyncByteChannel(Protocol):
    async def receive(self, maximum: int) -> bytes: ...
    async def send(self, data: bytes) -> None: ...
```

Concrete TCP and threaded pySerial channels are implemented by Station.

The simulator exposes one fixed Remote endpoint at
`tcp://127.0.0.1:47000`. Supporting multiple simultaneous simulator instances,
dynamic published ports, or Unix-domain socket forwarding is outside the
initial scope.

pySerial is a normal required Station dependency. The
serial implementation does not depend on `pyserial-asyncio`. It bridges blocking
serial behavior through a small owned worker thread and bounded handoff into
the asyncio loop. Threading is an implementation detail; callers only see the
async transport contract.

Closing or cancelling a serial connection must wake both sides of the bridge,
join the worker, and prevent callbacks from targeting a closed event loop.
Writes are ordered, partial writes are handled, and blocking work never runs on
the event-loop thread.

USB CDC line coding must never accidentally select the Teensy bootloader's
reserved 134-baud trigger. The bootloader trigger remains an explicit operation,
not a side effect of opening a Remote client.

## 4. Package Shape

The intended source layout is:

```text
sdk/python/
├── pyproject.toml
├── src/solar_remote/
│   ├── client.py
│   ├── session.py
│   ├── manifest.py
│   ├── protocol/
│   ├── transports/
│   │   ├── base.py
│   │   ├── serial.py
│   │   └── tcp.py
│   ├── discovery/
│   │   ├── usb.py
│   │   └── simulator.py
│   └── generated/
└── tests/
```

The existing prototype under `tools/remote/solar_remote` is an implementation
input, not a second supported runtime. It should be migrated or replaced so
there is one protocol implementation in the SDK.

## 5. Generated And Dynamic Manifests

Both modes are first-class.

A generated firmware-specific package provides Python dataclasses, enums,
typed endpoint facades, and a baked-in manifest digest. It depends on the
standard `solar_remote` runtime and contains no separate protocol engine.

A dynamic client loads the same canonical manifest model at runtime and exposes
generic get, set, call, subscribe, and input operations by stable ID or
name. Dynamic operation is required for interactive inspection of a firmware
image whose generated package is not installed.

Manifest resolution follows this conceptual order:

1. an explicit manifest object or path;
2. an explicitly selected generated client package;
3. a local cache entry matching the device's advertised digest;
4. a manifest fetched from the device when supported; and
5. a dynamic-client error that clearly identifies what metadata is unavailable.

The SDK must not silently import one globally named application manifest. An
application may manage multiple firmware families in one process.

Generated Python does not require compilation. Generated packages may be
distributed with a firmware release or loaded directly from build artifacts.

## 6. Client Semantics

The SDK should expose:

- an async context manager for connection ownership;
- explicit device and endpoint discovery;
- `await`-based one-shot requests;
- async iterators for watch and stream delivery;
- visible subscription handles for long-lived routing;
- typed protocol and application errors;
- bounded queues and visible loss/backpressure facts;
- clean cancellation and timeout behavior; and
- stable access to the resolved manifest and compatibility result.

An application may open a stream and transfer its handle to an application-owned
router. The SDK does not assume that the command or function which creates a
subscription will also consume it.

## 7. Console And Logs

The first SDK connects only to Solar Remote. It does not tail the independent
Zephyr console interface, connect to the simulator log socket, or capture
simulator stdout.

Station consumes console/log streams independently:

- a dedicated USB CDC serial stream on embedded hardware; or
- rendered UTF-8 text from `tcp://127.0.0.1:47001` in simulation.

That unidirectional text stream is not a Solar Remote SDK transport. Station
owns its retention, presentation, and persistence. Typed Solar log queries and
subscriptions may later appear through ordinary Remote endpoints, but are not
part of the initial SDK.

## 8. Verification Expectations

The SDK design pass must define tests for:

- shared C++/Python protocol vectors;
- arbitrary read chunking and frame resynchronization;
- request correlation and out-of-order responses;
- cancellation, timeouts, disconnect, and reconnect epochs;
- subscription credits, queue limits, loss, and cleanup;
- deterministic manifest loading and digest checks;
- generated/dynamic behavioral equivalence;
- TCP integration through Docker-published loopback ports with native
  simulation; and
- serial worker startup, shutdown, faults, and event-loop safety using a fake
  serial boundary before hardware tests.
