# Control A System With Remote

The `examples/remote-control` application defines a queryable and streamable
Telemetry resource plus a typed Scale Action.

## Define external value types

```{literalinclude} ../../examples/remote-control/src/main.cpp
:language: cpp
:start-after: // [types]
:end-before: // [types]
```

Every externally encoded type has a `remote::Schema<T>` specialization with a
stable nonzero type ID, ordered field IDs, bounded encoded size, and codec.
Field IDs are the compatibility contract; C++ member names and layout are not.

## Define Data and an Action

```{literalinclude} ../../examples/remote-control/src/main.cpp
:language: cpp
:start-after: // [endpoints]
:end-before: // [endpoints]
```

Telemetry uses an owned Query return and producer-pushed Latest stream. Scale
is an inline typed Action. Application code publishes a sample with
`remote::write<Telemetry>(sample)`.

## Add a Link

The example's in-memory Link gives tests deterministic RX/TX leases and
connection events. A production Link implements the same asynchronous contract
around UART, USB CDC, TCP, or another transport. Driver callbacks only publish
link events; parsing and application dispatch run in the Remote Service.

## Build and inspect host artifacts

```sh
west build -b native_sim/native/64 examples/remote-control
python examples/remote-control/host_demo.py --generated build/solar/remote
```

The post-link generator emits `manifest.cbor`, `manifest.json`, digest,
constants, a bounded Python client, and a C++ manifest header. The host script
loads the exact generated client for that firmware image.

For a real transport, feed received bytes to the generated Client, write every
frame returned by `take_outgoing()`, and keep pumping both directions. Once the
hello exchange marks the session active, use the manifest IDs to issue Query,
Action, Subscribe, and inbound-stream operations. Responses remain correlated
by request ID; stream messages carry endpoint identity and sequence.
