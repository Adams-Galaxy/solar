# Stage 14: Remote Runtime And Integration

Status: landed

Landed date: 2026-07-16

Implementation repositories/branches:

- `/workspaces/solar`, `static_reform`;
- `/workspaces/ENMT301-RoboCup`, `dev`, for firmware integration only.

Relevant commits or change identifiers: uncommitted reform working trees

## 1. Objective

Stage 14 turns the frozen Stage 13 wire and manifest into Solar's bounded,
asynchronous Remote runtime. It supplies demand-derived facility and service
components, replaceable links, sessions, authorization, request correlation,
typed application execution, bidirectional data flow, explicit subsystem
adapters, generated host clients, focused records, and native/Teensy firmware
integration.

The implementation preserves the governing ownership rule: Remote transports
and correlates values, but never becomes canonical storage for Parameters,
Metrics, Events, Logs, lifecycle, execution, or application state.

## 2. Specification Coverage

| Specification | Sections implemented | Notes |
| --- | --- | --- |
| `10-remote.md` | 4, 6-8, 14-25, 27, 29-34, 37-42 | Facility/service ownership, links, sessions, capabilities, execution, acquisition, ingress, queues, backpressure, adapters, host client, records, and firmware integration are landed. |
| `00-design-conventions.md` | static ownership, bounded storage, Kconfig exclusion, no hidden heap | Every runtime capacity is compile-time or Kconfig bounded. |
| `01-system-blueprint-and-binding.md` | demand-derived built-ins and bound activation | Remote components appear only when the effective Link catalog is non-empty. |
| `02-identity-contributions-and-catalogs.md` | typed endpoint catalogs and provenance | Data, Actions, Topics, Streams, Schemas, and Links retain distinct identity domains. |
| `04-bus.md` through `09-tasks-and-executors.md` | explicit exposure and execution adapters | Remote delegates to canonical subsystem APIs and visible Execution registrations. |

## 3. Public Surface Landed

The runtime aggregate remains:

```cpp
#include <solar/remote.hpp>
```

Applications contribute declarations and links through ordinary component
aliases:

```cpp
struct RobotApplication
{
    using RemoteData = solar::remote::ContributeData<
        RemoteDriveGain,
        RemoteStartupRuns,
        RemoteLifecycle>;

    using RemoteActions = solar::remote::ContributeActions<CalibrateImu>;
    using RemoteLinks = solar::remote::ContributeLinks<DebugUart>;
};
```

The common typed runtime paths are:

```cpp
solar::remote::write<ControlTelemetry>(sample);
solar::remote::publish<MapUpdate>(tile);
solar::remote::interested<ControlTelemetry>();
solar::remote::records::service();
solar::remote::records::links();
```

Data capabilities support typed Query, Update, Watch, OutStream, and InStream.
Acquisition supports source-owned Push, Poll, Latest, bounded Queue, Batch, and
generation-checked Loaned staging. Publication supports standalone Topics and
Data Watch surfaces. Inbound Streams use owned windows and explicit credits.

Actions support explicit Inline or generated visible Execution registrations.
Omitted execution selects Zephyr's system workqueue. `On<Target>` preserves the
authored target. Move-only `Responder<Action>` supports retained asynchronous
completion, domain failure, cancellation observation, abandonment, and
session-generation rejection without a heap promise.

## 4. Runtime Ownership

| Owner | Storage/resource | Capacity | Synchronization | Lifetime |
| --- | --- | --- | --- | --- |
| `remote::Facility<Architecture>` | frontend bindings, active request count, endpoint runtime state | exact effective declarations plus policy capacities | atomics and focused spinlocks | bound System lifetime |
| `remote::Service<Architecture>` | event queue, service thread, output lanes, sessions, response cache, fragments | Remote Kconfig ceilings per effective Link | message queue, atomics, focused spinlocks | lifecycle start to stop |
| each Link declaration | driver, transport, RX/TX lease state | link-authored static capacities | link-defined; ISR/callback safe | link open to close |
| each Push/Watch/Topic endpoint | latest value or bounded queue | declaration policy | focused spinlock/atomics | bound System lifetime |
| each Poll endpoint | release state and generated Execution registration | one in-flight release per endpoint | atomics and registration state | lifecycle lifetime |
| each InStream endpoint | per-Link owned value window | typed reliable window within Kconfig ceiling | focused spinlock | bound System lifetime |
| each Loaned endpoint | explicit fixed loan pool | declaration policy | generation-checked slots | bound System lifetime |
| each asynchronous Action | one retained responder generation per Action type | bounded by request and response ceilings | focused spinlock | request admission to completion/cancel/reset |
| generated host client | host-side decoder, reassembly, outgoing queue | constructor-selected bounds | caller owned | host session lifetime |

Remote does not allocate after boot. Producers never retain a client-owned
buffer and do not wait for a disconnected or slow client.

## 5. Facility, Service, And Link Model

Remote's facility and service are generated only when the bound system has at
least one effective Link. The facility owns typed dispatch and endpoint
frontends. The service owns the protocol pump, session and transport state,
bounded output scheduling, and one visible lifecycle-managed service thread.

The Link concept exposes asynchronous open, retained RX leases, retained TX
leases, exact completion identity, and close. Stage 14 supplies:

- deterministic in-memory links;
- a fake-DMA link used to prove retained lease lifetime and short transfer
  progression;
- Zephyr interrupt-driven UART integration;
- Zephyr async-UART integration;
- an optional link `poll()` hook for driver contexts that must defer callback
  notification into the Remote service context.

The async UART link uses Zephyr's UART async API and static driver/application
buffers. Native simulation uses its second PTY UART, preserving the first UART
for console output. Teensy uses the proven interrupt UART path.

## 6. Sessions, Security, And Correlation

Every Link has independently bounded session state. The runtime implements:

- ServerHello/ClientHello negotiation;
- major-version and schema compatibility checks;
- explicit link grants and endpoint access requirements;
- session epoch containment;
- monotonic nonzero request IDs;
- duplicate suppression;
- response reservation and bounded replay cache;
- response acknowledgement and early cache release;
- request cancellation;
- disconnect/session reset cleanup;
- late completion rejection.

Multiple simultaneous sessions are proven across independent Links. Session
subscriptions, credits, output lanes, response caches, and reconnect behavior
remain link local while global source interest reflects all surviving sessions.

## 7. Scheduling And Backpressure

Each session owns five independently admitted output lanes:

1. protocol control;
2. request responses;
3. important publications;
4. telemetry;
5. bulk stream data.

Control and response traffic are not trapped behind telemetry. Push and Watch
sources use policy-sized Latest or Queue ingress. Queue overflow follows the
declaration's DropOldest, DropNewest, or Reject policy. Rate negotiation clamps
host requests through endpoint and Kconfig ceilings. Poll acquisition uses a
visible generated registration and never overlaps one endpoint's in-flight
reader. Inbound Streams use per-Link credits and return credit only after the
consumer completes.

Logical messages fragment across bounded physical frames and reassemble in
ordered, timeout-reclaimed slots. Outbound messages own copied logical payloads
until every fragment has been staged. Lane interleaving remains available
between fragments.

## 8. Explicit Subsystem Exposure

`solar/remote/adapters.hpp` supplies opt-in adapters for:

- readable and externally writable Parameters;
- selected Metric views and Poll streams;
- lifecycle state;
- Bus input messages;
- Event records;
- Logging records;
- Execution registrations;
- component lifecycle records;
- graph category counts;
- Event-to-Topic bridges;
- Log-to-Topic sinks.

The adapters call owning subsystem APIs and copy coherent bounded values. The
Event and Log bridges are real components in the adapter fixture, so their
processor/sink contributions and Remote Topic catalogs are exercised through
canonical event observation and logging APIs.

## 9. Host Runtime And Generation

The Python package now supplies a transport-independent `Client` with:

- handshake and negotiated limits;
- incremental frame decoding;
- bounded logical-message reassembly;
- request IDs and frame sequencing;
- Action, Query, Update, subscribe, unsubscribe, Cancel, ACK, credit, and
  introspection operations;
- response and publication messages;
- session reset behavior.

The final-ELF generator emits `client.py` as a declared CMake byproduct. Its
`FirmwareClient` inherits the shared runtime and adds exact generated endpoint
constants and the firmware schema digest.

Firmware's `scripts/check_remote_native.py` launches the actual native_sim
image, discovers the dedicated Remote PTY, loads the generated client, performs
the two-step handshake, issues a Query against `DriveGain`, decodes canonical
CBOR, verifies the live value `1.25`, and acknowledges the response.

## 10. Runtime Introspection And Records

When `CONFIG_SOLAR_REMOTE_RUNTIME_INTROSPECTION=y`, the protocol exposes a
fixed bounded summary containing effective Schema, Data, Action, Topic, Stream,
and Link counts plus configured frame/message ceilings. It is derived from the
bound System during facility activation, so demand-derived catalogs are
included correctly.

Generic collection paging remains Stage 15 Inspection responsibility.

Focused records include:

- service readiness, acceptance, event counts, drops, and event high-water;
- link session state, grants, frame/byte counts, errors, connects, duplicates,
  completions, subscriptions, and TX state;
- per-lane depth, high-water, enqueued, sent, replaced, and dropped counts;
- acquisition, stream, ingress, loss, and credit facts where applicable.

Records are copied coherently and do not expose mutable references.

## 11. Compile-Time Behavior

Binding validates effective endpoint identity, capability collisions, schemas,
execution target forms, rates, queue/window/loan capacities, and global Kconfig
ceilings. Strict and relaxed catalog binding produce the same runtime graph and
wire behavior.

Generated Action and Data registrations are declaration specific. Action and
Data identity domains are dispatched independently, so equal numeric IDs in
different domains cannot select the wrong registration.

Remote-disabled builds retain protocol types where accepted by Stage 13 but
reject authored runtime declarations. Facility, service, thread, queues,
sessions, and endpoint state are absent when no Link is effective.

## 12. Zephyr And Firmware Integration

Solar uses Zephyr Message Queues, threads, workqueues, UART interrupt/async
drivers, devicetree devices, zcbor, COBS, CRC32C, and final-ELF build hooks. It
does not introduce a private scheduler, allocator, serial stack, or background
executor.

Firmware contributes three real Remote Data adapters. Teensy contributes an
interrupt-driven UART Link selected from devicetree. native_sim enables `uart1`
through a board overlay and contributes an async-UART Link. Board-specific UART
Kconfig lives in board configuration files rather than shared `prj.conf`.

Application-specific Remote ceilings reduce the firmware image from the broad
development defaults to the capacities needed by the current graph.

## 13. Files Changed

### Added

- `include/solar/remote/{api,facility,link,runtime,service,adapters}.hpp`;
- `include/solar/remote/links/{async_uart,interrupt_uart}.hpp`;
- `include/solar/remote/testing/{fake_dma_link,in_memory_link}.hpp`;
- Remote runtime Zephyr fixtures for links, runtime, polling, queues, loans,
  topics, multi-session operation, ingress, fragmentation, and adapters;
- `tools/remote/solar_remote/client.py`;
- firmware native board configuration/overlay and
  `scripts/check_remote_native.py`.

### Reshaped

- Remote declarations, protocol, manifest, contribution, and aggregate
  headers;
- Remote Kconfig and CMake generation outputs;
- firmware application graph, Kconfig ceilings, and board Link selection.

## 14. Tests And Evidence

| Command/gate | Platform/configuration | Result | What it proves |
| --- | --- | --- | --- |
| host build plus `ctest` | GCC 13, C++23 | 57/57 pass | C++/Python protocol parity, generation, compile failures, and host regressions |
| focused runtime responder matrix | native_sim 64, relaxed/strict/introspection | 3/3 configurations and cases, no warnings | retained completion, exactly once, cancellation, late rejection, and target execution |
| focused adapter matrix | native_sim 64, relaxed/strict | 2/2 configurations and cases, no warnings | canonical adapters plus real Event/Log Topic bridges |
| complete Remote Twister matrix | native_sim 64 | 24/24 configurations, 26/26 cases, no warnings | all links, sessions, capabilities, backpressure, fragments, adapters, and binding modes |
| generated client against native firmware | native_sim PTY UART | pass | actual process handshake, generated constants, Query, response, CBOR decode, and ACK |
| native firmware build | native_sim/native/64 | pass | board overlay/config merge, async UART, Remote service, and generated artifacts |
| Teensy firmware build | teensy40 | pass | physical UART compile/link, ISR path, lifecycle ownership, and generated artifacts |
| `git diff --check` | both repositories | pass | no whitespace errors |

The final Teensy image uses 149,568 bytes flash (7.13%) and 61,824 bytes RAM
(23.58%) in the reported primary regions. Before application-specific ceilings,
the same integration reached approximately 184,468 bytes flash and 107,136
bytes RAM. The reduction validates that Remote's major pools obey Kconfig and
application graph sizing instead of hidden dynamic allocation.

## 15. Implementation Decisions

### 15.1 Subscription Kind In Reserved Envelope Metadata

Problem: Data Watch and standalone Topics share subscribe operations while
their stable ID domains may contain equal numeric values.

Decision: encode a compact subscription-kind discriminator in frozen reserved
envelope metadata. Dispatch remains typed and domain aware without changing the
v1 envelope size.

Physical implementation: `remote/protocol.hpp`, `remote/runtime.hpp`, and the
Python protocol/client package.

### 15.2 Ordered Reassembly

Problem: arbitrary out-of-order fragments require more metadata, storage, and
DoS-sensitive bookkeeping than ordered byte-stream links need.

Decision: v1 reassembly accepts ordered fragments, rejects sequence conflicts,
and reclaims bounded slots on timeout. Links remain free to supply reliable
ordering beneath Remote.

### 15.3 Copied Outbound Logical Messages

Problem: callers cannot retain source buffers while a fragmented message waits
behind other output lanes.

Decision: Remote copies accepted logical messages into explicit bounded
service-owned message slots and releases source-owned loans promptly.

### 15.4 Per-Declaration Execution Registrations

Problem: one shared work registration silently forced Actions and Data readers
onto the system workqueue and could collide across endpoint identity domains.

Decision: generate one visible registration per non-inline Action and per
deferred Data declaration, preserve authored `On<Target>`, and dispatch Action
and Data IDs separately.

### 15.5 Fixed Runtime Summary Before Inspection

Problem: Remote needs a useful compatibility query in Stage 14, while generic
enumeration and paging belong to Stage 15.

Decision: add one opt-in fixed protocol summary derived from the bound System.
Do not create a temporary Remote-owned inspection registry.

### 15.6 Native PTY Callback Deferral

Problem: native_sim's UART model may invoke callbacks from a context that must
not directly wake Solar kernel primitives.

Decision: permit Links to expose `poll()`. `AsyncUart` records callback facts
and the Remote service delivers them during its normal maintenance cadence.
This keeps the Link contract asynchronous and avoids a hidden Link thread.

### 15.7 Board-Specific UART Configuration

Problem: enabling interrupt UART globally polluted native builds and coupled
unrelated boards to a transport implementation.

Decision: keep UART mode selection in `boards/teensy40.conf` and
`boards/native_sim_native_64.conf`. Shared `prj.conf` owns only Remote-wide
capacity and capability ceilings.

## 16. Known Limits And Deferred Work

- `Mailbox<Target>` remains a declaration extension point. Execution currently
  has no accepted typed service-mailbox contract, so Remote does not pretend a
  workqueue is one.
- The current physical Link model represents one live session per byte-stream
  Link declaration. Concurrent sessions are supported through independent
  Links. Multiplexed sessions over one transport require an explicit future
  transport contract.
- Host cancellation and session loss contain pending and retained asynchronous
  work. A configurable per-Action request deadline and cooperative stop token
  remain to be completed before claiming arbitrary long-running Action timeout
  policy.
- The Teensy prototype currently selects the chosen console UART. A deployed
  board should dedicate a devicetree UART to Remote or deliberately disable the
  competing console path.
- Native PTY UART on the pinned Zephyr revision emits a simulator diagnostic
  when its internal async polling thread wakes the simulated CPU. The firmware
  remains alive and the end-to-end protocol gate passes; this is isolated to
  native_sim and is not present in target builds.
- Physical Teensy runtime traffic was not exercised without attached hardware;
  the target gate is compile/link and driver API validation.
- Generic descriptor/record enumeration, filters, paging, formatting, and
  Remote collection exposure belong to Stage 15 Inspection.

## 17. Documentation Handoff

Public documentation must cover endpoint declarations, capability selection,
source-owned acquisition, queue/loss policy, responder lifetime, explicit
execution targets, grants, sessions, Link contracts, UART board selection,
generated host client use, resource ceilings, records, and the strict boundary
between explicit subsystem adapters and canonical subsystem ownership.

The Remote Zephyr fixtures and native interoperability script are executable
examples. Public documentation should not imply that local Inspection
availability automatically grants Remote exposure.

## 18. Closure Statement

Stage 14 is complete because the demand-derived runtime, asynchronous links,
sessions, authorization, bounded requests and responses, all accepted initial
Data capability families, Action execution and responders, acquisition,
publication, ingress, backpressure, fragmentation, explicit subsystem
adapters, generated host client, focused records, native process
interoperability, target builds, and regression gates are landed without
duplicating canonical subsystem state.

Stage 15 can now provide generic Inspection collections over these same
descriptors and records rather than inventing another Remote-specific query
registry.
