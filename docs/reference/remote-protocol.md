# Remote Protocol

Protocol version 1 is a transport-independent framed byte stream.

## Framing

- COBS provides delimiter-based resynchronization.
- CRC32C covers frame integrity.
- A fixed envelope carries version, kind, flags, session epoch, frame sequence,
  target, request ID, fragment identity, and payload size.
- Logical messages larger than a negotiated frame use ordered bounded
  fragmentation and timeout-based reassembly.

The canonical interoperability vectors are in
`tests/vectors/remote_protocol_v1.json` and are checked by both C++ and Python
implementations.

## Session flow

1. The server emits `ServerHello` with protocol and capacity bounds.
2. The client responds with `ClientHello` using compatible bounds.
3. The server confirms the active session epoch.
4. The client requests `ServerInformation`, which reports effective bounds,
   build identity, optional features, manifest size, and the exact embedded
   manifest SHA-256.
5. The host resolves a matching local manifest or retrieves it in bounded
   `Manifest` introspection chunks.
6. Requests, responses, subscriptions, data, credits, keepalives, cancellation,
   acknowledgement, introspection, errors, and reset messages use that epoch.

Protocol major mismatch rejects the session. Minor compatibility and manifest
digest determine which optional operations are safe. Reconnect creates a new
epoch and invalidates stale responders, leases, requests, and subscriptions.

## Encoding and compatibility

Ordinary schemas use deterministic CBOR with numeric field IDs. Packed encoding
is explicit and reserved for bounded high-rate layouts. Unknown optional CBOR
fields can be skipped; required field removal, ID reuse, type change, or packed
layout change requires an intentional compatibility/version decision.

## Manifest v2

The embedded `SLRM` image uses a 16-byte container header followed by sized
records. Unknown optional records can be skipped; an unknown record carrying
the `Required` flag rejects the manifest. Records describe object and enum
schemas, named fields, enum values, endpoints, link grants, and normalized
host-visible capabilities. The image and all records are little-endian,
pointer-free, deterministically sorted, and bounded.

Fields derive requiredness from `std::optional`; descriptions, units, and
deprecation are metadata. Closed enums reject undeclared numeric values while
open enums preserve them for forward-compatible hosts.

Remote is not encryption. Use an authenticated secure transport where the
threat model requires confidentiality or peer authentication.
