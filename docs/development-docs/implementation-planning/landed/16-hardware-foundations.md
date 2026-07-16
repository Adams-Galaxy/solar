# Stage 16: Hardware Generator And Foundations

Status: landed

Landed date: 2026-07-16

Implementation repository/branch: `/workspaces/solar`, `static_reform`

Relevant commits or change identifiers: working-tree implementation pass; no
stage commit was requested.

## 1. Objective

Land the build and type foundations for `solar::hardware` without creating a
second HAL. The stage had to consume each Zephyr image's resolved devicetree,
provide compact generated and explicit endpoint descriptors, establish
readiness and native-error behavior, and deliver useful GPIO and interrupt
roles plus caller-owned asynchronous completion scaffolding.

The resulting layer is usable without a Solar System, owns no application
lifecycle, and leaves device creation, initialization, pinctrl, and driver
state with Zephyr.

## 2. Specification Coverage

| Specification | Sections implemented | Notes |
| --- | --- | --- |
| `13-hardware-and-devicetree.md` | architecture boundary, devicetree model, descriptor model, EDT generation, common wrapper contract, foundational ownership, GPIO, errors, Kconfig, compile/native testing | Complete for the Stage 16 foundation and GPIO scope. |
| `00-design-conventions.md` | static typed surface, Kconfig inclusion, no hidden allocation or execution | Hardware remains independent of the System graph. |
| `00a-modern-cpp-result-and-status.md` | focused `Result<T, hardware::Error>` outcomes and native detail | Runtime driver errors retain operation, reason, status, errno, and endpoint path. |
| `03-lifecycle-kernel-and-configuration.md` | Zephyr-native configuration boundary | Hardware is selected through Solar module Kconfig and does not participate in Lifecycle. |

SPI, I2C, UART, ADC, PWM, Counter, Watchdog, family-specific asynchronous
operations, RTIO, and firmware Device migration belong to Stage 17. Advanced
direct DMA remains intentionally outside Solar's portable surface.

## 3. Public Surface Landed

The aggregate header is `solar/hardware.hpp`. `solar/solar.hpp` includes it
only when `CONFIG_SOLAR_HARDWARE=y`.

The public foundation includes:

- endpoint and selector kinds, capability flags, identities, and inventory
  entries;
- structural `NodeDescriptor`, `DeviceDescriptor`, and `GpioDescriptor`
  values;
- generated `dt::alias<"...">`, `dt::chosen<"...">`, and
  `dt::node_label<"...">` lookup;
- permanent explicit `dt::gpio(GPIO_DT_SPEC_GET(...))` construction;
- `Endpoint<Spec>` with descriptor, path, readiness, required readiness, and
  native-handle access where a native descriptor exists;
- focused Hardware operations, reasons, errors, and errno translation;
- `gpio::Pin`, `gpio::Input`, `gpio::Output`, `gpio::Interrupt`, and
  `gpio::Port`;
- logical and raw GPIO access, configuration, toggle, pending state, and
  callback ownership;
- caller-owned `async::Gate` and generation `Token` for exactly-once
  completion, busy admission, cancellation, and stale-completion rejection.

Generated board use is compact:

```cpp
using StatusLed = solar::hardware::gpio::Output<
    solar::hardware::dt::alias<"led0">>;

SOLAR_TRY(StatusLed::configure());
SOLAR_TRY(StatusLed::activate());
```

Generation is optional at the build level. The equivalent explicit path is:

```cpp
inline constexpr auto LedSpec = solar::hardware::dt::gpio(
    GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios));
using StatusLed = solar::hardware::gpio::Output<LedSpec>;
```

## 4. Runtime Ownership

| Owner | Storage/resource | Capacity | Synchronization | Lifetime |
| --- | --- | --- | --- | --- |
| Zephyr device model | devices, GPIO controller state, pin configuration, driver callbacks installed into a controller | driver-defined | Zephyr driver contract | image lifetime |
| generated descriptor header | immutable compile-time descriptor and inventory values | one entry per generated alias, chosen node, and node label | none | no runtime storage unless odr-used |
| GPIO physical endpoint core | one `gpio_callback`, handler, registration flag, and trigger value per odr-used `(port, pin)` key | one callback owner per physical pin | atomics plus Zephyr callback synchronization | image lifetime |
| caller | `async::Gate`, buffers, and future family operation objects | caller-selected | atomic generation and active state | caller-owned |
| build system | deterministic JSON EDT summary | one per Zephyr image | build dependency graph and atomic file replacement | build artifact only |

Hardware allocates no heap memory and creates no thread, stack, work item,
workqueue, timer, poll object, mutex, lifecycle component, or runtime registry.
No constructor registration or cross-translation-unit static initialization is
used.

Aliases resolving to the same physical GPIO use the same `(device*, pin)`
template key. They therefore cannot install conflicting independent callbacks
merely because they have different devicetree selector names.

## 5. Compile-Time Behavior

The Solar module CMake hook reads `${EDT_PICKLE}` using Zephyr's supported
`edtlib` Python model. It emits:

- `zephyr/include/generated/zephyr/solar/hardware/generated/devicetree.hpp`;
- `solar/hardware/devicetree.json` in the image build directory.

Selectors are sorted by category and name. Endpoint identities use the
resolved node path and a deterministic non-zero 32-bit FNV-1a path hash, so
aliases of the same physical node share physical identity. Files are replaced
only when content changes.

The generator currently emits native GPIO descriptors because Stage 16 enables
and verifies that driver family. Other recognized families retain endpoint
kind, path, compatible, status, and capability metadata as `NodeDescriptor`
values until their Stage 17 Kconfig option enables the corresponding native
driver descriptor. This avoids creating `DEVICE_DT_GET` references for an
otherwise disabled Zephyr driver merely because a chosen node or label exists.

Stable diagnostics cover:

- `SOLAR_DIAGNOSTIC_HARDWARE_ALIAS_NOT_GENERATED`;
- `SOLAR_DIAGNOSTIC_HARDWARE_CHOSEN_NOT_GENERATED`;
- `SOLAR_DIAGNOSTIC_HARDWARE_NODELABEL_NOT_GENERATED`;
- `SOLAR_DIAGNOSTIC_INVALID_HARDWARE_ENDPOINT`;
- `SOLAR_DIAGNOSTIC_HARDWARE_GPIO_DISABLED`;
- `SOLAR_DIAGNOSTIC_HARDWARE_GPIO_DESCRIPTOR_REQUIRED`;
- `SOLAR_DIAGNOSTIC_HARDWARE_GPIO_INTERRUPTS_DISABLED`.

When generation is disabled, no generated header is produced and explicit
descriptors remain available. When Hardware is disabled, the aggregate is not
included and the generator is absent.

## 6. Error And Availability Behavior

Hardware returns `Result<T, hardware::Error>`. `Error` records:

- Solar `Status`;
- focused `Reason`;
- attempted `Operation`;
- original native result or errno;
- resolved endpoint path where available.

The focused reasons distinguish not ready, unsupported, invalid
configuration, busy, timeout, cancelled, resource exhaustion, endpoint already
owned, stale completion, and driver failure.

Metadata-only endpoints report `Unsupported` from `require_ready()`. Native
endpoints that fail `device_is_ready` or a family-specific readiness check
report `NotReady`. Native driver failures preserve the original return value
and map it through Solar's canonical errno translation.

GPIO callback installation rejects a null handler and duplicate physical
ownership. Callback removal, interrupt configuration, and pending-state calls
preserve native failures. `async::Gate` rejects concurrent admission and stale
or duplicate completion generations.

## 7. Zephyr Integration

Stage 16 directly uses:

- Zephyr module Kconfig and CMake;
- the image's resolved `edt.pickle` and Zephyr `edtlib`;
- `DT_ALIAS`, `DT_CHOSEN`, `DT_NODELABEL`, and `GPIO_DT_SPEC_GET`;
- `gpio_dt_spec`, `gpio_is_ready_dt`, pin configuration, logical/raw access,
  callback, interrupt, pending, and port APIs;
- `device_is_ready` for native device descriptors;
- native driver errno values and device/spec handles.

Relevant Kconfig is:

- `CONFIG_SOLAR_HARDWARE`;
- `CONFIG_SOLAR_HARDWARE_GENERATE_DEVICETREE`;
- `CONFIG_SOLAR_HARDWARE_GPIO`;
- `CONFIG_SOLAR_HARDWARE_GPIO_INTERRUPTS`.

GPIO interrupt handlers execute in the context supplied by the Zephyr GPIO
driver. Solar invokes only the installed bounded function pointer there and
does not perform an implicit thread or workqueue handoff.

Native simulation validates behavior with `gpio_emul`. Teensy validates that
the same generated selector and wrapper form compiles against the real board
EDT and toolchain.

## 8. Files Changed

### Added

- `include/solar/hardware.hpp`
- `include/solar/hardware/{async,dt,endpoint,error,gpio,types}.hpp`
- `tools/hardware/generate_devicetree.py`
- `tests/zephyr/hardware_foundation/`
- `tests/zephyr/hardware_explicit/`
- `tests/zephyr/hardware_compile_fail/`
- `tests/zephyr/check_hardware_compile_fail.py`
- `tests/zephyr/check_hardware_headers.py`

### Reshaped

- `CMakeLists.txt`: per-image EDT generation hook and generated build outputs.
- `include/solar/solar.hpp`: Kconfig-guarded Hardware aggregate.
- `zephyr/Kconfig`: Hardware, generation, GPIO, and interrupt selection.

### Removed

- None. The earlier HAL-like implementation had already been removed during
  the hard repository reset.

## 9. Tests And Evidence

| Command | Platform/configuration | Result | What it proves |
| --- | --- | --- | --- |
| `cmake --build build/host -j8 && ctest --test-dir build/host --output-on-failure` | host C++23 | pass, 57/57 | Existing modern core, catalog, binding, protocol, and host SDK behavior remains green. |
| `west twister -T tests/zephyr/hardware_foundation -T tests/zephyr/hardware_explicit --inline-logs --integration` | native generated, native explicit, Teensy generated build-only | pass, 3/3 configurations, 5/5 executed cases, no warnings | Generated and explicit descriptor paths, native GPIO behavior, physical callback ownership, async generation gate, and Teensy compilation. |
| `python3 tests/zephyr/check_hardware_compile_fail.py --compile-commands build/hardware-compile-fail/compile_commands.json` | Zephyr C++23 compile database | pass, 3/3 negative cases | Missing generated alias, wrong descriptor family, and invalid endpoint diagnostics are stable. |
| `python3 tests/zephyr/check_hardware_headers.py --compile-commands build/hardware-compile-fail/compile_commands.json --include-root include` | Zephyr C++23 | pass, 7/7 headers | Every Stage 16 public Hardware header is self-contained. |
| `west twister -T tests/zephyr --inline-logs --integration` | full Solar Zephyr matrix | pass, 80 executed plus 1 Teensy build-only configuration, 252/252 executed cases, no warnings | Hardware Kconfig, generated includes, and aggregates introduce no cross-stage regressions. |

The post-build generator check reruns the generator against the same EDT and
byte-compares both outputs. It also verifies that `led0` resolves as GPIO.

Observed generated artifact sizes were approximately:

| Board/fixture | Generated C++ header | JSON summary |
| --- | ---: | ---: |
| native Hardware fixture | 30,612 B | 14,171 B |
| Teensy 4.0 | 309,705 B | 144,135 B |

These are build metadata sizes, not an implied runtime allocation. The Teensy
header is larger because node-label metadata is generated for the resolved SoC
tree. Firmware storage is emitted only for odr-used values and operations.

## 10. Specification Refinements

Observed contract: eagerly turning every recognized generated selector into a
native device descriptor caused a link reference to the chosen console even
when Zephyr Serial was disabled.

Evidence: the first Hardware foundation build failed on an undefined device
ordinal after generating `DEVICE_DT_GET(DT_CHOSEN(zephyr_console))` in a GPIO-
only image.

Accepted change: Stage 16 generates native descriptors only for the landed
GPIO family. Other family selectors remain typed metadata until the owning
Stage 17 family Kconfig enables its native descriptor form.

Specifications updated: no architectural specification change was necessary;
this is the implementation of the accepted rule that EDT cannot prove runtime
driver inclusion or capability.

Verification added: the GPIO-only native fixture includes a UART chosen node,
asserts its UART identity, and verifies `require_ready()` reports Unsupported
without pulling Serial into the image.

## 11. Firmware And Host Impact

No firmware migration was designated for Stage 16. Existing firmware remains
on direct Zephyr UART selection until Stage 17 lands `hardware::uart` and the
application Device boundary.

Host-only Solar builds do not expose Hardware because the layer is a Zephyr
module surface. Existing host tests remain unaffected.

## 12. Known Limits And Deferred Work

- Non-GPIO selectors are metadata-only until their Stage 17 family is enabled.
- GPIO callbacks use one function pointer without payload; richer callback
  ownership can be added only where it remains bounded and context-clear.
- The generated catalog currently emits every node label; making node-label
  generation opt-in remains an accepted compile-time optimization.
- No RTIO wrapper or family-specific asynchronous Operation exists yet.
- No generic direct-DMA abstraction will be added. Zephyr peripheral drivers
  remain responsible for normal DMA-backed transfers.
- Hardware has no System, Lifecycle, Health, or Inspection registration by
  design. Application Devices supply those semantics.

## 13. Documentation Handoff

The public documentation pass should explain:

- choosing generated selectors versus explicit Zephyr spec construction;
- semantic board aliases and why component headers include board headers, not
  the root System;
- readiness versus external-device responsiveness and Health;
- logical versus raw GPIO behavior;
- interrupt callback context and physical endpoint ownership;
- caller-owned asynchronous generations;
- generated build artifacts and why they are not runtime registries;
- native handle escape hatches and direct Zephyr interoperation.

The executable examples in `tests/zephyr/hardware_foundation/src/main.cpp` and
`tests/zephyr/hardware_explicit/src/main.cpp` are the preferred source examples.

## 14. Closure Statement

Stage 16 is complete because Solar now derives deterministic typed endpoint
metadata from Zephyr's canonical resolved EDT, supports both generated and
explicit endpoint construction, provides focused readiness/error semantics,
delivers GPIO and interrupt roles with correct physical ownership, and adds no
competing HAL state or hidden execution. Stage 17 Hardware Driver Families and
Devices is now active.
