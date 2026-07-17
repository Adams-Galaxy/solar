# Stage 6: Remote And Host Integration

Status: complete

## Landed

- Added the runnable `examples/remote-control` firmware sample and host artifact inspector.
- Documented schemas, Data capabilities, Actions, streams, links, sessions,
  acquisition, backpressure, protocol framing, and generated artifacts.
- Added Remote API, protocol, runtime architecture, and client-generation pages.

## Evidence

- Remote example passed on `native_sim/native/64` with no warnings.
- Its post-link manifest and generated `FirmwareClient` loaded successfully.
- Existing protocol vectors remain the C++/Python interoperability source.

## Decisions

The documentation example uses the deterministic in-memory Link. Physical
UART/USB/TCP ownership is target-specific and is documented as a Link adapter,
while full session/query/action/stream behavior remains covered by the focused
Remote runtime and host test suites.
