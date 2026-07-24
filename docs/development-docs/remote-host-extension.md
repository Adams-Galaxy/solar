# Remote Host Extension

Date: 2026-07-23

Status: temporary accepted design notes

Purpose: preserve the agreed direction for extending Solar Remote before the
protocol, Python SDK, and application tooling are designed and implemented in
separate passes. Delete or replace this note once the decisions have landed in
the canonical Remote design and public documentation.

The detailed metadata, enum-schema, manifest-v2, digest, and implementation
design is now accepted in `remote-metadata-and-manifest-v2.md`. That document
controls where these notes overlap.

## 1. Scope And Sequence

Work proceeds in this order:

1. Refine the Remote wire, schema, manifest, discovery, and transport design.
2. Implement and verify those Remote changes.
3. Design the reusable async-first Python SDK against the completed protocol.
4. Implement and verify the Python SDK.
5. Design the reusable Solar Station host/client service against the completed
   SDK.
6. Implement and verify Solar Station.
7. Build project-owned operator applications, such as RoboCup Tunnel, against
   `StationClient`.

A C++ host SDK remains deferred. The Python `solar-station` service is now a
separate layer above the direct `solar-remote` SDK; it must not leak process,
persistence, or UI policy into that lower SDK.

## 2. Stable Architectural Boundary

Solar Remote remains one transport-independent protocol. The same session,
framing, schema, request, subscription, credit, and compatibility behavior runs
over:

- a dedicated USB CDC ACM interface on an embedded target;
- a TCP stream link published from the Linux simulator container to host
  loopback; and
- deterministic in-memory links in focused tests.

Console output is a separate transport:

- an independent USB CDC ACM interface on embedded targets; and
- both process stdout/stderr and a sibling TCP text-stream sink in native
  simulation.

Remote binary traffic and console text must never share one byte stream. Zephyr
console logging remains available without a host SDK.

Only one simulator instance is required. The initial fixed host endpoints are:

```text
tcp://127.0.0.1:47000  Solar Remote
tcp://127.0.0.1:47001  rendered console/log stream
```

The simulator listens on the corresponding ports inside its Linux container.
Docker Compose publishes both ports only on host loopback. No dynamic port
allocation, instance registry, or socket discovery protocol is required.

## 3. Schema Metadata Without Language Reflection

Remote declarations are Solar's authored compile-time schema description. The
final linked manifest is the authoritative description of the effective wire
surface for one firmware image. No compiler type-name hashes, debug information,
DWARF parsing, or source-code scraping may become part of the wire contract.

C++ member pointers and type traits already provide:

- the owning value and member type;
- boolean, integer, floating-point, enumeration, bounded-text, and bounded-byte
  classification;
- integer signedness and width;
- optionality; and
- bounded text or byte capacity.

Standard C++ cannot recover a stable source-level member name from a member
pointer. Every external field therefore gains an explicit compile-time name.
The intended declaration shape is equivalent to:

```cpp
using Fields = remote::Fields<
    Field<1, "sequence", &TelemetrySample::sequence>,
    Field<2, "value", &TelemetrySample::value>>;
```

The exact template parameter order and supporting descriptor types are settled
during the detailed Remote design pass. Solar's `fixed_string` can carry the
name as a non-type template parameter. An optional convenience macro may
stringify a member name, but the ordinary non-macro declaration remains a
supported canonical form.

Field metadata should be extensible to descriptions, units, ranges, and
deprecation without placing those semantics in the payload encoding.

## 4. Enum Schemas

Detecting `std::is_enum_v<T>` is insufficient because C++ cannot enumerate the
declared names and values. Every enum used by an external field must have an
explicit stable Remote enum schema. Its conceptual shape is:

```cpp
template <> struct remote::EnumSchema<ControlMode>
{
    static constexpr TypeId id{0x...};
    static constexpr SchemaDescriptor descriptor{
        .id = id,
        .name = "control.ControlMode",
    };

    using Values = remote::EnumValues<
        EnumValue<ControlMode::Disabled, "disabled">,
        EnumValue<ControlMode::Manual, "manual">,
        EnumValue<ControlMode::Automatic, "automatic">>;
};
```

The detailed design may adjust spelling but preserves these rules:

- the enum type has a stable external type ID and version;
- every exported enumerator has an explicit wire value and stable name;
- enum value names are descriptive metadata, while numeric values remain the
  encoded representation;
- enum type IDs and enumerator numeric values cannot be silently reused;
- enum schemas participate in manifest compatibility checks; and
- an external field whose unwrapped value type is an enum fails compilation if
  its enum schema is missing from the effective bound Remote schema catalog.

The last rule is deliberate. An enum may not degrade to an undocumented integer
on the host merely because its underlying type can be encoded.

## 5. Manifest Extension

The manifest format must grow before generated host clients are treated as
typed. Schema field records need enough information to produce runtime models,
Python dataclasses, validation, help, and completion:

- stable field ID;
- field name and optional description;
- owning schema ID;
- primitive kind or referenced schema/enum type ID;
- integer signedness and bit width;
- bounded text or byte maximum length;
- required or optional status; and
- later semantic metadata such as units, ranges, and deprecation.

Raw C++ `sizeof(T)` is not a sufficient wire description. In particular, a
bounded wrapper's object size is not its maximum encoded string or byte length.

The manifest also gains enum and enum-value records. All records remain
pointer-free, deterministic, versioned, stable-ID based, and extracted from the
final linked ELF.

The manifest compiler continues to emit canonical CBOR, readable JSON, a
SHA-256 digest, and generated language artifacts. Compatibility tooling must be
able to classify additive and breaking schema changes.

## 6. Device And Capability Discovery

Transport discovery and capability discovery remain separate.

USB discovery uses device identity, VID/PID, USB serial number where available,
and an identifiable dedicated Remote CDC interface. The one supported native
simulator uses the fixed `tcp://127.0.0.1:47000` Remote endpoint, so it requires
no additional discovery mechanism.

Session discovery must make at least these facts available before typed calls:

- protocol version and negotiated limits;
- firmware/build identity;
- device identity;
- manifest digest; and
- supported optional protocol features.

The host resolves the digest against an explicit manifest, an installed
generated package, or a local cache. If no match exists and the firmware allows
it, the host may fetch the canonical manifest and use a dynamic client.

The current protocol documentation's manifest-digest compatibility promise must
be backed by an actual handshake or post-handshake discovery message. Runtime
introspection summaries are not a replacement for the complete Remote schema
manifest.

## 7. Logging Direction

The first implementation keeps ordinary Zephyr console logging separate from
Remote. This preserves early boot and failure diagnostics even when Remote is
unavailable.

Embedded and simulated targets expose equivalent host-facing shapes:

```text
Embedded console   dedicated USB CDC byte stream
Simulator console  TCP byte stream on 127.0.0.1:47001
```

The native simulator configures a socket log sink alongside its ordinary
Zephyr-console sink. Both receive the same rendered log output, allowing the
terminal to keep printing while Station independently consumes the socket.

The log socket is deliberately simple:

- server-to-client stream output only;
- rendered UTF-8 text;
- newline-terminated records where the rendered source provides record
  boundaries;
- no Remote session, CBOR, COBS, CRC, request protocol, history query, or replay;
- no buffering intended to provide persistence across a Station disconnect; and
- no effect on simulator progress when no client is attached or the client is
  slow.

This mirrors the dedicated CDC console as closely as practical. The sink uses
bounded non-blocking admission and reports drops through Logging's sink
accounting. Station owns all log retention, indexing, filtering, and persistence
after it receives bytes.

Typed logs exposed through Remote remain a possible later extension, but are
not required for the initial Station logging experience.
