# Remote Arrays And Chunked Streaming

Date: 2026-08-06

Status: Stages 1-6 implemented and verified (2026-08-07). Stage 6's
producer-path gap was split into `remote-output-stream-scheduler.md`
(implemented separately, see that document's §7) before Stage 6 itself
resumed and landed: `navigation.lidar.points` is a real, schema-declared,
chunked, best-effort `direction: out` stream in ENMT301-RoboCup
(`firmware/schemas/navigation.solar.yaml`, replacing the old
`navigation.lidar.scan` Data endpoint), the Python `solar_remote.chunked`
reassembly facade is built and tested, a synthetic `ReliableWindow` chunked
stream confidence fixture and a best-effort backpressure/degrade fixture
both pass in native-sim
(`tests/zephyr/remote_chunked_reliable_stream`,
`tests/zephyr/remote_chunked_best_effort_stream`), and the real ENMT301
Teensy 4.0 firmware rebuilds and shipment-verifies cleanly. Stage 7 (unify
`OutStream`/`InStream` onto one primitive) was explicitly descoped after
finding its two premised production consumers (log bridge, metrics export)
don't exist in this codebase, and Tunnel's InStream drive input was judged
not redundant with OutStream -- see conversation record, not re-derived
here.

Depends on:

- `remote-metadata-and-manifest-v2.md` (manifest record format, `ValueKind`,
  the reserved `referenced_type_id` extension point)
- `remote-host-extension.md`
- `../concepts/capacity-and-backpressure.md`

## 1. Purpose

Solar Remote message fields today are limited to scalars, enums, and two
flavors of opaque bounded blob (`BoundedText<N>`, `BoundedBytes<N>`). There is
no field type for a bounded array of scalars, and no field type for a bounded
array of records (a fixed-shape struct repeated N times). Anything array- or
record-array-shaped has to be hand-packed into a `BoundedBytes<N>` blob by the
firmware author and hand-unpacked by every client, with the packing format
agreed out of band and invisible to the manifest.

This was done once already, for `navigation.lidar.scan`
(`firmware/schemas/navigation.solar.yaml` in ENMT301-RoboCup): a `LidarScan`
point list packed as 5-byte little-endian records inside `bytes<2500>`, with
`tools/lidar_probe.py` hardcoding a matching `struct.unpack("<HHB", ...)`. It
works, but the manifest has no idea what's inside that blob, no client can be
generated for it, and the whole logical scan has to be fully assembled in a
fixed-size buffer on both ends before anything can be sent or read -- which is
exactly the bug that made `navigation.lidar.scan` silently time out until
`CONFIG_SOLAR_REMOTE_RESPONSE_CACHE_BYTES` was raised by hand.

This document proposes:

1. `Array<T, Capacity>` and a new bounded `Record` schema shape, as first-class
   field types with full manifest visibility -- so a lidar scan is
   `Array<LidarPoint, 500>`, not `bytes<2500>`.
2. A chunked delivery convention for `OutStream` so a logically large or
   open-ended array can be produced and consumed a chunk at a time, without
   ever holding the complete array in RAM on either end.
3. A credit-based reliable mode for that same chunked delivery, for the case
   where a transfer must arrive complete and in order.
4. Migration of Solar's existing `Stream` users (log bridge, metrics, tunnel
   drive input) onto this one unified mechanism, so there is a single stream
   concept going forward rather than two.

## 2. Goals

- One field-type extension (`Array<T, Capacity>`, `Record`) usable from `Data`,
  `Action`, and `Stream` schemas alike, fully described in the manifest so a
  generated or dynamic client can decode it without out-of-band knowledge.
- A chunked-array `OutStream` mode where the firmware side never buffers more
  than one chunk's worth of elements, and the station side can consume chunks
  as an async sequence rather than waiting for one giant assembled value.
- Reuse of Solar Remote's existing delivery-policy vocabulary
  (`Latest`, `Queue<Depth, Overflow>`, `ReliableWindow<Count>`,
  `DeliveryKind`) rather than inventing a second one. Extend it only where the
  direction it's missing (`ReliableWindow` today only governs `InStream`).
- A best-effort mode (drop under backpressure, consumer always gets the
  newest data) and a reliable mode (bounded retry within one connection,
  never silently drops), selectable per stream the same way delivery policy
  is selected today.
- One unified `Stream` primitive. Existing `OutStream`/`InStream` users (log
  bridge, metrics export, tunnel continuous drive input) keep working, moved
  onto the same underlying machinery rather than living beside a second,
  parallel concept.
- No dynamic allocation, no unbounded storage, no new pointer-bearing manifest
  state -- consistent with every existing Remote design constraint.

## 3. Non-Goals

- Station-to-device (`InStream`) chunked array transfer. The design leaves
  room for it (the envelope and manifest shapes are direction-agnostic) but
  nothing here is built or tested in that direction. Firmware/config/map
  upload is a real future need, not a v1 deliverable.
- Cross-reconnect resume. Reliable mode guarantees complete, in-order delivery
  within one continuous connection. A dropped link aborts the transfer; the
  consumer re-requests from the start. No persisted offset/checkpoint state.
- Arrays of arrays, or `Record` fields that themselves contain an `Array` or
  another `Record`. `Record` members are restricted to fixed-width scalars,
  enums, and each other's absence of recursion -- see §5.1.
- `BoundedText<N>`/`BoundedBytes<N>` array elements (variable-length element
  size defeats the fixed-stride element layout this design relies on for both
  the packed codec and the chunk-capacity accounting). An element containing
  text is out of scope; a whole chunk's raw bytes are still `BoundedBytes<N>`
  if genuinely needed.
- Wire compatibility with any currently-running firmware or station build.
  Confirmed acceptable: this is an in-house project, firmware and station are
  always rebuilt and reflashed together.
- Changing how `Data`/`Action` fragmentation works today. `Array`/`Record`
  fields become usable there too (a `Data` response can contain a bounded
  array), transported by the existing whole-message fragment/reassembly path,
  capped by `CONFIG_SOLAR_REMOTE_MAX_MESSAGE_BYTES` exactly as today. Chunked,
  never-fully-buffered delivery is a `Stream`-only capability in this design
  -- see §7 for why that split is deliberate.

## 4. Current State (what this design reuses unchanged)

Investigating before proposing anything turned up more existing machinery than
expected. Worth stating plainly so the new pieces read as additions, not a
rewrite:

- **Message fragmentation already works.** `service.hpp`'s `fragment_id` /
  `fragment_index` / `fragment_count` envelope fields and `ReassemblySlot`
  pool are real, tested, and unaffected by this design. Chunked streaming does
  not replace this -- an individual chunk that happens to exceed one frame
  still rides on it.
- **`OutStream<Push, Policies...>` already supports queued, batched delivery**,
  not just latest-value overwrite (`runtime.hpp`: `Queue<Depth, Overflow>` and
  `Batch<Count>` are valid `OutStream` policies today, diagnosed against
  double-declaration). This is exactly the shape a best-effort chunk producer
  needs: push a chunk, let the existing queue/overflow policy decide what
  happens if the consumer falls behind.
- **The delivery-policy vocabulary already distinguishes latest/queued/reliable
  delivery** (`declaration.hpp`: `Latest`, `Queue<Depth, Overflow>` with
  `DropOldest`/`DropNewest`/`Reject`, `ReliableWindow<Count>`;
  `manifest.hpp::DeliveryKind`: `Latest`, `QueueDropOldest`, `QueueDropNewest`,
  `QueueReject`, `Reliable`). This is precisely the "best-effort vs reliable"
  choice this design needs -- it does not need a new policy language, only
  `ReliableWindow` wired up for the `OutStream` direction (today it only
  governs `InStream`; see §6.3).
- **The manifest format was deliberately built to add this later.** The v2
  design's Non-Goals explicitly deferred "nested object fields" and "bounded
  arrays or maps," and reserved `referenced_type_id` on the `Field` record for
  exactly that ("nonzero for enum and future referenced-schema fields"). This
  design is that deferred work, landing on the extension point that was left
  for it rather than needing a manifest format change.
- **`solar::BoundedVector<Value, Capacity>`** (`core/bounded.hpp`) already
  exists as a fixed-storage, bounded-length array type with `.storage`/`.size`
  -- the same shape `BoundedBytes<N>` has, generalized over element type. It
  is not yet wired into `supported_scalar_v`, `codec.hpp`, or the manifest.
  `Array<T, Capacity>` in this design is `BoundedVector<T, Capacity>` gaining
  that wiring, not a new type.

## 5. Array And Record Field Types

### 5.1 `Record`: a bounded, non-recursive element schema

A new `SchemaShape::Record` variant, declared the same way `Object` schemas
are today, but restricted to keep every element a fixed encoded width:

```cpp
template <> struct solar::remote::Schema<devices::ld06::LidarPoint>
{
    static constexpr SchemaDescriptor descriptor{
        .id = TypeId{0x1301},
        .name = "lidar.LidarPoint",
        .description = "One LD06 range sample",
    };
    static constexpr SchemaShape shape = SchemaShape::Record;

    using Fields = remote::Fields<
        Field<1, "angle_centidegrees", &devices::ld06::LidarPoint::angle_centidegrees>,
        Field<2, "distance_mm", &devices::ld06::LidarPoint::distance_mm>,
        Field<3, "confidence", &devices::ld06::LidarPoint::confidence>>;

    static constexpr std::size_t encoded_size = 5; // u16 + u16 + u8, packed
};
```

Rules, checked at compile time the same way `Object` schemas are checked
today (`supported_fields`-equivalent, new diagnostic tokens in §10):

- every member must satisfy today's `supported_scalar_v` **minus**
  `BoundedText`/`BoundedBytes` -- so: `bool`, integral, floating-point, enum
  only.
- no member may be `std::optional<T>`. A `Record` element has no presence
  bitmap; it is always all its fields, every time, so array elements have a
  fixed stride.
- no member may itself be `Array<T, N>` or another `Record` (no recursion, no
  arrays of arrays).
- `encoded_size` is derived, not authored, the same way `max_encoded_size` is
  derived for `Object` schemas today.

This intentionally does not reuse `Object` shape as-is: `Object` permits
optional fields and `BoundedText`/`BoundedBytes` members, both of which make
per-element size variable. A variable-stride element defeats fixed
chunk-capacity accounting (`Capacity` in `Array<T, Capacity>` would stop
meaning "this many elements fit" and start meaning "this many elements fit in
the worst case," which is a worse bound for the same declared buffer).

### 5.2 `Array<T, Capacity>`

```cpp
template <typename T, std::size_t Capacity> using Array = solar::BoundedVector<T, Capacity>;
```

Valid `T`: any `supported_scalar_v` scalar/enum, or any type with
`SchemaShape::Record`. Usable as a field member the same way
`BoundedBytes<N>` is today:

```cpp
using Fields = remote::Fields<
    Field<1, "points", &LidarScanChunk::points>,       // Array<LidarPoint, 64>
    Field<2, "sequence", &LidarScanChunk::sequence>,
    Field<3, "final", &LidarScanChunk::final>>;
```

`supported_scalar_v` gains an `IsArray<T>` branch exactly like
`IsBoundedText`/`IsBoundedBytes` today, resolving the element type and
checking it recursively (one level only -- element-of-element is rejected by
§5.1's no-recursion rule, not by this check, since an `Array`-of-`Array`
would fail `Record`'s "no member may itself be `Array`" rule if someone tried
to sneak it in via a `Record` element, and directly, `Array<Array<...>, ...>`
is rejected because `Array`'s element-type check does not accept `IsArray`
itself).

### 5.3 Codec

CBOR encoding for `Array<T, Capacity>`: a definite-length CBOR array
(`zcbor_list_start_encode(state, value.size)` / `..._end_encode`), each
element encoded with the existing scalar `encode_value` if `T` is a scalar, or
a nested CBOR map using the same field-by-field encoding `Object` schemas use
today if `T` is a `Record`. Decode is symmetric, rejecting anything with
`size > Capacity` the same way `BoundedBytes` decode rejects an oversized blob
today (`codec.hpp:159`).

`zcbor`'s state-depth budget (`ZCBOR_STATE_E(state, 2, ...)` today) needs
raising by one level for a `Record` element's inner map; this is a compile-time
constant, not a runtime cost, and is exercised by a compile-fail/codec test
per §10.

### 5.4 Packed codec

Solar's packed (non-CBOR) codec already rejects `BoundedText`/`BoundedBytes`
(`declaration.hpp:170`, "Packed schemas continue to reject optional fields").
`Array<T, Capacity>` is rejected there too, for the same reason variable
runtime length doesn't fit a fixed packed offset table. `Record`-typed plain
values (not inside an `Array`) are fine under packed, since they're
fixed-width and non-recursive by construction.

### 5.5 Manifest

New `ValueKind::Array` value. The existing `Field` record (`manifest-v2`
§8.2) is **not** restructured -- its byte layout for every existing
`value_kind` is untouched. A field with `value_kind == Array` gets one new,
immediately-following, skippable record:

```text
kind 10  ArrayElement
owner_type_id         u32   -- the Array field's owner schema
field_id               u16   -- the Array field's ID
element_value_kind     u8    -- Boolean/Unsigned/Signed/Float/Enumeration/Record
element_referenced_type_id  u32  -- nonzero for Enumeration or Record elements
element_bit_width      u16   -- scalar/bool element width; zero for Record
maximum_length          u32   -- Capacity (element count, not bytes)
reserved                u16
```

This is exactly the pattern manifest-v2 built for: an unknown/optional record
kind that old readers skip and new readers use, with no change to the
already-shipped `Field` record's meaning. `Record` schemas themselves reuse
the existing `Schema` (kind 1) and `Field` (kind 2) records unchanged --
`shape = Record` is one more value alongside `Object`/`Enumeration` in the
already-existing `shape` byte, and a `Record`'s own fields are ordinary
`Field` records the same as an `Object`'s.

## 6. Chunked Delivery For `OutStream`

### 6.1 The chunk envelope is an ordinary `Record`/`Object`, not new protocol

No new frame-level concept is introduced. A chunked array stream is an
`OutStream<Push, Policies...>` whose published `Value` looks like:

```cpp
struct LidarScanChunk
{
    std::uint64_t generation{};        // per-scan id -- same counter Runtime already tracks
    std::uint32_t chunk_index{};       // 0-based index of this chunk within the scan
    bool final{};                      // true on the last chunk of this scan
    Array<LidarPoint, 64> points{};
};
```

**Resolved:** the scan, not the point, is the consumer-facing unit --
`Navigation`'s own consumption is per-scan, and per-scan is the more natural
shape for anything downstream of it too. Chunking still happens on the wire,
for reasons that hold independently of link congestion:

- firmware buffer footprint drops from "one whole scan" (2.3+ KB today,
  `ScanCapacity`-sized) to "one chunk" (64 x 5 = 320 bytes), a real MCU RAM
  win even with zero congestion, and the mechanism this document exists to
  provide;
- lower latency to first data -- the station can start rendering points
  before the whole revolution has arrived;
- wire safety stops being coupled to `ScanCapacity` -- a future denser
  lidar producing 2000+ points/scan cannot reproduce the
  `CONFIG_SOLAR_REMOTE_RESPONSE_CACHE_BYTES` bug that motivated this design,
  because no single message ever has to hold a whole scan.

`generation` reuses the counter `Runtime`/`LidarStatus` already expose (see
`firmware/include/services/navigation/navigation.hpp` in ENMT301-RoboCup) --
not a new concept, the same value that already means "this is scan N." Chunk
boundaries are purely a wire/firmware-memory concern and never surface past
the station SDK: §6.4's async facade reassembles by `generation` and only
hands the application a complete scan once `final` arrives. Station RAM is
not the constrained resource, so buffering one scan's worth of points there
(a few hundred elements) is free, and this is where genuinely giving the
application "one full scan" belongs -- at the ergonomic layer, not the wire
layer.

Publishing one `LidarScanChunk` per 64 points instead of one `LidarScan` per
~455-point scan is the entire wire-level mechanism. Nothing about
`Array`/`Record` from §5 required this -- it's what happens when a producer
calls `publish()` once per bounded batch instead of once per logical scan.

### 6.2 Best-effort mode

`OutStream<Push, Queue<Depth, DropOldest>>` (or `DropNewest`, per stream) --
already-implemented policy, unchanged. `DeliveryKind::QueueDropOldest`/
`QueueDropNewest` in the manifest, unchanged. This is what `navigation.lidar`
should become: if the station falls behind, older chunks are dropped and the
station-side reassembly in §6.1 sees a `chunk_index` gap, discards that
partial scan (it can never be completed), and moves on to the next
`generation` -- exactly like today's `generation` counter already signals a
skipped scan, just visible one layer earlier. No new machinery -- this
section exists to confirm the existing policy is sufficient and record that
decision, not to propose anything new.

### 6.3 Reliable mode: extending `InStream`'s existing credit mechanism to `OutStream`

New work, but not a new mechanism -- Solar Remote already has a working
credit/flow-control primitive for the *other* direction, and this reuses it
verbatim with the roles reversed rather than inventing acks.

**What already exists (`InStream`, station -> device):** the protocol already
has a `protocol::Kind::Credit` message carrying a `CreditGrant{ credits }`
(`protocol.hpp:232`). The *device* grants credit
(`send_in_stream_credit`, `runtime.hpp:1912`), the *station* spends one credit
per publish, and the device replenishes credit as it drains its inbound queue
(`return_in_stream_credit`, `runtime.hpp:2521`). If the station sends without
credit, that's `ErrorCode::CreditViolation` (`runtime.hpp:3500`) -- a hard
protocol error, because the station is expected to obey grants it received.

**Proposed for `OutStream` reliable mode (device -> station), mirrored:**

- `OutStream<Push, ReliableWindow<Count>>` bounds the producer to at most
  `Count` un-acknowledged chunks in flight -- `Count` is the credit window
  size, same meaning `ReliableWindow<Count>` already has for `InStream`.
- The *station* grants credit (same `Kind::Credit`/`CreditGrant` wire message,
  same `OutputLane` routing -- zero new frame types) as it consumes and frees
  buffered chunks, mirroring `return_in_stream_credit`. A new
  `send_out_stream_credit` on the station SDK side mirrors
  `send_in_stream_credit` structurally.
- The *device* spends one credit per chunk published, mirroring the spend
  side of `runtime.hpp:3519`. Credit granularity is "one chunk," matching
  `CreditGrant.credits`' existing unit -- no new accounting shape.
- **Behavioral difference from `InStream`, and the reason this isn't a
  literal copy-paste:** hitting zero credit is not a protocol error here.
  `InStream`'s station is a synchronous caller that can simply not call
  `publish()` without credit; `OutStream`'s device is an autonomous producer
  (UART interrupt -> work queue) that keeps producing regardless. So a
  publish attempt at zero credit queues locally in the same `Count`-sized
  buffer the window already implies, and is retried next work-queue pass
  rather than erroring immediately.
- If that local retry buffer stays full past a bounded timeout (consumer
  genuinely gone, not just briefly slow), the stream aborts:
  `DeliveryKind::Reliable`'s contract is "complete and in order or the
  consumer is told it broke," never "silently incomplete." This is the §3
  Non-Goal boundary in practice -- no cross-reconnect resume means "aborted"
  is a real, surfaced outcome, not hidden retry-forever.
- Manifest: `Capability`'s existing `reliable_window u16` field
  (`manifest-v2` §8.5) already exists for exactly this and needs no format
  change -- it currently has no `OutStream` producer to populate it from;
  this design is that producer.

### 6.4 Station SDK surface

Per §6.1, the SDK reassembles by `generation`/`chunk_index` and hands the
application a complete scan, not individual points or raw `LidarScanChunk`
records:

```python
async for scan in client.robot.streams["navigation.lidar.scan"]:
    scan.generation   # matches today's LidarScan.generation
    scan.points        # list[LidarPoint], complete -- reassembled from chunks
```

Best-effort streams surface a gap (a `generation` whose chunks never
completed before the next `generation` started) as an explicit event the
caller can observe or ignore -- the incomplete scan is discarded, matching
how a dropped/overwritten `Latest` value is invisible-by-default today, just
one layer up. Reliable streams never surface a gap; a broken reliable stream
raises instead.

This SDK layer is genuinely new code (there is no chunk-reassembly facade
today), but it sits entirely in `solar-remote`'s Python SDK, per
`remote-host-extension.md` §1's layering rule -- it does not touch
`solar-station` or Tunnel.

## 7. Why `Data`/`Action` Keep Whole-Message Fragmentation

`Array`/`Record` fields (§5) are usable from `Data` and `Action` schemas,
not only `Stream`. A `Data` response with an `Array<T, Capacity>` field is
still one request, one response, fully assembled on both ends, riding the
existing fragment/reassembly path, capped by `MAX_MESSAGE_BYTES` -- exactly
like every other `Data` response today, just with a typed array field instead
of an opaque blob.

True chunked, never-fully-buffered delivery (§6) is proposed as
`Stream`-only. Reasoning: `Data`'s request-response shape has no notion of
"more is coming" -- a response either is or isn't complete when it's sent.
Retrofitting partial/incremental responses onto `Data` would mean giving
request-response a second, streaming-shaped mode, which is a bigger change to
an endpoint kind whose whole contract today is "one request, one answer." A
genuinely large or open-ended array belongs on a `Stream`, which already has
an open-ended, ongoing-delivery contract. If a real need for chunked `Data`
responses shows up later, it is a separate document -- flagged in §12,
not designed here.

## 8. Unifying `OutStream`/`InStream` Onto One Primitive

The decision (confirmed) is to migrate existing `Stream` users -- the Solar
log bridge (`OutStream`), metrics export (`OutStream`), and Tunnel's
continuous drive input (`InStream`) -- onto the same machinery this design
adds, rather than leaving them on a separate, older code path.

Concretely: today's plain scalar `Fields<...>` `Value` types remain completely
valid `Stream` values -- `Array`/`Record` support is additive to what
`Fields` already accepts, not a replacement. "Unify" here means: one
`Stream<Value, Acquisition, Policies...>` declaration surface, one runtime
implementation, one manifest record shape, whether `Value` happens to contain
an `Array` field or not, and whether the direction is `Push`/`Poll`/`Loaned`
acquisition or plain consumption. The log bridge and metrics export don't
change behavior -- they change which underlying implementation they compile
against, verified by the existing log-bridge and metrics tests continuing to
pass unmodified in §11's staged migration.

This is real, non-trivial churn to already-working, tested paths for no
functional gain to those specific paths -- accepted deliberately, in exchange
for one stream concept in Solar going forward instead of two. Flagged here so
the cost is visible, not discovered mid-implementation.

## 9. Diagnostics

New stable tokens, following the existing `SOLAR_DIAGNOSTIC_REMOTE_*`
convention:

```text
SOLAR_DIAGNOSTIC_REMOTE_RECORD_OPTIONAL_FIELD       -- Record member is std::optional
SOLAR_DIAGNOSTIC_REMOTE_RECORD_VARIABLE_FIELD       -- Record member is BoundedText/BoundedBytes
SOLAR_DIAGNOSTIC_REMOTE_RECORD_RECURSIVE_FIELD      -- Record member is Array or Record
SOLAR_DIAGNOSTIC_REMOTE_ARRAY_UNSUPPORTED_ELEMENT   -- Array<T, N> with unsupported T
SOLAR_DIAGNOSTIC_REMOTE_ARRAY_ZERO_CAPACITY         -- Array<T, 0>
SOLAR_DIAGNOSTIC_REMOTE_PACKED_REJECTS_ARRAY        -- Array field in a packed-codec schema
SOLAR_DIAGNOSTIC_REMOTE_OUTBOUND_WINDOW_CEILING     -- OutStream ReliableWindow exceeds configured maximum
```

Compile-fail tests assert these tokens, matching every prior Remote design
doc's diagnostic-testing convention.

## 10. Implementation Sequence

### Stage 1: `Record` schema shape

- Add `SchemaShape::Record`, the no-optional/no-`BoundedText`/no-`BoundedBytes`/
  no-recursion field restriction, and `encoded_size` derivation.
- Compile-fail tests for every restriction in §5.1.
- No manifest, codec, or runtime change yet -- a `Record` type that compiles
  but isn't referenced by anything.

### Stage 2: `Array<T, Capacity>` field support

- `supported_scalar_v` gains the `Array` branch.
- `codec.hpp`: CBOR array encode/decode for scalar and `Record` elements.
- Packed codec: explicit rejection with the Stage-1-adjacent diagnostic.
- Codec round-trip tests: scalar element arrays, `Record` element arrays, at
  and past `Capacity`.

### Stage 3: Manifest

- `ValueKind::Array`, the new `ArrayElement` record (kind 10).
- Deterministic ordering/digest updates, canonical test vectors.
- Python manifest parser: recognize `ArrayElement`, produce a typed model.

### Stage 4: `Data`/`Action` array fields end-to-end

- One real schema (candidate: rebuild `navigation.lidar.scan`'s `LidarScan`
  as `Array<LidarPoint, 500>` instead of `bytes<2500>`) exercised as a normal
  `Data` response, over the existing fragmentation path, on real hardware.
- Generated Python client produces a typed `list[LidarPoint]`, replacing
  `lidar_probe.py`'s hand-rolled `struct.unpack`.

### Stage 5: Outbound `ReliableWindow`

- Extend the credit/window accounting `InStream` already has to the
  `OutStream` direction.
- `SOLAR_DIAGNOSTIC_REMOTE_OUTBOUND_WINDOW_CEILING`, matching the existing
  inbound one.
- Native-sim test: full window, timeout-triggered abort, confirmed no silent
  drop.

### Stage 6: Chunked `OutStream` + station SDK facade

- `navigation.lidar.scan` (or its Stage-4 replacement) becomes a chunked
  `navigation.lidar.points` stream, `Queue<N, DropOldest>` best-effort.
- Python async-iterator facade (§6.4), gap surfacing for best-effort.
- One reliable-mode stream, built as a synthetic test fixture (per the
  "no concrete client yet" decision in §1) -- not a production endpoint,
  a confidence test that the mode actually works end to end.
- Hardware soak test matching the rigor already applied to the LD06 driver
  fixes: extended run, verify zero unexplained gaps in reliable mode, verify
  best-effort mode degrades to dropped-not-stalled under induced backpressure.

### Stage 7: Unify existing `Stream` users

- Migrate the log bridge, metrics export, and Tunnel drive input onto the
  unified implementation from Stage 6, with no behavior change.
- Existing log-bridge and metrics tests pass unmodified; Tunnel's drive-input
  integration tests pass unmodified.

Each stage lands with host tests, Zephyr tests, native-simulation coverage
where applicable, and documentation updates, matching every prior Remote
design doc's delivery discipline.

## 11. Rejected Alternatives

### 11.1 A second, parallel "big data" protocol

Introducing an entirely separate binary bulk-transfer mechanism alongside
Remote (e.g. a raw byte-stream side-channel) was considered and rejected.
Solar Remote already has schema, manifest, fragmentation, and delivery-policy
machinery that covers nearly everything this need requires (§4); building
a second mechanism would duplicate all of it for no benefit and give clients
two things to implement instead of one.

### 11.2 Fully-buffered arrays only (no true streaming)

Just raising `Array<T, Capacity>` to arbitrary capacity and relying on
existing whole-message fragmentation (i.e., §6 never happens, only §5
and §7) was considered. Rejected: this is exactly the shape of bug that
motivated this document (`navigation.lidar.scan`'s response-cache overflow) --
it raises the ceiling without removing it, and the ceiling is a real MCU RAM
constraint, not an arbitrary tuning knob. Confirmed with the user as
insufficient before this design was started.

### 11.3 Recursive `Record`/`Array` nesting in v1

Allowing a `Record` field to itself be an `Array` or another `Record`, or an
`Array` of `Array`, was considered for generality. Rejected for v1: no current
use case needs it, and it meaningfully complicates the fixed-stride element
guarantee §5.1 relies on (variable-depth nesting means variable-depth
size computation, which is a real cost on firmware doing this in a work-queue
handler). Revisit if a concrete need appears -- the manifest's `ArrayElement`
record (kind 10) does not preclude adding this later; it would need a new
element-value-kind value and a size-computation rule, not a format change.

## 12. Decisions And Remaining Open Questions

The four items originally raised here are resolved as follows.

1. **`navigation.lidar` reshape -- resolved.** The scan, not the point, is
   the consumer-facing unit (§6.1). The chunk envelope carries `generation`
   (the existing per-revolution counter, reused unchanged) and `chunk_index`;
   `final` marks scan completion. Chunking is a wire/firmware-memory
   optimization only, invisible past the station SDK, which reassembles a
   complete scan before handing it to the application (§6.4). This holds
   independent of link congestion -- see §6.1 for the standing reasons
   (bounded firmware buffer, lower latency-to-first-data, decoupling from
   `ScanCapacity`).
2. **Outbound credit framing -- resolved.** No new framing. §6.3 reuses the
   existing `protocol::Kind::Credit`/`CreditGrant` message and the
   `send_in_stream_credit`/`return_in_stream_credit` accounting pattern
   verbatim, with station and device swapping which side grants vs. spends.
   Zero new wire messages; the only new code is the mirrored grant/spend
   functions and the local retry-buffer-then-abort behavior on the spending
   (device) side, which `InStream`'s synchronous station-side caller never
   needed.
3. **`Batch<Count>` interaction -- resolved.** Compose. A chunked stream may
   declare `OutStream<Push, ReliableWindow<Count>, Batch<N>>` (or
   `Queue<Depth, Overflow>` for best-effort); `Batch` coalesces multiple
   chunks into one frame exactly as it already does for any other `OutStream`
   today. No interaction beyond ordinary policy composition -- confirmed,
   not new work.
4. **`InStream` symmetry timing -- resolved: lock the shape, defer the
   policy.** `Array<T, Capacity>`, `Record`, and the
   `generation`/`chunk_index`/`final` envelope shape from §6.1 are
   direction-agnostic already and should be reused verbatim whenever
   station-to-device chunked transfer is built, rather than redesigned from
   scratch. What stays explicitly open, because it is genuinely
   use-case-dependent and guessing now risks designing against the wrong
   constraints: open/close lifecycle (`OnOpen`/`OnClose`, matching
   `InStreamFlags::ExplicitOpen` today), whether the device may refuse or
   backpressure a transfer at open time, cancellation, and whether a transfer
   is atomic-or-nothing (e.g. firmware image) or incrementally usable (e.g.
   map upload) -- a firmware upload and a map upload plausibly want different
   answers here. Revisit with a concrete first consumer in hand.
