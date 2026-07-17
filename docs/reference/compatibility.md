# Compatibility And Constraints

## Baseline

Solar 0.1 targets Zephyr 4.4 and C++23 with the full C++ standard library.
Exceptions and RTTI are disabled in all canonical fixtures. The current
verified toolchains are Zephyr SDK/host GCC 13.3 and the Zephyr 4.4 module build
contract.

Solar follows Zephyr releases rather than providing a freestanding non-Zephyr
firmware runtime. Host builds cover compile-time core, protocol, generators,
and SDK logic only.

## Verified targets

| Target | Coverage |
| --- | --- |
| `native_sim/native/64` | Runtime tests, canonical examples, concurrency, protocol, generated fixtures |
| Teensy 4.0 | Firmware compile/link, real EDT aliases, peripheral drivers, driver-managed DMA route |
| Host GCC | Core/catalog/protocol/host SDK tests and documentation generators |

Physical Teensy GPIO, UART, buses, watchdog, and timing require attached
hardware and are not claimed by simulator or compile-only evidence.

## Language and runtime assumptions

- C++23 library support includes `std::expected` and monadic operations.
- Public fallible APIs do not require exceptions.
- Canonical state is statically bounded unless a feature explicitly documents
  optional dynamic allocation.
- Static wrapper storage must retain a stable address while Zephyr owns native
  references.
- Endianness and packed wire layouts are protocol-defined, never raw C++ object
  serialization.

## Disabled-feature behavior

| Capability | Disabled behavior |
| --- | --- |
| Demand-derived Bus, Parameters, Events, Metrics, Remote | Intentional catalog use is a compile-time diagnostic; unused facility is absent |
| Kconfig-selected Logging, Health, Inspection | Aggregate header is unavailable or focused API returns `NotSupported` where a stable stub is provided |
| Supervisor | No service, response policy execution, or watchdog gate; Health may remain enabled |
| Execution | No task registrations/services; direct `solar::kernel` remains available |
| Hardware family | Family header is not aggregated and generated selectors are not emitted for that family |
| Optional diagnostics | Query returns unsupported/unavailable rather than fabricated zero or nominal data |
| Strict binding | Relaxed frontend binding is used; canonical catalogs and state are unchanged |

Intentional use of a disabled subsystem should fail as early and specifically
as its inclusion model permits. Runtime stubs never pretend success.

## Protocol compatibility

Remote protocol major versions must match. Minor negotiation, schema versions,
stable field IDs, endpoint IDs, and manifest digest determine image/client
compatibility. Generated host artifacts are tied to one linked firmware
surface; package them together.

## Deferred capabilities

Coroutine execution, controlled in-process reboot, generic direct DMA, broader
driver-family wrappers, and production board watchdog providers are not part of
the current public contract. Design notes are not availability claims.
