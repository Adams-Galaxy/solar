# Stage 17: Hardware Driver Families And Devices

Status: landed

Landed date: 2026-07-16

Implementation repository/branch: `/workspaces/solar`, `static_reform`, with
the firmware integration in `/workspaces/ENMT301-RoboCup`, `dev`.

Relevant commits or change identifiers: working-tree implementation pass; no
stage commit was requested.

## 1. Objective

Complete the accepted initial `solar::hardware` family surface over Zephyr's
public drivers, preserve native asynchronous and DMA-backed execution paths,
and move firmware's board-selected UART behind a generated typed Hardware alias
and an application Device lifecycle boundary.

The stage had to add useful C++23 typing and ownership without becoming a
second HAL, adding a portable direct-DMA fiction, or creating hidden execution.

## 2. Specification Coverage

| Specification | Sections implemented | Notes |
| --- | --- | --- |
| `13-hardware-and-devicetree.md` | 8-12, 14-31, 34 | SPI, I2C, UART, ADC, PWM, Counter, Watchdog, RTIO, asynchronous ownership, native escape hatches, Kconfig, target validation, and Device integration are landed. |
| `00-design-conventions.md` | static typed surface, no hidden allocation or execution, Kconfig inclusion | Hardware remains usable without a System and is not a component or facility. |
| `00a-modern-cpp-result-and-status.md` | `Result<T, hardware::Error>`, errno preservation, move-only resource handles | Runtime capability and driver failures remain explicit. |
| `03-lifecycle-kernel-and-configuration.md` | Zephyr-native Kconfig and application Device boundary | Hardware itself has no lifecycle; `DebugUartDevice` chooses to make readiness part of application lifecycle. |
| `12-health-and-supervision.md` | Hardware/Device boundary needed by later Health integration | The migrated Device exposes focused readiness and canonical `last_error`; Health storage and assessment remain Stage 18. |

Direct DMA, later driver families, active Health assessment, and Supervisor
watchdog policy remain in their accepted later boundaries. They are not hidden
inside Hardware.

## 3. Public Surface Landed

`solar/hardware.hpp` now conditionally aggregates:

- `spi::Endpoint`, `spi::Controller`, `spi::Session`, callback Operations, and
  native RTIO endpoints;
- `i2c::Endpoint`, `i2c::Controller`, callback Operations, register/burst
  conveniences, and native RTIO endpoints;
- `uart::Port`, `uart::Polling`, `uart::InterruptDriven`, and `uart::Async`;
- `adc::Channel`, caller-buffered `adc::Sequence`, asynchronous reads, and
  RTIO `adc::Stream` configuration;
- `pwm::Output`, validated `DutyCycle`, and callback/synchronous
  `pwm::Capture`;
- `counter::Counter`, fixed-channel `counter::Alarm`, and `counter::Top`;
- `watchdog::Device`, validated `watchdog::Timeout`, and move-only dynamically
  assigned watchdog channels;
- caller-owned `rtio::Context` and move-only RAII completion entries.

Generated addressed endpoints use the same compact board shape as GPIO:

```cpp
using ImuBus = solar::hardware::spi::Endpoint<
    solar::hardware::dt::alias<"imu">>;

std::array<std::byte, 8> request{};
std::array<std::byte, 8> response{};
SOLAR_TRY(ImuBus::transceive(request, response));
```

Multi-channel ADC remains caller-buffered:

```cpp
using Battery = solar::hardware::adc::Channel<
    solar::hardware::dt::alias<"battery">>;
using Samples = solar::hardware::adc::Sequence<Battery>;

std::array<std::byte, sizeof(std::int16_t)> storage{};
SOLAR_TRY(Samples::setup());
SOLAR_TRY(Samples::read(storage));
```

RTIO is optional and preserves native queue ownership:

```cpp
RTIO_DEFINE(io, 8, 8);
solar::hardware::rtio::Context context{io};

rtio_sqe* last{};
auto count = ImuBus::Rtio::copy(context, transmit, receive, last);
```

The application board header now exposes `board::DebugUart` from generated
`dt::chosen` or `dt::node_label` facts. `DebugUartDevice` owns the semantic
lifecycle readiness boundary; Remote consumes the same native UART through the
board alias.

## 4. Runtime Ownership

| Owner | Storage/resource | Capacity | Synchronization | Lifetime |
| --- | --- | --- | --- | --- |
| Zephyr drivers | controller state, DMA/interrupt machinery, configured devices, native callbacks | driver/devicetree-defined | native driver contract | image lifetime |
| SPI/I2C RTIO endpoint type | copied immutable native endpoint spec and `rtio_iodev` | one per odr-used endpoint type | none after initialization | image lifetime |
| ADC Stream type | immutable channel/trigger arrays, read configuration, and `rtio_iodev` | declaration-defined | none after initialization | image lifetime |
| caller | SPI/I2C callback Operation, ADC async Operation, buffers, RTIO queues/pools, and completion processing | caller-selected | generation gate plus native driver/RTIO synchronization | caller-owned |
| physical UART endpoint core | one callback owner and handler publication per native UART device | one interrupt or async callback role | atomic publication plus Zephyr callback contract | image lifetime |
| PWM/Counter role type | persistent native callback bridge and function pointer | one owner per typed endpoint/channel role | atomic callback publication | image lifetime |
| watchdog caller | move-only channel identifier returned by driver timeout installation | driver-selected | driver contract | caller-owned handle |
| application `DebugUartDevice` | one atomic last status | one | atomic acquire/release | image lifetime |

Hardware creates no thread, stack, service, executor, work item, workqueue,
timer, heap allocation, dynamic registry, or lifecycle storage. RTIO queues,
completion queues, and optional memory pools are explicitly defined and owned
by the caller. Normal Zephyr driver-managed DMA remains entirely driver-owned.

Type-owned native specs and callback bridges are constant-initialized. No
constructor registration or cross-translation-unit dynamic initialization was
introduced.

## 5. Compile-Time Behavior

The EDT generator now recognizes and emits structural descriptors for:

- addressed SPI and I2C child nodes;
- UART, Counter, and Watchdog controller devices;
- ADC `io-channels` and PWM `pwms` property endpoints;
- enabled family-specific native specs only when both Solar and Zephyr driver
  Kconfig make the family usable.

Classification uses the immediate bus parent, not inherited bus ancestry. This
prevents a nested child such as a flash partition from being misclassified as
an addressed SPI endpoint.

Compile-time validation covers descriptor family mismatch, disabled async or
capture capabilities, and ADC Sequence controller mismatch. Family types also
validate resolution, oversampling, channel-mask range, endpoint identity, and
role contracts where facts are constexpr.

Counter alarm-channel count is exposed by Zephyr only through a runtime driver
call. Solar therefore validates the fixed channel on `Alarm::set()` rather
than inventing a compile-time controller count. Identical
`Alarm<Counter, Channel>` uses the same type-owned callback state, so aliases
cannot create a second independent owner for the same typed channel.

The Stage 17 stable diagnostics extend the Hardware suite to 10 cases,
including UART/SPI/I2C family mismatch, disabled callback capabilities, PWM
capture exclusion, and ADC Sequence controller mismatch.

## 6. Error And Availability Behavior

All fallible family operations return `Result<T, hardware::Error>`. Errors
preserve:

- Solar status;
- focused Hardware reason and operation;
- original Zephyr return value;
- resolved endpoint path where available.

The mapping distinguishes not ready, unsupported, invalid configuration, busy,
timeout, cancellation, resource exhaustion, physical role ownership, stale
completion, and driver failure. `-ENOSYS` now maps to `NotSupported` instead of
a generic failure.

Examples of focused behavior include:

- ADC Sequence rejects an undersized caller buffer with `NoBuffer`;
- I2C RTIO rejects an empty or oversized native message chain as invalid;
- callback Operations reject concurrent reuse and release admission after a
  failed submission;
- UART rejects a second callback-owning API model on the same device;
- PWM rejects duty cycles above 100 percent instead of silently clamping;
- moved-from watchdog channels reject feed as invalid;
- unavailable optional driver calls return `Unsupported` with the native
  value retained;
- RTIO acquisition and family copy report fixed-pool exhaustion without
  allocation.

## 7. Zephyr Integration

The implementation uses Zephyr's public:

- `spi_dt_spec`, synchronous, callback, release, and SPI RTIO APIs;
- `i2c_dt_spec`, transfer/register/recovery, callback, and I2C RTIO APIs;
- polling, interrupt-driven, asynchronous, line-control, and error UART APIs;
- `adc_dt_spec`, sequence, poll-signal async, stream, decoder, and default ADC
  RTIO APIs;
- PWM output and capture APIs;
- Counter value, alarm, top, conversion, and interrupt APIs;
- Watchdog installation, setup, feed, and disable APIs;
- RTIO SQE/CQE, submission, cancellation, iodev, and completion release APIs.

Relevant Solar Kconfig mirrors compiled Zephyr capabilities without restating
devicetree facts:

- `CONFIG_SOLAR_HARDWARE_{SPI,I2C,UART,ADC,PWM,COUNTER,WATCHDOG}`;
- focused SPI/I2C/ADC async options;
- UART interrupt and async options;
- PWM capture;
- native RTIO adapters when SPI RTIO, I2C RTIO, or ADC Stream exists.

Teensy target validation uses an NXP LPSPI controller with EDT `dmas` and
`dma-names`. The linked build selects Zephyr's
`spi_nxp_lpspi_dma.c`, proving that ordinary Solar SPI calls preserve the
normal driver-managed DMA route without a Solar DMA abstraction.

Native Zephyr emulator controllers cannot host conventional addressed child
fixture nodes through the normal forwarding topology without triggering a DTC
binding assertion. Native behavior tests therefore use explicit structural
SPI/I2C descriptors over the real emulator controllers, while Teensy compiles
and links conventional generated addressed child aliases. This keeps both the
runtime driver behavior and real EDT shape covered.

## 8. Files Changed

### Added

- `include/solar/hardware/{spi,i2c,uart,adc,pwm,counter,watchdog,rtio}.hpp`
- `tests/zephyr/hardware_drivers/`
- `tests/zephyr/hardware_target_compile/`
- Stage 17 cases in `tests/zephyr/hardware_compile_fail/`
- `firmware/include/board/hardware.hpp`

### Reshaped

- `include/solar/hardware/{dt,endpoint,error,types}.hpp` and
  `include/solar/hardware.hpp`
- `tools/hardware/generate_devicetree.py`
- `zephyr/Kconfig`
- `tests/zephyr/check_hardware_{compile_fail,headers}.py` coverage inputs
- `firmware/include/app/robot.hpp`, `firmware/prj.conf`, board configuration,
  and firmware documentation

### Removed

- firmware's direct Zephyr `include/system/board.hpp` selection path; generated
  Solar Hardware aliases now own board endpoint naming.

## 9. Tests And Evidence

| Command | Platform/configuration | Result | What it proves |
| --- | --- | --- | --- |
| `west twister -T tests/zephyr/hardware_foundation -T tests/zephyr/hardware_explicit -T tests/zephyr/hardware_drivers -T tests/zephyr/hardware_target_compile --inline-logs --integration` | native_sim 64 and Teensy 4.0 build-only | 5/5 configurations, 12/12 executed cases, no warnings | Generated/explicit foundations, every driver family, RTIO iodevs, native behavior, and target compilation. |
| direct `hardware_drivers` build and execution | native_sim 64 with SPI/I2C/ADC RTIO | 7/7 | Caller-owned RTIO completion release, family request copying, ADC stream construction, async reads, callback ownership, and driver errors. |
| `check_hardware_compile_fail.py` | Zephyr C++23 compile database | 10/10 expected failures | Stable family, capability, and sequence diagnostics. |
| `check_hardware_headers.py` | Zephyr C++23 | 15/15 headers | Every public Hardware header is self-contained. |
| host build plus `ctest` | GCC 13, C++23 | 57/57 | No host core, binding, catalog, protocol, or SDK regression. |
| `west twister -T tests/zephyr --inline-logs --integration` | complete Solar Zephyr matrix | 83/83 selected configurations; 81 executed plus 2 target build-only; 259/259 cases; no warnings in 994.07 s | No regression across every landed Solar stage. |
| firmware native and Teensy builds | native_sim 64 and Teensy 4.0 | pass | Generated board alias, Device lifecycle dependency, Remote link reuse, and physical target compile/link. |
| generated client against native firmware | native PTY UART | pass | The migrated board/Device path still provides a live Remote handshake, Query, response, decode, and ACK. |
| `git diff --check` | both repositories | pass | No whitespace errors. |

The Teensy firmware reports 151,544 B flash (7.23 percent) and 62,144 B RAM
(23.71 percent). GNU section totals are 126,014 B text, 25,528 B data, and
36,589 B bss. Native firmware totals are 199,974 B text, 27,976 B data, and
40,424 B bss.

The driver fixture generated 53 selectors; the Teensy target fixture generated
504 selectors including PWM, SPI, I2C, UART, Counter, and Watchdog families.
The corresponding target C++ header is 322,915 B and JSON EDT summary is
146,042 B. These are build artifacts, not runtime allocations.

No Teensy was attached to this workspace, so flashing and physical GPIO, UART,
bus, timing, and watchdog execution could not run. The stage records that
environmental limit explicitly. Real-board EDT, toolchain, driver selection,
DMA source selection, compile, and link gates all passed; destructive watchdog
runtime behavior remains part of the final attached-hardware gate in Stage 20.

## 10. Specification Refinements

Observed contract: native_sim's SPI/I2C emulator topology cannot accept the
same ordinary addressed child fixture shape used by a physical controller.

Evidence: adding a conventional child under the native forwarding controller
caused devicetree binding processing to fail before application compilation.

Accepted change: use permanent explicit structural descriptor construction for
native emulator behavior tests and generated conventional child descriptors on
the Teensy target.

Specifications updated: none. Spec 13 already makes explicit descriptors a
permanent supported path and requires native plus target evidence.

Verification added: native sync/error and RTIO-copy tests plus Teensy generated
SPI/I2C alias compile/link tests.

Observed contract: Zephyr exposes Counter channel count through the runtime
driver API, not a portable constexpr controller fact.

Accepted change: validate fixed alarm channel range before native alarm setup;
retain type-owned callback identity for duplicate typed ownership.

Specifications updated: none. Spec 13 requires compile-time rejection only
where ownership or validity can be proven.

## 11. Firmware And Host Impact

Firmware now names its selected debug UART through `board::DebugUart`. The
composition root contains `DebugUartDevice`, and `RobotApplication` depends on
that Device. Its `init()` makes controller readiness part of boot and its
`deinit()` resets semantic readiness. The existing Remote Link takes the
native handle from the same typed alias instead of selecting devicetree itself.

The native firmware and generated host client remain interoperable. Host-only
Solar builds remain unaffected because Hardware is a Zephyr-only Kconfig
surface.

## 12. Known Limits And Deferred Work

- Direct generic DMA remains deliberately absent; project-specific advanced
  DMA may use native Zephyr or SoC APIs.
- I2C target mode, extra driver families, and generic device power management
  remain optional future focused wrappers.
- Native simulation does not exercise physical PWM capture, UART electrical
  behavior, or watchdog reset. Target compile/link coverage is present and
  attached-hardware execution is retained by Stage 20.
- Health assessment of `DebugUartDevice` and other Devices belongs to Stage 18;
  active response and feed policy belongs to Stage 19.

## 13. Documentation Handoff

The later public pass should explain:

- generated board aliases and the explicit descriptor fallback;
- the Hardware-versus-Device responsibility boundary;
- caller buffer and Operation lifetimes for every async API;
- UART callback role exclusivity;
- ADC Sequence and RTIO Stream ownership;
- RTIO queue, pool, cancellation, and completion-release rules;
- driver-managed DMA and why no generic direct-DMA API exists;
- native-handle escape hatches and target-specific capability errors;
- watchdog installation versus later supervisory feed policy.

The Hardware driver fixtures should be the source for executable examples.

## 14. Closure Statement

All required initial driver families, generated descriptors, asynchronous and
RTIO boundaries, native escape hatches, firmware alias migration, Device
lifecycle boundary, native behavior, target compile/link, and full Solar
regression gates are present and green. Stage 17 is complete and unblocks
Stage 18 Health.
