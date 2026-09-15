# Remote Output Stream Producer Scheduler

Date: 2026-08-07

Status: **implemented and verified, 2026-09-15 -- Stage 6 is unblocked.**
Split out of `remote-arrays-and-chunked-streaming.md` Stage 6 after
implementation surfaced a gap in that document's own premise; landed in a
separate, dedicated implementation pass before Stage 6 resumed, as intended.
See §7 for what actually landed, including two deliberate scope expansions
beyond this document's original sketch (`ReliableWindow` reachable through
the scheduler, not just best-effort `Queue`; a manifest-emission bug found
and fixed along the way).

Depends on:

- `remote-arrays-and-chunked-streaming.md` (the parent design; this document
  exists because that one's §4 "Current State" claims a piece of machinery
  is reusable that in fact has no application-facing entry point yet)
- Stage 5 of that same document (`OutStream<Push, ReliableWindow<Count>>`),
  already implemented and verified in `include/solar/remote/runtime.hpp` /
  `service.hpp` -- real, working, but only reachable from a hand-built test
  harness, never from an ordinary Solar service.

## 1. Problem

`remote-arrays-and-chunked-streaming.md` Stage 6 wants `navigation.lidar.scan`
(currently a `Data` query/response, `firmware/schemas/navigation.solar.yaml`)
to become a real, schema-declared, chunked `navigation.lidar.points`
*producer-paced* stream: the firmware autonomously publishes a
`LidarScanChunk` (`generation`/`chunk_index`/`final`/`points: Array<LidarPoint,
64>`) whenever one becomes ready, not on a station request/response cycle.

That parent document's §4 states this is safe to build on because
`OutStream<Push, Policies...>` "already supports queued, batched delivery."
That's true at the runtime-template level -- Stage 5 built and verified real
`Queue<Depth,Overflow>`/`ReliableWindow<Count>` support for it
(`include/solar/remote/runtime.hpp`, `PushStoragePolicy<OutStream<Push,
Policies...>>`, `publish_reliable_out_stream`, etc.) -- but investigating
Stage 6 surfaced that **no code path lets an ordinary Solar service call it**.
Every device-to-station data flow that a real service (`Navigation`,
`Cockpit`, ...) can actually reach today is framework-*pull*: the runtime
calls into the service on a schedule (`Data::Read`, `Watch`, `Topic`), never
the other way around. `OutStream<Push,...>` has only ever been exercised by
a hand-rolled `solar::remote::Architecture`/`ByteRuntime` test harness that
bypasses the whole `Endpoints`/`Dispatch`/`Contract` layer real services are
written against (see `tests/zephyr/remote_outbound_credit/src/main.cpp` and
the ENMT301-RoboCup production rebuild used to verify Stage 5).

This document is the design for closing that gap: giving a real service
(concretely, `Navigation`) a way to have the runtime periodically pull a
value from it and push that value onto a genuine `OutStream<Push,...>`-style
delivery pipeline, through the normal schema/codegen/application pipeline
every other endpoint in this codebase goes through.

## 2. Current architecture (what exists today, with exact locations)

Two, mostly-separate mechanisms already exist for "device publishes a value
that stations can subscribe to." Neither has a producer path a service can
call.

### 2.1 `Data` + `Capabilities<OutStream<Push, Policies...>>` (rich, but unreachable)

- Declared via `include/solar/remote/declaration.hpp:671`:
  `template <typename Acquisition, typename... Policies> struct OutStream`.
- Runtime machinery lives in `include/solar/remote/runtime.hpp`: `PushState<DataT>`
  (ring buffer), `PushStoragePolicy<OutStream<Push,Policies...>>` (now supports
  `Queue<Depth,Overflow>` *and*, since Stage 5, `ReliableWindow<Count>`),
  `write_data<System,DataT>` (the actual producer-side write call),
  `process_stream_publication_for` (runtime.hpp:1437), `poll_reliable_out_streams`
  (runtime.hpp:1573, wired into `Service::run()`'s maintenance loop at
  `service.hpp:1636`).
- Types using this live in `System::RemoteDataCatalog` (`ArchitectureT::Data`),
  keyed by `DataDescriptor`.
- **The gap:** `write_data<System, DataT>` needs `System = RuntimeContext<Architecture>`.
  Nothing generated from a yaml schema, and nothing a hand-authored service struct
  like `firmware/include/services/navigation/navigation.hpp` can see, ever
  produces a concrete, nameable `RuntimeContext<Architecture>` -- that type
  is only assembled inside `solar::detail::RemoteSynthesis`
  (`include/solar/application.hpp:397-420`), which itself is only instantiated
  once a project's top-level `Application<...>` composition is written,
  strictly *after* every service header (including `navigation.hpp`) has
  already been fully parsed and type-checked as an ordinary (non-template)
  struct. There is no existing forward-declaration/deferred-instantiation
  seam for this direction, unlike the `facility.hpp` one Stage 5 reused for
  `poll_reliable_out_streams`.

### 2.2 `ArchitectureT::Streams` / `RemoteStreamCatalog` (wired, but a dead end)

- `concept Stream` (`declaration.hpp:1031`) just requires `Value` +
  `StreamDescriptor`; no `Capabilities` requirement.
- Both `direction: in` *and* `direction: out` schema streams
  (`firmware/schemas/*.yaml`, `streams:` section) compile into this catalog
  (`tools/solar_codegen/compiler.py:1547-1717`, `remote_streams` list feeding
  `RemoteStreams = solar::remote::ContributeStreams<...>` at compiler.py:2416).
- `direction: in` (e.g. Cockpit's `cockpit.manual.drive.*`) is fully wired and
  in production: wire-triggered frames dispatch into
  `in_stream_state<System,DataT>`/`consume()`/`open()`/`close()` -- a
  completely separate machinery from what follows.
- `direction: out` today: codegen literally emits `using Capabilities =
  solar::remote::Capabilities<>;` (compiler.py:2344) -- empty, no policy.
  The delivery machinery it *would* use already exists and is already
  production-tested via `Watch`/`Topic`: `stream_state<System,StreamT>()`
  (`runtime.hpp:911`) hardcodes `Publication = Watch<Latest,
  MultipleProducers>`, and `write_discrete`/`publish_stream`
  (`runtime.hpp:918-1003`) is the same generic bounded-ring-with-overflow
  primitive `Watch`/`Topic` use (`DiscreteState`, `DiscreteStoragePolicy`,
  `runtime.hpp:283-290`). Wire-side fan-out
  (`process_stream_endpoint_publication_for`, `runtime.hpp:1512`) is already
  wired into `process_publication` and would work today for any subscriber.
  **What's missing is only the producer side**: nothing in `runtime.hpp` or
  `service.hpp` ever calls `publish_stream`/`write_discrete` for a
  `direction: out` entry. The one public entry point that could
  (`ByteRuntime::publish<StreamT>`, `runtime_context.hpp:176`) is never
  called by generated code either.
- A **separate, already-generated, currently-inert binding exists for
  exactly this producer role and is not wired to anything**:
  `solar::endpoint::Output<Endpoint, Publisher>`
  (`include/solar/system/endpoints.hpp:86-96`) plus
  `Dispatch::publish<Stream>()` (`include/solar/system/contributions.hpp:641-651`,
  calling `Binding::publish()` which calls the service's own `Publisher`
  reader function). Codegen already emits the local (non-Remote,
  `Endpoints`-independent) alias `template <auto Publisher> using Output =
  solar::endpoint::Output<{cpp_name}, Publisher>;` for every stream
  regardless of direction (compiler.py, stream-declarations loop,
  ~line 1975). **Nothing calls `Dispatch::publish<Stream>()`.** Confirmed by
  grep: `contributions.hpp:436` and `:647` are the only two references to
  `Binding::publish()`/`::publish()` in the whole framework -- it is dead
  code from the runtime's perspective.

### 2.3 Why nothing exists to migrate for Stage 7's log bridge / metrics export

Direct consequence of §2.2: since `direction: out` has never had a working
producer path, nothing could have been built on it yet. This corroborates
the finding already reported to the user during Stage 6 exploration -- the
log bridge and metrics exporter `remote-arrays-and-chunked-streaming.md`
assumes exist do not exist anywhere in this codebase.

### 2.4 The `Dispatch`-before-`Architecture` ordering problem

`include/solar/application.hpp:397-420` (`RemoteSynthesis`):

```cpp
using Dispatch = system::Dispatch<Contract, Components>;
using GeneratedContract =
    typename GeneratedRemoteTraits<Application>::template Contract<Parameters, Dispatch>;
using Architecture = remote::Architecture<..., typename GeneratedContract::RemoteStreams::Entries, ...>;
using Runtime = remote::ByteRuntime<Application, Architecture, Dependencies>;
```

`Dispatch<Contract, Components>` is built *before* `Architecture`/`Runtime`
exist, from `Contract` (fixed, per-project, generated from yaml) and
`Components` (the concrete service list, known only at this exact point in
the top-level application composition). `Dispatch` itself never references
`Architecture` -- it only resolves "which `Component` in `Components` owns
which `Contract` entry" (`FindOwner`, `contributions.hpp`). This is good:
it means `Dispatch::publish<Stream>()` *can* be called successfully any time
after this point, including from further down in the same
`RemoteSynthesis`/`Application` assembly -- `Dispatch` is fully valid, it is
just never invoked.

The reverse direction (something inside `runtime.hpp`/`service.hpp` calling
`Dispatch::publish<Stream>()` to *pull* a value, then forwarding it into
`publish_stream`) needs `Dispatch` threaded down into wherever the periodic
maintenance tick lives (`Service::run()`, `service.hpp:1636`, same spot
Stage 5's `poll_reliable_out_streams` was added). `RuntimeContext<ArchitectureT>`
(`runtime_context.hpp:53`) and `ByteRuntime<Application, ArchitectureT,
DependenciesT>` (`runtime_context.hpp:81`) are both built from `ArchitectureT`
alone today -- neither carries `Dispatch`.

## 3. Design goals

- A real service (`Navigation`) declares a producer for a `direction: out`
  stream the same way it already can declare a `Data` reader today
  (`LidarScanData::Read<&lidar_scan>`, `navigation.hpp:76-77`) -- i.e. reuse
  the *already-generated, already-correct* `Output<Endpoint, Publisher>`
  local binding (`solar::Endpoints<..., LidarPointsStream::Output<&next_chunk>>`),
  not a new declaration shape.
- The runtime calls that `Publisher` on a schedule (reuse `maximum_rate_hz`,
  already parsed from yaml, already threaded into the generated stream
  struct) and forwards whatever it returns onto the wire via the *existing*,
  already-tested `publish_stream`/`write_discrete`/`DiscreteState` machinery
  (§2.2) -- do not duplicate Stage 5's `PushState`/`ReliableWindow` work for
  this catalog; that machinery is for `Data`-capability `OutStream<Push,...>`
  and stays scoped there per §2.1 unless a concrete need for
  producer-callable `ReliableWindow` OutStream shows up (§2.1's `write_data`
  reachability gap is a separate, larger problem than best-effort
  `Queue<N,DropOldest>` delivery and should not be conflated with it -- see
  §5 open question 3).
- `Queue<N, DropOldest>` (or `DropNewest`/`Reject`) delivery for
  `direction: out` streams, driven by a new yaml `delivery:` field, reusing
  `DiscreteStoragePolicy`/`SelectedQueuePolicy` unchanged (they are already
  fully generic -- see the `TopicPublication<TopicT,void>` SFINAE-default
  precedent at `runtime.hpp:269-281`, which this should mirror almost
  exactly as `StreamPublication<StreamT,void>`).
- A way to signal "nothing new to publish this tick" so an idle poll doesn't
  spam duplicate/stale chunks. `Output<Endpoint,Publisher>::publish()`
  currently must return `Endpoint::Value` unconditionally
  (`endpoints.hpp:92-95`, enforced by `PublisherSignatureValid`,
  `contributions.hpp:426-438`). Changing this to `std::optional<Value>` is
  safe -- confirmed zero existing callers of `Output`/`Dispatch::publish`
  anywhere in the codebase (§2.2) -- but touches a shared, generic contract,
  so should be done deliberately and reviewed as its own step.
- Purely additive at every layer touched: `ByteRuntime`/`RuntimeContext`
  gain an optional, defaulted extra template parameter, not a required one
  -- the three existing Zephyr test fixtures
  (`tests/zephyr/remote_outbound_credit`, `remote_protocol`, `remote_link`)
  and the ENMT301-RoboCup production build must keep compiling and passing
  completely unmodified.

## 4. Sketch of the change (starting point for the implementing agent, not a final spec)

1. **`endpoints.hpp` / `contributions.hpp`**: change `Output<Endpoint,
   Publisher>::publish()` and `PublisherSignatureValid` /
   `Dispatch::publish<Stream>()` to return `std::optional<typename
   Endpoint::Value>`. Zero current callers -- safe, but re-check
   `binding_for_t`/`PublisherSignatureValid`'s two specializations
   (`has_endpoints_v<Owner>` true/false branches, `contributions.hpp:426-438`)
   both get updated consistently.
2. **`runtime.hpp`**: add `StreamPublication<StreamT, void>` mirroring
   `TopicPublication<TopicT, void>` (`runtime.hpp:269-281`) exactly --
   default `Watch<Latest, MultipleProducers>`, SFINAE-override via
   `typename StreamT::Publication` when present. Change `stream_state`
   (`runtime.hpp:911-916`) to use it instead of the hardcoded
   `Watch<Latest, MultipleProducers>`.
3. **`runtime.hpp`/`service.hpp`**: new `poll_output_streams_for<System>(TypeList<StreamTypes...>)`
   / `poll_output_streams<System>()`, mirroring `poll_reliable_out_streams_for`/
   `poll_reliable_out_streams` (`runtime.hpp:1551-1577`) structurally: for each
   `StreamTypes` in `RemoteStreamCatalog` with `!StreamTypes::input`, and only
   if `System::PollDispatch` is non-void, call
   `System::PollDispatch::template publish<typename StreamTypes::ContractType>()`
   (needs a `using ContractType = {cpp_name};` alias added to the
   `{cpp_name}Remote` codegen emission linking it back to the plain
   `Dispatch`-compatible contract-level type, since those are two distinct
   generated types today -- see §2.2), and if it returned a value, forward
   into `publish_stream<System, StreamTypes>(std::move(*value))`. Wire the
   call into `Service::run()`'s maintenance loop (`service.hpp:1636`, same
   line `poll_reliable_out_streams` was added on) -- respect rate limiting
   via each stream's already-parsed `maximum_rate_hz`, not every tick.
4. **`runtime_context.hpp`**: add a defaulted `PollDispatchT = void` template
   parameter to `RuntimeContext` and `ByteRuntime`
   (`runtime_context.hpp:53`, `:81`), exposed as `System::PollDispatch`.
5. **`application.hpp`**: `RemoteSynthesis` (`application.hpp:397-420`)
   passes its already-local `Dispatch` as the new parameter:
   `Runtime = remote::ByteRuntime<Application, Architecture, Dependencies, Dispatch>;`.
6. **`compiler.py`**: parse a new optional `delivery:` field on
   `direction: out` stream declarations (`latest` default, `queue<N,drop-oldest
   |drop-newest|reject>`); emit `using Publication = solar::remote::Watch<...>;`
   and `using ContractType = {cpp_name};` on the `{cpp_name}Remote` struct
   (compiler.py ~2333-2346).

## 5. Open questions for the implementing agent

All four resolved during implementation -- see §7 for the landed shape.

1. **Rate limiting -- resolved: honored.** `maximum_rate_hz` gates each
   stream via a new per-stream `PollScheduleState.next_poll` clock
   (`runtime.hpp`, `poll_schedule_state`/`poll_output_streams_for`), the
   `SubscriptionSlot.next_delivery` analogue this question anticipated.
2. **`MultipleProducers` correctness -- resolved: `SingleProducer`, label
   only.** Investigation before implementing found neither
   `SingleProducer` nor `MultipleProducers` is read by any storage or locking
   path today (`DiscreteStoragePolicy` only inspects the `Queue<...>` policy
   slot; the lock is an unconditional `kernel::SpinLock` either way) --
   `StreamPublication<StreamT,void>`'s default is `Watch<Latest,
   SingleProducer>` purely to document intent honestly. A real
   cheaper-lock-discipline optimization for the single-producer case remains
   unbuilt and was deliberately not attempted without profiling evidence it's
   needed.
3. **`ReliableWindow<Count>` reachability -- resolved: yes, in this same
   pass, deliberately expanding scope beyond this document's original
   sketch.** The concern above (that this "almost certainly means solving
   §2.1's `write_data`/`RuntimeContext` reachability problem instead") did
   not hold: §2.1's reachability problem is specifically about *service* code
   needing to name `RuntimeContext`, and the poll scheduler's enqueue call
   (`enqueue_reliable_stream<System,StreamT>`, `runtime.hpp`) always runs
   *runtime-side*, on the same maintenance thread `poll_reliable_out_streams`
   already ran on -- `System` was already nameable there. Stage 5's credit
   machinery (`PushState<T>`, `deliver_reliable_out_stream`,
   `dispatch_out_stream_credit`) turned out to already be generic over any
   `T` with `::Capabilities`/`::Value`, not actually `DataDescriptor`-specific
   -- confirmed by `dispatch_out_stream_credit` already being called with
   `StreamTypes{}` before this pass, a no-op only because no Stream declared
   a push `Capabilities` yet. Only `advance_reliable_out_stream` and
   `receive_out_stream_credit` needed a small `if constexpr (Data<T>) ...
   else ...` branch to select the right subscription-slot numbering space;
   the §2.1 problem remains open and unrelated -- see
   `docs/development-docs/todo/remote-event-driven-publish.md` for that
   (service/ISR-initiated *push*, not scheduler pull).
4. **Verification bar -- resolved: relaxed to localized, by explicit user
   decision.** Not the originally-specified full bar (all three prior Zephyr
   suites plus a real Teensy rebuild) -- verified instead with solar's full
   host ctest (78/78 passing) plus the specific Zephyr native-sim suites that
   touch this code: the new `tests/zephyr/remote_output_stream_scheduler`
   fixture (best-effort rate-limited delivery and reliable-window credit
   gating, both exercised with zero manual `publish()` calls -- genuinely
   producer-paced), `remote_outbound_credit`, and `remote_protocol`, run via
   the ENMT301-RoboCup native-sim Docker container (native_sim requires
   Linux). `remote_link` and a real Teensy rebuild were explicitly skipped as
   out of scope for this pass -- no ENMT301-RoboCup firmware/schema files
   were touched.

## 6. What Stage 6 needs once this lands

`poll_output_streams` has landed and is verified -- Stage 6 is unblocked.
Resuming `remote-arrays-and-chunked-streaming.md` Stage 6 means: add `delivery:
queue<8,drop-oldest>` to a new `navigation.lidar.points` stream declaration
in `navigation.solar.yaml` (replacing `navigation.lidar.scan`'s `data:`
entry per that document's own Stage 6 wording), give `Navigation` a chunking
state machine and `Output<&next_lidar_chunk>` binding in its `Endpoints`,
build the Python async-iterator reassembly facade (§6.4 of the parent
document), the synthetic reliable-mode confidence test (§10 Stage 6, which
does *not* depend on this document at all -- it can be built directly on
Stage 5's already-working `OutStream<Push,ReliableWindow<Count>>` fixture
independently of everything above), and the soak test.

## 7. What landed (2026-08-07)

Implemented and verified in the `solar` repo (uncommitted, per this
project's convention of leaving work uncommitted until explicitly asked).
This section is the concrete map from the §4 sketch to what's actually in
the tree, so a later reader doesn't have to diff it out by hand.

**Scope delivered:** the full §4 sketch, plus two deliberate expansions
agreed with the user before implementation -- `ReliableWindow<Count>`
reachability through the scheduler (originally scoped out, see §5 item 3),
and `SingleProducer` as `StreamPublication`'s honest default (§5 item 2).

**Core scheduler:**

- `endpoint::Output<Endpoint,Publisher>::publish()`
  (`include/solar/system/endpoints.hpp`) and `Dispatch::publish<Stream>()`
  (`include/solar/system/contributions.hpp`) now return
  `std::optional<Value>` -- `nullopt` means nothing to publish this tick.
  `PublisherSignatureValid`'s two specializations updated to match.
- `RuntimeContext<ArchitectureT, PollDispatchT = void>` and
  `ByteRuntime<..., PollDispatchT = void>`
  (`include/solar/remote/runtime_context.hpp`) gained a defaulted extra
  parameter exposing `System::RemotePollDispatch` -- purely additive, every
  pre-existing instantiation still compiles unchanged.
- `RemoteSynthesis` (`include/solar/application.hpp:418`) threads its
  already-local `Dispatch` into `ByteRuntime` as that new parameter.
- `poll_output_streams`/`poll_output_streams_for`
  (`include/solar/remote/runtime.hpp`) walk `RemoteStreamCatalog`, skip
  `direction: in` streams, rate-limit via `PollScheduleState.next_poll`
  against `maximum_rate_hz`, call
  `System::RemotePollDispatch::publish<StreamT::ContractType>()`, and
  forward a returned value into either `publish_stream` (best-effort) or the
  new `enqueue_reliable_stream` (reliable). Wired into `Service::run()`'s
  maintenance loop (`include/solar/remote/service.hpp:1636`).
- A forward declaration (`template <typename System> void
  poll_output_streams();`) was needed in `include/solar/remote/facility.hpp`,
  mirroring the existing `poll_reliable_out_streams` one -- this is the
  "facility.hpp seam" this document's own dependency note anticipated reusing,
  and a real GCC-only compile error (clang accepted the code without it) that
  only surfaced once the Zephyr native-sim build ran.

**Reliable-window-on-Streams (the scope expansion):**
`advance_reliable_out_stream<System,T>` and
`receive_out_stream_credit<System,LinkT,LinkIndex,T>` (`runtime.hpp`) each
gained a small `if constexpr (Data<T>) ... else ...` branch to select
`data_stream_subscription_slot` vs `stream_subscription_slot`; everything
else (`PushState<T>`, `deliver_reliable_out_stream`,
`dispatch_out_stream_credit`) was already generic and needed no change.
`poll_reliable_out_streams` now also drains `RemoteStreamCatalog` entries.

**Codegen (`tools/solar_codegen/compiler.py`):** new yaml `delivery:` field
on `direction: out` streams (`latest` default / `queue<N,overflow>` /
`reliable<N>`); `{cpp_name}StreamRemote` is now always `template <typename
Endpoints>`-templated (previously only `direction: in` streams were -- this
was *why* no producer path existed at all, the generated type had nothing to
call through) and emits a real `Capabilities<OutStream<Push,...>>` plus
`using ContractType = {cpp_name};` instead of the old, permanently-empty
`Capabilities<>`.

**Bug found and fixed along the way, not in the original scope:**
`manifest.hpp`'s `emit_stream_capability` unconditionally emitted the
generic placeholder `StreamPublication` capability record for every
`direction: out` stream regardless of what `Capabilities` actually declared
-- so even after codegen started emitting real policies, the wire manifest
would have kept lying about them. Fixed to walk real `Capabilities::Entries`
in either direction, falling back to the placeholder only when genuinely
empty.

**Tests:**

- `tests/host/check_codegen.py`:
  `test_output_stream_delivery_policy_end_to_end` and
  `test_output_stream_delivery_rejected_on_input_direction`.
- `tests/zephyr/remote_output_stream_scheduler/` (new fixture): best-effort
  rate-limited delivery and reliable-window credit gating, both driven
  entirely by the scheduler pulling from a `PollDispatch` stand-in -- no
  manual `publish()`/`write_data` call anywhere in either test, unlike
  `remote_outbound_credit`'s hand-triggered style.
- Three hand-written host fixtures (`tests/host/generated_application.cpp`,
  `system_dispatch.cpp`, `remote_server.cpp`) updated for the
  `std::optional<Value>`-returning `Output`/`Dispatch::publish` signature.

**Verified:** solar host ctest 78/78 passing; `remote_output_stream_scheduler`
(new), `remote_outbound_credit`, `remote_protocol` all passing in native-sim
via the ENMT301-RoboCup Docker container (7/7 test cases). Per §5 item 4,
`remote_link` and a real Teensy rebuild were not run -- explicit,
user-agreed scope reduction, and no ENMT301-RoboCup files were touched by
this pass.

**Deadline correction (2026-09-15):** the producer cadence is now part of
`Service::run()`'s existing event-or-deadline wait calculation. An active
output stream requests a wake at its `PollScheduleState.next_poll` deadline,
so a 200 Hz stream is no longer limited by the 50 ms maintenance timeout.
The reactor still wakes immediately for incoming link/publication events; no
new polling thread was added. Output streams with no active session
subscription are dormant (including their stateful `Publisher`) and request
no extra wake-ups. A focused scheduler test covers both the dormant case and
the high-rate deadline case.

**Not done, deliberately out of scope:** genuine service/ISR-initiated
*push* (as opposed to this scheduler's pull) -- see
`docs/development-docs/todo/remote-event-driven-publish.md`.
