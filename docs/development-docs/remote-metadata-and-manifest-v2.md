# Remote Metadata And Manifest V2

Date: 2026-07-23

Status: implemented

Depends on:

- `static-reform/design-specs/00-design-conventions.md`
- `static-reform/design-specs/02-identity-contributions-and-catalogs.md`
- `static-reform/design-specs/10-remote.md`
- `remote-host-extension.md`

## 1. Purpose

This design extends Solar Remote's authored schemas and final-ELF manifest so a
host can construct a complete dynamic client or generate a typed Python client
without compiler reflection, debug information, or a second authored IDL.

It establishes:

- names and extensible metadata directly on external fields;
- object and enumeration variants of the existing `Schema<T>` model;
- explicit enumeration contribution to the effective Remote schema catalog;
- compile-time rejection of missing or unbound enumeration schemas;
- exact host-visible scalar, bounded-value, enumeration, and packed metadata;
- externally observable endpoint-capability metadata;
- a versioned, sized, skippable pointer-free manifest record format;
- one deterministic manifest digest available to firmware and host tooling; and
- an implementation sequence that preserves protocol and generator
  interoperability at every landed stage.

The accepted approach is to enrich Solar's existing C++ declarations. An
external sidecar metadata system and a separately authored language-neutral IDL
are rejected for this work.

## 2. Goals

The completed manifest must let a host determine:

- which Data, Actions, Topics, and Streams exist;
- which operations each endpoint supports;
- which schemas those operations consume and produce;
- every field's stable ID, name, wire type, optionality, and bound;
- every referenced enumeration's identity, values, names, and unknown-value
  policy;
- the codec and exact layout required for CBOR and packed payloads;
- externally relevant permissions, rates, batching, and delivery behavior;
- the exact manifest revision used by the running firmware; and
- whether an installed generated client matches that revision.

The design must preserve:

- explicit stable numeric wire identities;
- deterministic compile-time validation;
- pointer-free final-ELF artifacts;
- bounded firmware storage and processing;
- no dynamic registration or allocation requirement;
- transport independence; and
- one authored schema definition shared by codecs, runtime dispatch, manifest
  generation, dynamic clients, and generated clients.

## 3. Non-Goals

This work does not initially add:

- arbitrary C++ reflection;
- compiler type-name or member-name extraction;
- DWARF or other debug-information parsing;
- YAML, CDDL, Protobuf, or another authored IDL;
- unbounded strings or collections;
- nested object fields;
- bounded arrays or maps;
- unions or variants;
- defaults inferred from C++ object initialization;
- arbitrary host-side validation expressions;
- a C++ host SDK;
- a host daemon; or
- transport implementation for USB CDC or simulator TCP.

The manifest format reserves a referenced-type representation so nested and
collection schemas can be introduced later without replacing the format.

## 4. Source Of Truth

Solar's C++ Remote declarations are the source-level IDL.

The manifest emitted from the final bound `System` is the authoritative external
surface for one firmware image. It contains only declarations and policies that
survive normalization, catalog binding, and target configuration.

The post-link generator consumes that embedded image and produces host
artifacts. Generated Python must not independently infer schema facts from
C++ source, compiler symbols, or debug information.

Stable numeric IDs remain wire identity. Human-readable names are metadata used
for generated APIs, dynamic lookup, documentation, help, and completion.

## 5. Named Field Declarations

### 5.1 Canonical form

The canonical field declaration becomes:

```cpp
template <FieldId Id, FixedString Name, auto Member, typename... Attributes>
struct Field;
```

Example:

```cpp
template <> struct solar::remote::Schema<app::Euler>
{
    static constexpr SchemaDescriptor descriptor{
        .id = TypeId{0x1201},
        .name = "imu.Euler",
        .description = "Orientation expressed as Euler angles",
    };

    using Fields = remote::Fields<
        Field<1, "roll", &app::Euler::roll, Unit<"rad">>,
        Field<2, "pitch", &app::Euler::pitch, Unit<"rad">>,
        Field<3, "yaw", &app::Euler::yaw, Unit<"rad">>>;

    static constexpr std::size_t max_encoded_size = 48;
    static constexpr Codec codec = Codec::Cbor;
};
```

The initial implementation must support the mandatory ID, name, and member
pointer. Its attribute mechanism must be extensible. Initial useful attributes
are:

```cpp
Description<"...">
Unit<"...">
Deprecated<"...">
```

An attribute affects the manifest and generated host interface only when its
contract says so. It must not silently alter payload encoding.

### 5.2 Name rules

Field names:

- must be non-empty UTF-8;
- must be unique within one object schema;
- are emitted exactly as authored;
- do not replace the stable field ID;
- may change without changing a CBOR key; and
- are treated as generated-source compatibility changes even when wire
  compatibility is retained.

A convenience macro may later stringify a member name, but the non-macro
declaration remains canonical and fully supported.

### 5.3 Required and optional fields

Requiredness is derived from the member type:

- `T` is required;
- `std::optional<T>` is optional.

The former independent `Required` Boolean template argument is removed. This
prevents a declaration from claiming that an absent optional member is a
required encoded field.

Packed schemas continue to reject optional fields.

### 5.4 Field type derivation

After removing `std::optional`, the implementation derives:

- Boolean;
- unsigned integer and bit width;
- signed integer and bit width;
- IEEE floating-point width;
- enumeration reference;
- bounded UTF-8 text and maximum byte length; or
- bounded bytes and maximum length.

The host-visible bound of `BoundedText<N>` and `BoundedBytes<N>` is `N`, not
`sizeof(T)`.

The initial object codec continues to reject unsupported and unbounded member
types at compile time.

## 6. Enumeration Schemas

### 6.1 One schema identity domain

Enumerations use the existing `Schema<T>`, `SchemaTag`, `TypeId`, and
`RemoteSchemaCatalog`. They do not introduce a second type-identity domain.

`Schema<T>` therefore has variants selected by `SchemaShape`:

- `Object`;
- `Enumeration`; and
- the existing special status-code shape.

Object schemas provide `Fields`, `codec`, and `max_encoded_size`. Enumeration
schemas provide `Values`, an underlying representation derived from the C++
enum type, and an openness policy.

### 6.2 Canonical enumeration declaration

```cpp
template <> struct solar::remote::Schema<app::ReferenceFrame>
{
    static constexpr SchemaDescriptor descriptor{
        .id = TypeId{0x1202},
        .name = "imu.ReferenceFrame",
        .description = "Reference frame used by an orientation sample",
    };

    static constexpr SchemaShape shape = SchemaShape::Enumeration;
    static constexpr EnumOpenness openness = EnumOpenness::Closed;

    using Values = remote::EnumValues<
        EnumValue<app::ReferenceFrame::Body, "body">,
        EnumValue<app::ReferenceFrame::World, "world">>;
};
```

`EnumValue` has the conceptual form:

```cpp
template <auto Value, FixedString Name, typename... Attributes>
struct EnumValue;
```

It supports the same extensible descriptive attributes where meaningful.

### 6.3 Explicit catalog contribution

An enumeration referenced by an external object field must be explicitly
contributed:

```cpp
struct Imu
{
    using RemoteSchemas =
        remote::ContributeSchemas<app::ReferenceFrame>;

    using RemoteData =
        remote::ContributeData<app::EulerTelemetry>;
};
```

Endpoint object schemas remain derived transitively from the effective endpoint
catalogs as they are today. Enumeration schemas are explicit infrastructure and
must appear in the effective bound `RemoteSchemaCatalog`.

Solar-owned adapter enums are the narrow exception to application contribution.
Their schemas are framework built-ins and are added to the manifest only when a
selected adapter schema references them. The reserved built-in schema IDs are:

- `1`: `solar.Empty`;
- `2`: `solar.Status`;
- `3`: `solar.Error`;
- `4`: `solar.lifecycle.SystemState`;
- `5`: `solar.lifecycle.ComponentState`; and
- `6`: `solar.log.Level`.

This exception does not make arbitrary discovered enum schemas implicit.
Application enums still require `ContributeSchemas`.

Manifest binding performs the system-aware validation because ordinary
`validate_schema<T>()` does not know which `System` is being bound.

### 6.4 Validation

Compilation fails when:

- an enum field has no valid enumeration `Schema<T>`;
- its enum schema is absent from the effective bound schema catalog;
- schema IDs collide across object, enum, and special schemas;
- schema names collide;
- an enum has no declared values;
- enum value names are empty or duplicated;
- enum numeric values are duplicated;
- a declared value does not belong to the enum type; or
- metadata cannot represent the enum's underlying signedness or width.

The missing-catalog diagnostic uses a stable Solar diagnostic token:

```text
SOLAR_DIAGNOSTIC_REMOTE_UNBOUND_ENUM_SCHEMA
```

C++ cannot enumerate all source-level enumerators. Solar can verify the complete
authored wire set, but cannot prove that every enumerator in the C++ definition
was included.

### 6.5 Closed and open enums

`EnumOpenness::Closed` is the default:

- decoding an undeclared numeric value fails;
- control and configuration values therefore cannot silently enter an unknown
  state.

`EnumOpenness::Open` explicitly permits unknown underlying values:

- the numeric value is retained;
- generated and dynamic clients expose that it has no known symbolic name; and
- adding a named value may be treated as wire-additive.

Both CBOR and packed decoding enforce the declared policy.

Top-level enum endpoint values are deferred. The initial enum schema is a
referenced field type. The v2 manifest representation does not prevent adding
top-level scalar or enum endpoint schemas later.

## 7. Manifest V2 Container

### 7.1 Header

Manifest v2 retains the deterministic 16-byte image header:

```text
magic[4]          "SLRM"
format_version    u16 little-endian, value 2
protocol_major    u8
protocol_minor    u8
record_count      u16 little-endian
flags             u16 little-endian, initially zero
total_size        u32 little-endian
```

Manifest format version and Remote protocol version remain independent.

### 7.2 Sized records

Every record begins with:

```text
kind              u8
flags             u8
size              u16 little-endian, including this header
```

Rules:

- `size` is at least four;
- a record may be skipped using `size`;
- bit zero of `flags` is `Required`; all other initial bits are reserved;
- unknown kinds without `Required` are skipped;
- unknown kinds with `Required` cause a clear unsupported-manifest error;
- all reserved fields and bits are zero;
- strings are length-prefixed UTF-8 without trailing NUL; and
- no record contains a pointer, compiler-dependent layout, or native object
  representation.

The initial kinds preserve existing numeric identities where practical:

```text
1  Schema
2  Field
3  Data
4  Action
5  Topic
6  Stream
7  Link
8  EnumValue
9  Capability
```

The implementation defines named encoding and decoding functions. It must not
serialize packed C++ structs directly.

### 7.3 Deterministic ordering

Records are emitted in this order:

1. schemas ordered by stable type ID;
2. fields ordered by owning type ID and then field ID;
3. enum values ordered by owning type ID and then numeric value;
4. Data ordered by stable ID;
5. Actions ordered by stable ID;
6. Topics ordered by stable ID;
7. Streams ordered by stable ID;
8. capabilities ordered by endpoint domain, endpoint ID, and capability kind;
9. links ordered by stable ID.

Catalog ordering may already produce this order, but manifest generation must
validate or normalize it explicitly. Equivalent effective systems must emit
identical bytes.

## 8. Manifest V2 Records

This section defines semantic fields and their encoding order. Exact byte
vectors become canonical in `tests/vectors` when implementation lands.

### 8.1 Schema

```text
type_id               u32
version               u16
shape                 u8
codec                 u8
max_encoded_size      u32
underlying_kind       u8
underlying_flags      u8
underlying_bit_width  u16
name_length           u16
description_length    u16
name                  bytes
description           bytes
```

For object schemas:

- `codec` and `max_encoded_size` are populated;
- underlying fields are zero.

For enumeration schemas:

- `codec` and `max_encoded_size` are zero because the containing schema controls
  field encoding;
- underlying kind is signed or unsigned;
- underlying flags carry openness;
- underlying width is the enum's underlying bit width.

### 8.2 Field

```text
owner_type_id         u32
referenced_type_id    u32
field_id              u16
packed_offset         u32
value_kind            u8
field_flags           u8
bit_width             u16
maximum_length        u32
name_length           u16
description_length    u16
unit_length           u16
reserved              u16
name                  bytes
description           bytes
unit                  bytes
```

Rules:

- `referenced_type_id` is nonzero for enum and future referenced-schema fields;
- primitive fields use zero;
- `packed_offset` is a byte offset for packed schemas and `UINT32_MAX` for CBOR;
- `field_flags` initially represents required and deprecated status;
- numeric and Boolean fields use `bit_width`;
- text and byte fields use `maximum_length`;
- unused representation fields are zero; and
- packed width is derived from the referenced or primitive wire type.

Initial `value_kind` values cover Boolean, unsigned integer, signed integer,
floating point, enumeration, text, bytes, and a reserved schema reference.

### 8.3 EnumValue

```text
owner_type_id         u32
numeric_value         u64 two's-complement bit representation
value_flags           u8
reserved[3]           zero
name_length           u16
description_length    u16
name                  bytes
description           bytes
```

The owning Schema record determines signed interpretation and width.

### 8.4 Endpoint records

Data, Action, Topic, and Stream records continue to carry:

- stable endpoint ID;
- endpoint version;
- endpoint name and description; and
- referenced value, request, response, and error schema IDs as appropriate.

The Data capability mask remains a convenient summary, but is no longer the
complete capability description.

Action records retain their required permission mask.

Link records gain the link's declared grant mask. Transport-specific host paths,
device nodes, and TCP ports are not schema metadata and are not embedded here.

### 8.5 Capability

One record is emitted for each externally visible endpoint capability:

```text
endpoint_domain       u8
capability_kind       u8
permission_mask       u8
codec                 u8
endpoint_id           u32
maximum_rate_hz       u32
maximum_batch         u16
reliable_window       u16
delivery_kind         u8
capability_flags      u8
reserved              u16
```

The record exposes only host-observable policy:

- supported operation;
- required permission;
- payload codec;
- declared maximum rate;
- declared maximum batch;
- reliable inbound window where relevant;
- latest, queued-lossy, or reliable delivery class; and
- protocol-visible feature flags such as cancellation.

It does not expose execution targets, work queues, thread priorities, internal
pool layout, or other implementation policy.

Runtime negotiation remains authoritative. The manifest states declared
ceilings and behavior; a live session reports the effective accepted policy.

## 9. Metadata Scope

### 9.1 Required in the first implementation

- field names;
- exact primitive kind, width, and signedness;
- required or optional status;
- bounded text and byte lengths;
- enum schema IDs, names, versions, values, and openness;
- schema codec and maximum encoded size;
- packed offsets;
- endpoint schema references;
- endpoint capability and permission records;
- declared stream rate, batch, delivery, and window facts;
- manifest digest; and
- generator support for the complete v2 model.

### 9.2 Supported extension points

- descriptions;
- units;
- deprecation metadata;
- numeric ranges;
- examples; and
- display hints.

Descriptions, units, and deprecation are included in the initial record shape,
but application use may be introduced incrementally.

### 9.3 Deferred type forms

- nested object fields;
- bounded arrays and maps;
- variants and tagged unions;
- top-level scalar or enum endpoints; and
- explicit wire defaults.

## 10. Manifest Digest

The compatibility digest is SHA-256 over the exact deterministic embedded v2
manifest bytes.

The bound image exposes:

```cpp
manifest::Image<System>::bytes
manifest::Image<System>::digest
```

Firmware session discovery uses `digest`. The post-link generator computes the
same SHA-256 from the extracted bytes and verifies it against canonical test
vectors.

`manifest.sha256` contains this embedded-image digest. Generated CBOR and JSON
include it as metadata but are not independently used as the compatibility hash
source.

Descriptions and other host-visible metadata participate in the exact digest.
A separate compatibility-diff tool classifies whether two different digests are
wire-compatible; digest inequality alone means “not exact,” not necessarily
“breaking.”

The digest is not embedded inside the bytes it hashes.

## 11. Runtime Discovery And Retrieval

The Remote session must expose:

- protocol version;
- negotiated frame and message bounds;
- firmware/build identity;
- manifest digest; and
- optional protocol feature flags.

This may extend Hello or use a mandatory post-Hello server-information message.
The detailed protocol change is finalized during implementation, but the digest
must be available before a typed application request is issued.

When configured, firmware exposes bounded manifest retrieval using offset and
limit. Chunked reads are preferred to requiring one full manifest-sized working
buffer. Remote fragmentation may still carry individual responses.

Runtime retrieval returns the exact embedded manifest bytes whose digest was
advertised.

## 12. Generated And Dynamic Python Results

Manifest v2 must be sufficient to generate:

- Python dataclasses for object schemas;
- `enum.IntEnum`-compatible models for known enum values;
- an unknown-value representation for open enums;
- required and optional type annotations;
- bounded text and byte validation;
- typed Action request, response, and error methods;
- typed Data query and update methods;
- async watch and stream facades;
- endpoint capability constants;
- names, descriptions, units, and deprecation help; and
- a baked-in expected manifest digest.

The dynamic client consumes the same parsed manifest model. Generated and
dynamic clients must use one runtime codec and session implementation.

## 13. Compatibility Rules

The compatibility tool distinguishes wire, generated-source, and behavioral
compatibility.

Examples:

- adding an optional CBOR field is wire-additive;
- adding a required field is breaking;
- removing a field is breaking for producers or consumers that require it;
- reusing a field or type ID is forbidden;
- changing a field's wire kind, width, bound, or referenced type is breaking;
- changing packed order, offset, width, or total size is breaking;
- changing a closed enum numeric mapping is breaking;
- adding a closed enum value requires an explicit compatibility decision;
- adding an open enum name for a previously unknown numeric value is
  wire-additive;
- changing a field name preserves wire identity but changes generated source;
- reducing a maximum stream rate is behavioral tightening;
- raising a maximum stream rate is behavioral relaxation; and
- description or unit changes alter the exact digest but are normally
  wire-compatible.

Compatibility classification never permits stable ID reuse.

## 14. Diagnostics

New validation failures use stable tokens, including:

```text
SOLAR_DIAGNOSTIC_REMOTE_EMPTY_FIELD_NAME
SOLAR_DIAGNOSTIC_REMOTE_DUPLICATE_FIELD_NAME
SOLAR_DIAGNOSTIC_REMOTE_FIELD_ATTRIBUTE_COLLISION
SOLAR_DIAGNOSTIC_REMOTE_MISSING_ENUM_SCHEMA
SOLAR_DIAGNOSTIC_REMOTE_UNBOUND_ENUM_SCHEMA
SOLAR_DIAGNOSTIC_REMOTE_EMPTY_ENUM
SOLAR_DIAGNOSTIC_REMOTE_DUPLICATE_ENUM_NAME
SOLAR_DIAGNOSTIC_REMOTE_DUPLICATE_ENUM_VALUE
SOLAR_DIAGNOSTIC_REMOTE_ENUM_REPRESENTATION
SOLAR_DIAGNOSTIC_REMOTE_MANIFEST_RECORD_SIZE
SOLAR_DIAGNOSTIC_REMOTE_MANIFEST_REQUIRED_RECORD
```

Compile-fail tests assert these tokens rather than compiler-specific prose.

## 15. Implementation Sequence

### Stage 1: declaration metadata

- Add named `Field`.
- Derive requiredness from `std::optional`.
- Add attribute normalization.
- Update all schemas and compile-fail tests.
- Preserve existing CBOR and packed vectors.

### Stage 2: enumeration schemas

- Add enumeration `Schema<T>` variant.
- Add `EnumValue`, `EnumValues`, and openness.
- Add local and bound-system validation.
- Enforce closed/open decoding in CBOR and packed codecs.
- Add enum compile-fail and codec tests.

### Stage 3: manifest v2 core

- Add the sized-record writer.
- Emit Schema, Field, and EnumValue records.
- Implement exact scalar bounds and packed offsets.
- Update the Python parser to accept v2.
- Keep v1 parsing temporarily only where fixture migration requires it.
- Add deterministic C++ and Python vectors.

### Stage 4: capability metadata

- Normalize host-visible capability policies.
- Emit Capability and link-grant records.
- Verify manifest policy facts against runtime negotiation behavior.

### Stage 5: digest and discovery

- Compute and expose the exact embedded-image digest.
- Extend session discovery with build identity and digest.
- Add optional chunked manifest retrieval.
- Add mismatch, cache, and retrieval tests.

### Stage 6: host artifacts and compatibility

- Generate complete Python types and endpoint facades.
- Share one parsed manifest model with the dynamic client.
- Add manifest diff classification.
- Update public Remote protocol, generation, and compatibility documentation.

Each stage lands with host tests, Zephyr tests, native-simulation coverage where
applicable, documentation updates, and no known divergence between C++ and
Python protocol behavior.

## 16. Rejected Alternatives

### 16.1 Sidecar field metadata

Separate `FieldMetadata<Value, Id>` specializations split field identity,
encoding, and host metadata across declarations. They increase the chance of
stale IDs and indirect diagnostics. Rejected.

### 16.2 Separately authored external IDL

A language-neutral IDL would introduce a generator dependency before firmware
compilation and duplicate application binding concerns while C++ remains the
firmware authoring language. Deferred unless Solar later requires equal
multi-language firmware authorship.

### 16.3 Fixed-size implicit v2 records

Simply enlarging current records leaves future readers unable to skip unknown
metadata. Rejected in favor of sized records.

### 16.4 Embedded canonical CBOR

Emitting canonical CBOR directly from compile-time C++ templates increases
firmware artifact complexity and size without benefiting the firmware runtime.
The compact deterministic binary image remains canonical; post-link tooling
continues to produce CBOR and JSON. Rejected.

## 17. Accepted Outcome

Solar Remote retains one authored C++ schema system and one transport-independent
wire protocol. The final bound system emits enough deterministic metadata for a
host to understand the complete supported Remote surface.

Field names are explicit. Enum values are explicit. Enum schemas are globally
bound and compile-time checked. Manifest records are versioned, sized, and
skippable. Host-observable capability policies are described without leaking
firmware execution internals. The same manifest model drives dynamic operation,
generated Python, compatibility checks, and runtime session matching.
