# Remote

Remote is Solar's typed external protocol plane. It lets a host discover,
query, stream, configure, and control explicitly exposed capabilities without
an in-firmware text shell or mutable runtime registry.

## Model

- A Schema defines stable external fields and bounded CBOR or packed encoding.
- Data represents one resource with Query, Update, Watch, OutStream, or
  InStream capabilities.
- An Action is typed request/response RPC.
- A Topic is a standalone discrete publication.
- A Stream is a standalone sustained typed flow.
- A Link adapts an asynchronous byte transport.

The Remote Facility owns catalogs, codecs, dispatch, and typed frontends. The
Remote Service owns protocol execution, sessions, queues, fragments, request
slots, and output scheduling. Links own transport buffers and in-flight I/O.
Canonical application state remains in its source subsystem.

## Contribute capabilities

```cpp
struct Application {
    using RemoteData = remote::ContributeData<Telemetry>;
    using RemoteActions = remote::ContributeActions<Calibrate>;
    using RemoteLinks = remote::ContributeLinks<DebugUartLink>;
};
```

A Data declaration chooses independent capabilities. One source can therefore
be queried for its current value and streamed without creating two unrelated
endpoint identities.

## Coherent acquisition

Remote never serializes a borrowed reference to live mutable state. Query and
poll readers return an owned coherent value, acquiring source locks only long
enough to copy. Push sources call `remote::write<Data>(value)` when a sample
exists. Latest and bounded Queue policies define retention. Loaned acquisition
uses generation-checked fixed slots for large producer-filled buffers.

## Actions and inbound data

Actions execute Inline or on an explicit Execution target. Inline work must be
strictly bounded and non-blocking. A move-only `Responder<Action>` supports
bounded asynchronous completion without a heap promise. Update capabilities
delegate validation to the canonical source.

Inbound Streams are explicit, token-scoped producer sessions. The host opens a
declared input with a requested policy; firmware runs its open callback, returns
the effective policy and a non-zero token, and only then grants bounded credit.
Every sample carries that token. Close, replacement, disconnect, reset, and
fault use the same idempotent closure path and invalidate old queued
generations.

`Exclusive<Group, Replace>` provides a system-wide mode switch across Data
endpoints and links. `RejectExisting` is available when takeover must fail
instead. Stable group identity, lifecycle support, replacement behavior,
maximum rate, codec, and credit window are all present in the manifest.

## Sessions and backpressure

Each connection negotiates protocol version and bounds. Permissions are link
grants refined by session authorization. Request IDs provide correlation,
duplicate suppression, cancellation, response acknowledgement, and bounded
cache behavior.

Output is separated into finite priority lanes. Streams expose sequence and
loss accounting; they do not promise lossless telemetry. Reliable inbound
streams require token-specific credits. Credit from an old generation is never
transferred to its replacement. Fragmentation, reassembly slots, timeouts, and
all message/frame sizes are Kconfig bounded.

See {doc}`../tutorials/remote-control`, {doc}`../reference/api/remote`, and
{doc}`../reference/remote-protocol`.
