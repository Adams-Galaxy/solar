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
4. Requests, responses, subscriptions, data, credits, keepalives, cancellation,
   acknowledgement, introspection, errors, and reset messages use that epoch.

Protocol major mismatch rejects the session. Minor compatibility and manifest
digest determine which optional operations are safe. Reconnect creates a new
epoch and invalidates stale responders, leases, requests, and subscriptions.

## Encoding and compatibility

Ordinary schemas use deterministic CBOR with numeric field IDs. Packed encoding
is explicit and reserved for bounded high-rate layouts. Unknown optional CBOR
fields can be skipped; required field removal, ID reuse, type change, or packed
layout change requires an intentional compatibility/version decision.

Remote is not encryption. Use an authenticated secure transport where the
threat model requires confidentiality or peer authentication.
