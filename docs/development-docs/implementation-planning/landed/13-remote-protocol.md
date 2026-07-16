# Stage 13: Remote Protocol And Generation

Status: landed

Landed date: 2026-07-16

Implementation repository/branch: `/workspaces/solar`, `static_reform`

Relevant commits or change identifiers: uncommitted reform working tree

## 1. Objective

Stage 13 freezes Remote protocol v1 and lands the deterministic schema, codec,
framing, manifest, and host-tooling foundation required by the later runtime.
The stage deliberately owns no transport, session, application execution, or
canonical application state.

## 2. Specification Coverage

| Specification | Sections implemented | Notes |
| --- | --- | --- |
| `10-remote.md` | 5, 9-13, 20 declaration shape, 26 envelope metadata, 28, 33, 35-36, Stage 13 portions of 38-40 | Wire v1, typed catalogs, deterministic scalar/bounded text and byte schemas, packed records, emitted metadata, and host vectors are landed. Runtime dispatch and richer bounded collection/nested-schema support continue in Stage 14. |
| `00-design-conventions.md` | bounded ownership, Kconfig exclusion, Zephyr-first integration | All mutable buffers are caller owned; Remote remains optional. |
| `01-system-blueprint-and-binding.md` | catalog derivation and bound artifact emission | Default binding emits one manifest image; explicit multi-application binding controls emission directly. |
| `02-identity-contributions-and-catalogs.md` | typed identity domains and component contribution aliases | Schema, Data, Action, Topic, Stream, and Link catalogs retain owner/provenance facts. |

## 3. Public Surface Landed

The aggregate include is:

```cpp
#include <solar/remote.hpp>
```

An external value has an explicit schema and stable field IDs:

```cpp
struct Sample
{
    std::uint32_t sequence{};
    float gain{};
};

template <> struct solar::remote::Schema<Sample>
{
    static constexpr SchemaDescriptor descriptor{
        .id = TypeId{0x3001},
        .name = "app.Sample",
    };
    using Fields = remote::Fields<
        Field<1, &Sample::sequence>,
        Field<2, &Sample::gain>>;
    static constexpr std::size_t max_encoded_size = 24;
    static constexpr Codec codec = Codec::Cbor;
};
```

Data, Actions, Topics, Streams, Links, and Schemas contribute through their
independent aliases. The bound System exposes each effective Remote catalog.

The frozen protocol uses a 32-byte little-endian envelope, CRC32C integrity,
and COBS stream framing. C++ and Python expose the same envelope and incremental
decoder behavior. Structured payloads use deterministic zcbor encoding;
explicit Packed schemas use field-order little-endian encoding and never copy
native object representation.

`SOLAR_BIND_SYSTEM(System)` emits the pointer-free manifest image when Remote
generation is enabled. `SOLAR_BIND_SYSTEM_FOR` leaves emission explicit through
`SOLAR_REMOTE_EMIT_MANIFEST(System)` for multi-application/test translation
units.

The post-link generator produces:

- canonical `manifest.cbor`;
- reviewable `manifest.json`;
- `manifest.sha256`;
- Python constants;
- a C++ constants header.

## 4. Runtime Ownership

| Owner | Storage/resource | Capacity | Synchronization | Lifetime |
| --- | --- | --- | --- | --- |
| `manifest::Image<System>` | immutable pointer-free binary metadata | exact derived catalogs and Kconfig ceilings | none | image lifetime |
| CBOR/Packed caller | output/input span and decoded value | caller selected, bounded by Schema | caller owned | one call |
| frame caller | scratch, encoded, and decoded spans | caller selected, bounded by frame ceiling | caller owned | one call |
| `frame::StreamDecoder` | partial encoded and decoded arrays plus counters | template capacities | single-owner parser | decoder lifetime |
| generated host tooling | post-link files | exact manifest | build-system serialized | build artifact lifetime |

Stage 13 owns no heap, thread, stack, workqueue, timer, poll object, service,
transport, session, request slot, or canonical application value. Static
initialization is constant initialization only.

## 5. Compile-Time Behavior

Remote uses distinct typed 32-bit ID domains for Schema, Data, Action, Topic,
Stream, and Link identities. Field IDs are 16-bit and local to one Schema.
Binding rejects missing or duplicate schema identities, duplicate/unordered
field IDs, unsupported or unbounded fields, duplicate Data capabilities,
invalid Packed fields, Kconfig ceiling violations, and authored Remote
declarations when Remote is disabled.

Schema dependencies are derived from explicit Schema contributions and every
effective Data value, Action request/response/error, Topic value, and Stream
value. The emitted image is pointer free and deterministic under catalog-order
changes that preserve authored identities.

Strict and relaxed binding use the same wire and manifest types. This stage has
no relaxed-mode runtime lookup because it adds no runtime frontend.

## 6. Error And Availability Behavior

Codec and frame operations return `Result<T, remote::Error>` with focused
operation and reason fields. Errors distinguish disabled support, no space,
malformed input, unsupported kind/version, integrity failure, duplicate or
missing fields, invalid values, trailing data, and generation failure.

Remote-disabled codec entry points remain includeable and return
`NotSupported/Disabled`. Authored declarations while disabled are rejected at
binding. Frame decoding never exposes a payload before envelope, length, COBS,
and CRC checks succeed. Incremental decoding discards an overflowing frame and
resynchronizes at the next delimiter.

`solar::Status` has an explicit stable wire-code mapping; neither errno nor the
C++ enum representation is serialized.

## 7. Zephyr Integration

`CONFIG_SOLAR_REMOTE` selects Zephyr COBS, CRC, zcbor, and canonical zcbor
support. Software CRC32C is provided by `CONFIG_CRC`; `CONFIG_CRC32_C` is not
selected because that symbol is target-capability constrained in the pinned
Zephyr release.

Firmware uses Zephyr's streaming COBS and CRC32C implementations and zcbor for
CBOR. Host tests use a portable protocol core implementing the same frozen
wire contract. The manifest is emitted through a Zephyr iterable ROM section,
collected into `solar_remote_manifest_area`, and extracted from the final ELF.

The CMake post-link target verifies required Python modules and depends on the
final `zephyr.elf`; generated files live under the build directory. There are
no ISR or thread-context restrictions in this stage beyond caller ownership of
mutable buffers.

## 8. Files Changed

### Added

- `include/solar/remote/{catalog,contribution,declaration,frame,manifest,packed,types}.hpp`
- `tests/vectors/remote_protocol_v1.json`
- `tests/host/{remote_protocol.cpp,check_remote_host.py,check_remote_generation.py}`
- `tests/compile_fail/remote_*`
- `tests/zephyr/{remote_protocol,remote_disabled,remote_manifest}/`
- `tests/zephyr/check_remote_headers.py`
- `tools/remote/generate_manifest.py`
- `tools/remote/solar_remote/{__init__.py,protocol.py}`
- `tools/remote/requirements.txt`
- `zephyr/remote_manifest.ld`

### Reshaped

- `include/solar/remote.hpp`
- `include/solar/remote/{codec,protocol}.hpp`
- `include/solar/system/{binding,blueprint,system}.hpp`
- `include/solar/solar.hpp`
- `CMakeLists.txt` and `zephyr/Kconfig`

### Removed

- the prototype YAML registry, generated Remote headers, schema implementation,
  synchronous Remote service, and method/observable compatibility surface;
  Git history remains the archive.

## 9. Tests And Evidence

| Command | Platform/configuration | Result | What it proves |
| --- | --- | --- | --- |
| host configure/build plus `ctest` | GCC 13, C++23 | 57/57 pass | protocol vectors, Python parity, deterministic generation, generated C++/Python artifacts, and all host regressions |
| focused Remote host tests | C++ and Python | 10/10 pass | declarations, malformed inputs, disabled use, and generation failures |
| focused Remote Twister matrix | native_sim 64, relaxed/strict/disabled/manifest | 4/4 configurations, 6/6 cases, no warnings | zcbor, framing, status map, binding modes, and ELF manifest |
| `check_remote_headers.py` | enabled and disabled compile databases | 10/10 headers in each mode | public-header isolation and Kconfig exclusion |
| complete `west twister -T tests/zephyr ...` | native_sim 64 | 55/55 configurations, 220/220 cases, no warnings | all Stage 00-13 Zephyr regressions |
| `west build -d /tmp/solar-stage13-remote-teensy` | Teensy 4.0 | pass | target compile/link and automatic final-ELF generation |
| repeated generator invocation | native and Teensy ELF | byte-identical | deterministic CBOR, JSON, digest, Python, and C++ outputs |
| `git diff --check` | Solar tree | pass | no whitespace errors |

The Teensy Remote manifest image uses 44,144 bytes flash (2.10%) and 8,832
bytes RAM (3.37%) in the reported primary regions. The generated canonical
manifest artifact is 269 bytes for the host fixture before transport/runtime
metadata is introduced.

## 10. Implementation Decisions

### 10.1 Zephyr Primitives With Portable Host Parity

Problem: protocol vectors need host fuzzing while firmware must integrate with
Zephyr instead of maintaining private embedded primitives.

Decision: firmware calls Zephyr zcbor, COBS, and CRC; the host module implements
only the frozen protocol behavior needed for generation and interoperability.

Physical implementation: `remote/codec.hpp`, `remote/frame.hpp`, and
`tools/remote/solar_remote/protocol.py`.

### 10.2 Static Manifest Object And Iterable Section

Problem: inline constexpr manifest objects may be emitted as COMDAT and escape
Zephyr iterable-section collection.

Decision: emit one translation-unit-local constexpr object through the binding
macro and collect it with a dedicated iterable ROM linker section.

Physical implementation: `remote/manifest.hpp`, `system/binding.hpp`, and
`zephyr/remote_manifest.ld`.

### 10.3 Preferred Deterministic Floating Encoding

Problem: deterministic CBOR requires the shortest exact floating encoding.

Decision: encode exact half values as float16, otherwise exact float values as
float32, and use float64 only when required.

Physical implementation: `remote/codec.hpp`; the shared vector fixes 1.5 as
`f9 3e 00`.

## 11. Firmware And Host Impact

Firmware no longer includes generated Remote source headers or a YAML registry.
The bound application type is the only schema/endpoint authority, and final-ELF
artifacts are generated automatically. The Python protocol package imports the
same manifest identities and wire constants used by firmware tests.

No physical link or runtime service is migrated in this stage. The Stage 14
integration image will add an in-memory link first and a board-supported byte
stream link where available.

## 12. Known Limits And Deferred Work

- physical links, sessions, authorization, requests, acquisition, ingress,
  queues, backpressure, fragmentation/reassembly runtime, and subsystem
  adapters belong to Stage 14;
- initial schema fields currently cover scalar, enum, optional, bounded text,
  and bounded bytes. Bounded collections and nested registered values are
  completed with Stage 14's decoded-value pools and resource ceilings;
- Packed batch headers and timestamp deltas belong to the Stream runtime;
- full generated host clients and CLI operations depend on runtime endpoint
  semantics and therefore belong to Stage 14.

These deferrals do not change the frozen protocol envelope, IDs, deterministic
scalar encoding, manifest format, or ownership rules.

## 13. Documentation Handoff

Public documentation must explain explicit IDs, Schema specialization, CBOR
versus Packed selection, component contribution aliases, binding-time manifest
emission, generated artifact locations, wire-version policy, bounded buffer
ownership, and the distinction between declarations and Stage 14 runtime
exposure. The host and Zephyr Remote protocol fixtures are executable examples.

## 14. Closure Statement

Stage 13 is complete because the typed schema/catalog foundation, frozen wire,
deterministic codecs, framing, manifest extraction, host artifacts, shared
vectors, disabled behavior, target build, and complete regressions all pass
without introducing runtime ownership. Stage 14 Remote runtime and integration
is now unblocked.
