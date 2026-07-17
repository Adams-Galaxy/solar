# Hardware And Devicetree

Date: 2026-07-15

Status: accepted design

Owning phase: Phase 13

Depends on:

- `00-design-conventions.md`
- `00a-modern-cpp-result-and-status.md`
- `01-system-blueprint-and-binding.md`
- `02-identity-contributions-and-catalogs.md`
- `03-lifecycle-kernel-and-configuration.md`
- `09-tasks-and-executors.md`
- `11-inspection.md`
- `12-health-and-supervision.md`

## 1. Purpose

This specification defines `solar::hardware`, a typed, static, C++23 hardware
layer built with Zephyr's device model, devicetree, pin control, driver APIs,
and build system.

Hardware makes ordinary embedded operations concise and type-safe without
creating a competing HAL. Zephyr remains responsible for describing,
configuring, constructing, initializing, and driving physical hardware.

The design establishes:

- devicetree as the canonical hardware description;
- pinctrl as the canonical pin-routing and electrical configuration system;
- Zephyr devices and drivers as the canonical runtime hardware owners;
- static typed wrappers for GPIO, SPI, I2C, UART, ADC, PWM, Counter, and
  Watchdog;
- compact board-level aliases suitable for direct global use;
- structural C++23 endpoint descriptors derived from Zephyr spec structures;
- an EDT-driven generated descriptor catalog and build-time diagnostics;
- synchronous, interrupt-driven, asynchronous, and RTIO pathways where the
  underlying Zephyr driver supports them;
- explicit callback, buffer, operation, controller, and channel ownership;
- transparent use of driver-managed DMA;
- direct Zephyr interoperation for uncommon or platform-specific facilities;
- a clean boundary between raw hardware access and Solar Device integration;
- no hidden threads, queues, allocation, initialization, or execution policy.

The ordinary board path is compact:

```cpp
namespace board
{
using StatusLed = solar::hardware::gpio::Output<
    solar::hardware::dt::alias<"status-led">>;

using ImuBus = solar::hardware::spi::Endpoint<
    solar::hardware::dt::alias<"imu">>;
}
```

Ordinary use is static:

```cpp
SOLAR_TRY(board::StatusLed::write(true));
SOLAR_TRY(board::ImuBus::transceive(tx, rx));
```

Objects appear only where an independently owned operation, callback,
transaction, lease, buffer, or synchronization lifetime requires one.

## 2. Non-Goals

Hardware is not:

- a replacement for Zephyr's device model;
- a replacement for devicetree, overlays, bindings, or pinctrl;
- a manually maintained MCU pin map;
- a second driver framework;
- a universal runtime hardware registry;
- a lifecycle facility or system component;
- a duplicate of the application System graph;
- a requirement that board hardware appear in the System blueprint;
- an owner of application Devices;
- an execution scheduler or callback worker;
- a promise that every Zephyr driver supports every API mode;
- a portable abstraction over arbitrary direct DMA programming;
- a mechanism for inferring application meaning from node names;
- automatic generation of Solar Devices from devicetree nodes;
- a reason to conceal native Zephyr handles, return details, or restrictions;
- a reason to copy resolved devicetree into runtime storage;
- a universal hardware snapshot;
- a mandate to wrap every niche or unstable Zephyr driver API.

Hardware wrappers must add useful typing, ownership, units, validation, or
ergonomics. A wrapper that only renames one C function without improving its
contract is not automatically justified.

## 3. Vocabulary

- **node**: one resolved devicetree node;
- **node identifier**: Zephyr's compile-time token identifying one node;
- **endpoint**: one application-facing addressed peripheral or hardware
  resource described by a node or spec;
- **controller**: a shared hardware controller such as an SPI, I2C, DMA, ADC,
  or Counter device;
- **spec**: a structural compile-time descriptor containing the resolved data
  needed by one wrapper;
- **role wrapper**: a restricted view such as GPIO Input or Output;
- **board alias**: an application-owned semantic type name for a physical
  endpoint;
- **generated catalog**: deterministic C++ declarations derived from the
  resolved EDT for the current image;
- **operation**: caller-owned state for one asynchronous request;
- **buffer lease**: temporary exclusive ownership of storage until an
  asynchronous request completes or cancellation is confirmed;
- **callback owner**: the one type or object authorized to install and retain a
  driver callback for an endpoint;
- **controller authority**: permission to perform operations affecting every
  endpoint on a shared controller;
- **native handle**: the underlying Zephyr device, spec, callback, signal, or
  driver value exposed for direct interoperation;
- **driver-managed DMA**: DMA selected and configured internally by a Zephyr
  peripheral driver;
- **direct DMA**: application or driver code programming the generic Zephyr DMA
  API itself;
- **RTIO**: Zephyr's bounded submission and completion framework for supported
  I/O devices;
- **canonical truth**: the subsystem whose state is authoritative rather than
  copied into Solar-owned storage.

## 4. Architectural Boundary

### 4.1 Responsibility layers

```text
 devicetree + overlays + bindings + pinctrl + Kconfig
                         |
                         v
              Zephyr devices and drivers
                         |
                         v
                 solar::hardware
          typed primitives and descriptors
                         |
                         v
                 project board aliases
                         |
                         v
                application Devices
       lifecycle, health, behavior, recovery
```

Each layer has one deliberate responsibility.

Zephyr owns:

- hardware discovery and description;
- devicetree resolution and binding validation;
- pin multiplexing and pinctrl states;
- driver configuration and device construction;
- initialization ordering;
- runtime driver state;
- peripheral-specific DMA integration;
- power-management integration provided by drivers.

`solar::hardware` owns:

- typed compile-time endpoint descriptors;
- role-specific static APIs;
- C++23 concepts and diagnostics;
- `Result<T>` translation that preserves native error detail;
- units, spans, bounded values, and ownership vocabulary;
- callback and asynchronous operation scaffolding;
- generated access to resolved EDT identities and properties;
- explicit native escape hatches.

Project board declarations own:

- semantic names such as `StatusLed`, `ImuBus`, and `BatteryVoltage`;
- intentional selection of hardware roles;
- any board-specific composition not naturally represented by one endpoint;
- optional adaptations for platform-specific behavior.

Application Devices own:

- component lifecycle;
- sensor and actuator protocols;
- domain state and synchronization;
- health assessment and recovery;
- execution policy and task registration;
- Events, Metrics, Parameters, Logging, Remote, and Inspection participation.

### 4.2 Hardware is not a facility

`solar::hardware` is a namespace of standalone compile-time wrappers and
focused operation objects. It is not a built-in Facility and is not included
in the System blueprint.

Hardware can be used before a System type is declared, by code that has no
System, and in tests that bind no Solar application. This mirrors
`solar::kernel`: Kernel wraps operating-system primitives while Execution adds
system integration; Hardware wraps physical primitives while Devices add
system integration.

### 4.3 No automatic graph participation

Hardware endpoints do not automatically become components, graph nodes,
Lifecycle records, Health subjects, or Inspection entries.

The application Device that owns or uses an endpoint is the meaningful graph
component. Focused generated hardware metadata may support build diagnostics
and board inspection tools, but it does not create runtime system ownership.

### 4.4 Direct Zephyr use

Applications should use Zephyr directly when:

- Solar has no wrapper for the driver family;
- the API is highly SoC-specific;
- direct DMA programming is genuinely required;
- driver development requires structures Solar should not abstract;
- a native API exposes a capability absent from the focused Solar wrapper;
- interoperation is clearer than extending the public Solar surface.

Direct use is supported, not treated as contamination. Every wrapper with a
meaningful native representation exposes it explicitly.

## 5. Canonical Devicetree Model

### 5.1 Source of truth

The final resolved Zephyr devicetree is canonical. Solar does not maintain a
second pin map, peripheral map, interrupt table, or DMA routing database.

Board DTS files and overlays select:

- enabled controllers and child devices;
- pinctrl states and electrical configuration;
- GPIO controllers, pins, and flags;
- bus addresses and chip-select lines;
- SPI modes and maximum frequencies;
- ADC inputs and acquisition settings;
- PWM channels and flags;
- interrupts and priorities;
- DMA channels or request cells consumed by drivers;
- aliases and chosen nodes used by application code.

Changing a board or overlay changes the resolved endpoint descriptors at build
time. Application modules should not require scattered `#if` branches merely
to account for a board variant.

### 5.2 Zephyr initialization

Zephyr initializes enabled device drivers according to its device dependency
and initialization model. Hardware wrappers do not expose a misleading
universal `init()` that repeats that process.

Wrappers expose readiness and configuration operations appropriate to the
driver family:

```cpp
Endpoint::ready();
Endpoint::require_ready();
Endpoint::configure(...); // only where the driver API defines configuration
```

`configure()` means configure the endpoint through the driver. It does not
mean construct or initialize the Zephyr device.

### 5.3 Pinctrl

Pinctrl remains responsible for peripheral pin selection and alternate
functions. Solar does not recreate STM32-style alternate-function binding
tables.

Where Zephyr exposes runtime pinctrl state selection, a focused wrapper may
provide typed state application. It must operate on Zephyr pinctrl data and
must not duplicate it.

## 6. C++23 Descriptor Model

### 6.1 Structural specifications

Zephyr 4.4 spec structures used by GPIO, SPI, I2C, ADC, and PWM are suitable
inputs for C++23 structural non-type template parameters in the target
toolchain. Solar may normalize them into small structural descriptor values
where normalization improves concepts or diagnostics.

The explicit form remains valid:

```cpp
inline constexpr auto DebugLedSpec = solar::hardware::dt::gpio(
    GPIO_DT_SPEC_GET(DT_ALIAS(debug_led), gpios));

using DebugLed = solar::hardware::gpio::Output<DebugLedSpec>;
```

`dt::gpio`, `dt::spi`, `dt::i2c`, `dt::adc`, and `dt::pwm` are `consteval`
factories or equivalent descriptor constructors. They preserve the native spec
and add no runtime lookup.

### 6.2 Device-only endpoints

Driver families without a standard Zephyr `*_dt_spec`, including UART,
Counter, and Watchdog, use a typed device descriptor derived from a node:

```cpp
using Console = solar::hardware::uart::Polling<
    solar::hardware::dt::device<DT_CHOSEN(zephyr_console)>>;
```

The exact implementation may hold a `const device*` or a generated node-backed
descriptor, but it must retain compile-time node identity for diagnostics.

### 6.3 Descriptor facts

A descriptor exposes only facts that are authoritative from devicetree and its
binding, such as:

- endpoint kind;
- node status;
- compatible identity;
- controller identity;
- channel, address, chip select, pin, and flags;
- fixed configuration represented by the standard Zephyr spec;
- source path, alias, or node label for diagnostics;
- property presence required by the wrapper.

It does not claim that every function in a driver's API table is implemented.
Runtime driver support may still produce `NotSupported`.

### 6.4 Compile-time validation

Concepts and `static_assert` diagnostics reject information that can be proved
invalid at compile time:

- missing or disabled nodes;
- applying a GPIO wrapper to a non-GPIO descriptor;
- applying an SPI endpoint wrapper to an I2C node;
- absent required properties;
- invalid fixed channel indices;
- ADC sequences spanning incompatible controllers;
- contradictory role or configuration types;
- callback modes disabled by required Kconfig options.

Diagnostics should identify the wrapper, endpoint identity, and failed
requirement. Solar does not convert a compile-time fact into a late boot error.

## 7. EDT-Driven Generation

### 7.1 Input

Zephyr produces `zephyr/edt.pickle` for each image after resolving DTS files,
overlays, bindings, aliases, chosen nodes, phandles, and property types. The
pickle contains the public `edtlib.EDT` model used by Zephyr's own generators.

The Solar Zephyr module adds a deterministic Python generation step that reads
the image's `edt.pickle` through public `edtlib` APIs. It does not parse
`devicetree_generated.h` and does not independently reinterpret raw DTS text.

The generator is version-coupled to the supported Zephyr release by design.
Solar tracks modern Zephyr releases and tests this boundary explicitly.

### 7.2 Output

The generator emits a build-local header under the generated include tree,
conceptually:

```text
build/zephyr/include/generated/solar/hardware/devicetree.hpp
```

The generated catalog may contain:

- descriptors for aliases;
- descriptors for chosen nodes;
- descriptors for explicitly addressable node labels;
- stable source identities and diagnostic strings;
- endpoint kind and controller relationships;
- binding-derived compile-time facts;
- generated specializations supporting string-like alias lookup;
- a build-time hardware summary for diagnostics and tooling.

It contains no mutable runtime database and causes no firmware storage unless
the application odr-uses emitted data.

### 7.3 Generated public form

The desired ergonomic form is:

```cpp
using StatusLed = solar::hardware::gpio::Output<
    solar::hardware::dt::alias<"status-led">>;
```

The exact fixed-string and specialization machinery may be refined during
implementation. The accepted contract is that generated aliases are compact,
typed, deterministic, and validated against the requested wrapper family.

The explicit Zephyr macro form remains permanently supported. Generated lookup
is a polished frontend, not the only foundation on which wrappers can operate.

### 7.4 Generation scope

Generation may:

- remove repetitive `DT_ALIAS`, `DT_NODELABEL`, and `*_DT_SPEC_GET` syntax;
- provide better build diagnostics;
- expose resolved endpoint and controller relationships;
- generate deterministic board inventories for tools and tests;
- validate Solar-specific requirements against binding-derived facts;
- make board variants naturally select different descriptors.

Generation must not:

- generate application Devices;
- infer lifecycle or Health participation;
- infer semantic ownership from an alias alone;
- create system blueprint entries;
- configure drivers independently of Zephyr;
- infer unsupported runtime driver capabilities;
- expose every enabled node as application API by default;
- synthesize a portable direct-DMA model from SoC-specific cells;
- make firmware behavior depend on Python at runtime.

### 7.5 Selection and naming

Aliases and chosen nodes are the primary generated application-facing
selection mechanisms because they are explicit board contracts. Node labels
may be generated for lower-level board work. Arbitrary enabled nodes remain
available through explicit Zephyr node identifiers but are not all promoted
into a polished API.

Generated names preserve the DTS spelling. Applications own domain-facing C++
names in a board namespace:

```cpp
namespace board
{
using StatusLed = hardware::gpio::Output<hardware::dt::alias<"status-led">>;
using Battery = hardware::adc::Channel<hardware::dt::alias<"battery">>;
}
```

The generator does not guess that `status-led` should be named `FaultLamp` in
one robot and `ReadyIndicator` in another.

### 7.6 Build integration

The generation command:

- runs once per Zephyr image and build configuration;
- depends on that image's `edt.pickle`;
- regenerates when DTS inputs, overlays, or bindings change;
- uses Zephyr's configured Python environment;
- writes only into the build tree;
- produces deterministic output suitable for incremental builds;
- reports malformed Solar-facing aliases during configuration or generation;
- supports sysbuild by treating each image independently.

A Kconfig option may disable the generated convenience catalog for minimal or
special build environments. Explicit descriptors continue to work. Generation
is enabled by default whenever Solar Hardware is enabled.

### 7.7 No code-size optimization claim

The generated catalog improves source ergonomics, diagnostics, and tooling. It
is not primarily a firmware-size optimization. Header-only templates already
emit no operation code until used, while Zephyr Kconfig and devicetree control
driver inclusion.

## 8. Common Wrapper Contract

Every endpoint family should use a recognizable baseline where meaningful:

```cpp
Endpoint::ready() -> bool;
Endpoint::require_ready() -> Result<void>;
Endpoint::native_handle();
```

Additional common rules are:

- public methods are static for singular type-identified endpoints;
- inputs and outputs use `std::span` rather than pointer-length pairs;
- time uses `std::chrono` durations where the native API permits it;
- bounded values and enums replace untyped flag combinations where useful;
- no heap allocation is required;
- native errno or driver detail remains recoverable from `solar::Error`;
- context restrictions are documented and represented in method naming or
  concepts where practical;
- unsupported operations return an explicit error rather than pretending to
  succeed;
- native handles are available without breaking wrapper ownership.

`ready()` only reports Zephyr device readiness. It does not imply that a
sensor is connected, configured, calibrated, responsive, or healthy.

## 9. Static State And Ownership

### 9.1 Stateless endpoints

Simple endpoint operations require no Solar-owned state:

```cpp
StatusLed::write(true);
Button::read();
ImuBus::transceive(tx, rx);
```

The endpoint type holds only compile-time descriptor data and delegates runtime
state to the Zephyr driver.

### 9.2 Type-owned endpoint state

Type-owned static state is appropriate when the underlying API permits only
one callback registration or requires persistent callback metadata, including:

- GPIO interrupt callbacks;
- UART interrupt-driven or asynchronous callback dispatch;
- PWM capture callbacks;
- Counter alarm callbacks;
- endpoint-local synchronization when Solar must serialize access.

Aliases resolving to the same endpoint must not accidentally create multiple
independent owners. Role wrappers delegate endpoint-wide state to one
descriptor-keyed internal core.

### 9.3 Operation objects

Caller-owned objects are required when multiple independent requests or
lifetimes can exist:

- asynchronous SPI or I2C submissions;
- UART transmit and receive operations;
- ADC asynchronous reads or streams;
- RTIO contexts and submissions;
- explicit bus transactions;
- cancellation state and completion tokens.

Operation objects are statically allocatable, movable only where movement is
safe, and never require hidden dynamic allocation.

## 10. Concurrency And Shared Controllers

Zephyr driver thread-safety remains authoritative. Solar does not claim that a
driver is safe for concurrent calls unless Zephyr provides that contract or
Solar explicitly serializes the endpoint.

Shared-controller rules are:

- addressed SPI and I2C endpoints perform ordinary transactions;
- controller-wide configuration and recovery require an explicit Controller
  type;
- endpoint wrappers do not casually reconfigure a shared controller;
- locked or chip-select-held SPI sequences use an explicit Session;
- buffer and callback ownership remains bounded for the duration of a call;
- wrapper locks are never held while invoking arbitrary application callbacks;
- ISR-safe operations are a documented subset;
- blocking APIs are not callable from ISR merely because their C function is
  visible;
- no wrapper silently submits work to the system workqueue.

Where a driver already serializes transfers, Solar should not add a redundant
mutex. Where stronger ownership is required, policy is explicit in the wrapper
or board alias.

## 11. Asynchronous Operation Contract

Supported asynchronous endpoints share one philosophy, not necessarily one
base class.

```cpp
static inline board::ImuBus::Operation transfer;
static inline std::array<std::byte, 64> rx;

auto receipt = transfer.submit(request, rx, completion);
```

The contract is:

- the caller owns the Operation and every referenced buffer;
- buffers remain leased until completion or confirmed cancellation;
- an Operation cannot be reused while active;
- submission distinguishes accepted, busy, unsupported, and failed;
- completion occurs exactly once for an accepted generation;
- stale callbacks cannot complete a reused generation;
- cancellation is complete only when the driver confirms buffer release;
- callback execution context is explicit;
- driver callbacks perform bounded work;
- thread handoff uses Kernel primitives or an explicitly selected Executor;
- Hardware creates no worker, service, thread, or workqueue;
- synchronous calls may be implemented directly rather than through the async
  machinery.

An endpoint may expose only synchronous operations, only selected async
operations, or both. The wrapper remains useful when capabilities are partial.

## 12. RTIO

RTIO is a first-class optional Zephyr-backed path for driver families that
support it, especially SPI, I2C, and ADC.

`solar::hardware::rtio` may provide typed C++ ownership over:

- statically sized submission and completion queues;
- I/O device descriptors;
- submission entries and chained transactions;
- completion tokens;
- cancellation;
- optional fixed memory pools.

RTIO remains a Kernel-adjacent I/O primitive within Hardware because its
endpoints are hardware driver operations. It does not become an Executor and
does not register application Tasks.

Ordinary peripheral APIs are not forced through RTIO. Devices choose RTIO when
queued, chained, or high-throughput I/O justifies it.

## 13. GPIO

### 13.1 Types

```cpp
gpio::Pin<Spec>
gpio::Input<Spec, Options...>
gpio::Output<Spec, Initial, Options...>
gpio::Interrupt<Spec, Trigger, Options...>
gpio::Port<Device>
```

`Pin` is the unrestricted focused primitive. Role wrappers intentionally
restrict invalid operations. `Port` provides advanced masked and bulk port
operations.

### 13.2 Surface

GPIO covers:

- readiness;
- input and output configuration;
- active/inactive logical reads and writes;
- raw physical reads and writes where polarity matters;
- set, reset, and toggle;
- pull, drive, open-drain, open-source, and disconnect flags supported by
  Zephyr;
- interrupt trigger configuration;
- enable and disable;
- callback installation and removal;
- pending interrupt queries where supported;
- masked port reads and writes;
- native `gpio_dt_spec`, port device, pin, flags, and callback access.

Logical methods respect devicetree active-low flags. Raw methods are named
explicitly.

### 13.3 Interrupt ownership

One `gpio::Interrupt` endpoint owns one persistent `gpio_callback`. The driver
callback runs in its native context and may:

- update an atomic value;
- release a Kernel semaphore or event;
- enqueue bounded ISR-safe ingress;
- submit explicitly selected work through an ISR-safe API.

It must not run arbitrary blocking Device behavior.

## 14. SPI

### 14.1 Types

```cpp
spi::Endpoint<Spec>
spi::Controller<Device>
spi::Session<Endpoint>
Endpoint::Operation
```

The endpoint spec includes the controller, operation flags, frequency, slave
selection, and Zephyr-managed chip select represented by `spi_dt_spec`.

### 14.2 Surface

SPI covers:

- readiness;
- write, read, and full-duplex transceive;
- `std::span` convenience overloads;
- native `spi_buf_set` access for scatter/gather and unequal shapes;
- synchronous operations;
- callback or poll-signal asynchronous operations where enabled;
- RTIO submission where supported;
- explicit release;
- native endpoint and controller handles.

`Session` represents controller lock, held chip select, or a deliberately
chained transaction. Its lifetime and release behavior are explicit. Ordinary
single transactions need no Session object.

Solar does not move frequency, mode, or chip-select truth out of devicetree.
Runtime overrides, where allowed, are explicit configuration values rather
than hidden mutations of the endpoint descriptor.

## 15. I2C

### 15.1 Types

```cpp
i2c::Endpoint<Spec>
i2c::Controller<Device>
Endpoint::Operation
```

`Endpoint` represents the normal bus-plus-address child described by
`i2c_dt_spec`. `Controller` represents shared-bus authority.

### 15.2 Surface

Endpoint operations include:

- read and write;
- write-then-read;
- multi-message transfer;
- register and burst conveniences built on transfer primitives;
- synchronous operations;
- callback or poll-signal asynchronous operations where supported;
- RTIO submission;
- native endpoint access.

Controller operations may include:

- controller configuration and current configuration queries;
- bus recovery;
- target mode registration;
- controller-wide diagnostics;
- native controller access.

Bus recovery and controller configuration do not belong on every addressed
endpoint because they affect sibling devices.

## 16. UART

### 16.1 Types

```cpp
uart::Port<Device>
uart::Polling<Device>
uart::InterruptDriven<Device, Options...>
uart::Async<Device, Options...>
```

Zephyr's polling, interrupt-driven, and asynchronous UART APIs have different
ownership and callback models. Role types make the selected model visible and
share descriptor-keyed endpoint state.

### 16.2 Surface

UART covers:

- readiness;
- runtime configuration get and set;
- polling input and output;
- error checks;
- interrupt FIFO reads and writes;
- interrupt enable, disable, and readiness controls;
- asynchronous transmit;
- transmit abort;
- asynchronous receive enable and disable;
- receive buffer request and release events;
- line control where supported;
- native device and callback event access.

Only one callback-owning UART role controls an endpoint at a time. Buffer
ownership follows the shared asynchronous contract. Hardware does not provide
a hidden RX thread, mailbox, or parser.

## 17. ADC

### 17.1 Types

```cpp
adc::Channel<Spec>
adc::Sequence<Channels...>
adc::Stream<Configuration>
```

### 17.2 Surface

ADC covers:

- controller readiness;
- channel setup from `adc_dt_spec`;
- one-channel sampling;
- caller-buffered multi-channel sequences;
- synchronous and poll-signal asynchronous reads;
- RTIO streaming where supported;
- raw sample access;
- raw-to-millivolt or other supported conversion;
- acquisition time, reference, gain, resolution, and oversampling values;
- native channel and sequence access.

`Sequence<Channels...>` validates that its channels can participate in one
controller operation. It does not allocate its sample buffer.

Physical engineering-unit conversion beyond what the ADC binding can prove is
owned by the board alias or application Device. Hardware does not guess voltage
dividers, shunts, calibration curves, or sensor transfer functions.

## 18. PWM

### 18.1 Types

```cpp
pwm::Output<Spec>
pwm::Capture<Spec, Options...>
```

### 18.2 Surface

PWM output covers:

- readiness;
- period and pulse configuration;
- chrono-duration overloads;
- validated duty-cycle convenience;
- polarity represented by the spec or explicit configuration;
- inactive or off state;
- native `pwm_dt_spec` access.

PWM capture covers:

- period, pulse, or both-edge capture modes;
- synchronous capture where supported;
- callback configuration;
- enable and disable;
- timeout and error reporting;
- native capture values and callback access.

Output and Capture are separate roles because they have different direction,
callback, and exclusivity contracts.

## 19. Counter

### 19.1 Types

```cpp
counter::Counter<Device>
counter::Alarm<Counter, Channel>
counter::Top<Counter>
```

### 19.2 Surface

Counter covers:

- readiness;
- start, stop, reset, and current value;
- 32-bit and 64-bit access where supported;
- controller frequency;
- tick and chrono-duration conversion;
- absolute and relative alarms;
- fixed alarm-channel ownership;
- top and wrap configuration;
- pending interrupt state where supported;
- native device, alarm, and top configuration access.

An `Alarm` type owns its declared channel and persistent callback state. A
second owner for the same counter channel is rejected where it can be proven.

## 20. Watchdog

### 20.1 Types

```cpp
watchdog::Device<Device>
watchdog::Channel<Device, Id>
watchdog::Timeout
```

The installation result may be a small move-only channel handle when the
driver allocates timeout IDs dynamically.

### 20.2 Surface

Watchdog covers:

- readiness;
- typed timeout windows;
- timeout installation;
- setup and supported options;
- typed channel feeding;
- disable where the hardware permits it;
- callback and reset behavior represented explicitly;
- multistage timeout configuration where supported;
- native device and channel identifiers.

Hardware owns only the physical primitive. Phase 12 Supervisor policy decides
whether all required gates permit feeding. A Device must not independently
feed the physical watchdog in a way that conceals supervisory failure.

Zephyr's task watchdog remains in `solar::kernel`, not Hardware.

## 21. DMA Boundary

### 21.1 Normal path

Driver-managed DMA is the default and only portable Solar DMA path.

Application code calls the ordinary peripheral API:

```cpp
SOLAR_TRY(ImuBus::transceive(tx, rx));
SOLAR_TRY(TelemetryUart::receive(buffer));
```

The selected Zephyr driver decides whether to use DMA, interrupts, polling, or
another mechanism according to:

- driver implementation;
- enabled Kconfig options;
- controller devicetree `dmas` and `dma-names` properties;
- synchronous versus asynchronous operation;
- transfer size or other driver policy.

Solar neither requests nor configures those DMA channels. A Solar Device using
SPI, UART, ADC, or another peripheral does not mention DMA merely because its
controller may use it internally.

### 21.2 No generic direct-DMA wrapper

Solar does not provide a generic `solar::hardware::dma` facade in this design.

Zephyr exposes a generic DMA API, but direct configuration remains volatile
across controllers and SoCs. Request cells, slots, channel allocation,
alignment, cache coherence, cyclic behavior, linked blocks, address widths,
and peripheral semantics cannot be made uniformly safe by reading devicetree.

Code that genuinely needs direct DMA uses:

- Zephyr's native DMA API;
- an SoC-specific API;
- a dedicated project driver;
- a future focused wrapper for one stable, justified controller family.

The EDT generator may report that a peripheral has configured DMA resources.
It must not turn controller-specific cells into a supposedly portable direct
DMA type.

### 21.3 Documentation and diagnostics

Solar documents for each peripheral family:

- whether its Zephyr driver can use DMA;
- which Kconfig and devicetree conditions enable that path;
- whether DMA is limited to asynchronous operations;
- buffer alignment, cache, and memory-region constraints exposed by Zephyr;
- whether a runtime fallback exists.

These are driver capability facts, not guarantees made by the generic wrapper.

## 22. Additional Hardware Families

The initial required implementation surface is:

- GPIO;
- SPI;
- I2C;
- UART;
- ADC;
- PWM;
- Counter;
- Watchdog;
- shared devicetree descriptors;
- EDT-generated catalog support;
- RTIO support required by the implemented core families.

Later focused wrappers may cover:

- CAN;
- flash and retained memory;
- USB endpoints and device classes;
- Zephyr sensor API devices;
- DAC;
- input devices;
- clocks and resets;
- regulators and power domains;
- entropy and cryptographic devices;
- display and video devices;
- network interfaces.

Later inclusion requires a stable Zephyr public surface and meaningful C++
typing or ownership value. These families do not block Phase 13 acceptance.

## 23. Board Declaration Pattern

### 23.1 Devicetree overlay

```dts
/ {
    aliases {
        status-led = &status_led;
        imu = &imu0;
        imu-ready = &imu_ready;
        battery = &battery_channel;
        motor-pwm = &motor_pwm;
    };
};
```

The exact node shapes follow their Zephyr bindings. Solar-specific DTS
bindings are introduced only when Solar truly owns a devicetree concept, not
to restate standard GPIO, SPI, or ADC bindings.

### 23.2 Board header

```cpp
#pragma once

#include <solar/hardware/adc.hpp>
#include <solar/hardware/gpio.hpp>
#include <solar/hardware/pwm.hpp>
#include <solar/hardware/spi.hpp>

namespace board
{
using StatusLed = solar::hardware::gpio::Output<
    solar::hardware::dt::alias<"status-led">>;

using ImuBus = solar::hardware::spi::Endpoint<
    solar::hardware::dt::alias<"imu">>;

using ImuReady = solar::hardware::gpio::Interrupt<
    solar::hardware::dt::alias<"imu-ready">,
    solar::hardware::gpio::Edge::Rising>;

using Battery = solar::hardware::adc::Channel<
    solar::hardware::dt::alias<"battery">>;

using MotorPwm = solar::hardware::pwm::Output<
    solar::hardware::dt::alias<"motor-pwm">>;
}
```

Ordinary component headers include the board endpoint headers they use. They do
not include the root System declaration.

### 23.3 Explicit fallback

Generated names are optional at the use site:

```cpp
inline constexpr auto LedSpec = solar::hardware::dt::gpio(
    GPIO_DT_SPEC_GET(DT_ALIAS(status_led), gpios));

using StatusLed = solar::hardware::gpio::Output<LedSpec>;
```

This path is also useful when prototyping a wrapper before extending the EDT
generator.

## 24. Application Device Example

```cpp
#pragma once

#include "board/imu.hpp"

#include <solar/lifecycle.hpp>
#include <solar/result.hpp>

struct Imu
{
    static solar::Result<void> init()
    {
        return board::ImuBus::require_ready()
            .and_then([] { return board::ImuReady::require_ready(); })
            .and_then([] { return configure_sensor(); });
    }

    static solar::Result<void> start()
    {
        return board::ImuReady::set_handler([] {
            SampleTask::try_notify_isr();
        });
    }

    static solar::Result<void> stop()
    {
        return board::ImuReady::disable();
    }

    struct Health
    {
        static solar::Result<solar::health::Assessment> assess();
        static solar::Result<void> recover();
    };

private:
    static solar::Result<void> configure_sensor();
    static void request_sample();

    static inline board::ImuBus::Operation transfer;
};
```

The boundary is deliberate:

- `board::ImuBus` owns typed Zephyr SPI mechanics;
- `Imu` owns sensor protocol and lifecycle;
- `SampleTask` owns execution registration;
- `Imu::Health` owns domain assessment and recovery;
- the Zephyr SPI driver may use DMA without any DMA type appearing here.

## 25. Lifecycle And Runtime Failure

Hardware readiness participates in a Device's lifecycle only when that Device
chooses to check it. Hardware endpoints do not receive lifecycle hooks.

Typical Device initialization is:

```cpp
static Result<void> init()
{
    return Endpoint::require_ready()
        .and_then(configure_hardware)
        .and_then(probe_external_device);
}
```

These are distinct facts:

- Zephyr controller device is ready;
- endpoint configuration succeeded;
- external hardware responded;
- domain Device is ready;
- domain Device remains healthy.

Hardware reports the first two. The application Device owns the latter three.

Runtime errors such as timeout, disconnect, arbitration loss, overrun, or bus
fault are returned to the caller. Hardware does not automatically restart a
Device, recover a bus, report Health, or invoke Supervisor policy.

## 26. Power Management, Reset, And Recovery

Zephyr device power management remains canonical. Hardware may expose focused
typed wrappers over stable public power-management calls, but must preserve:

- whether an operation is synchronous or asynchronous;
- reference and busy-state semantics;
- controller dependency ordering;
- unsupported-state errors;
- driver-owned resume behavior.

Suspend and resume are not universal endpoint methods unless the underlying
family has coherent semantics.

Peripheral reset and bus recovery are similarly focused:

- I2C controller recovery belongs to `i2c::Controller`;
- UART buffer reset belongs to the selected UART role;
- application sensor reset belongs to the Device protocol;
- system component restart belongs to Lifecycle when implemented;
- reboot belongs to system policy;
- physical reset-controller APIs may receive a future focused wrapper.

## 27. Errors And Results

Hardware follows Phase 00a.

```cpp
Result<void> write(...);
Result<Value> read(...);
Result<Receipt> submit(...);
```

Error translation preserves:

- native Zephyr errno;
- Solar operation and subsystem identity;
- endpoint identity where storage permits;
- distinctions among not ready, unsupported, invalid configuration, busy,
  timeout, cancellation, transport failure, and resource exhaustion.

`ready() == false` is not silently converted to generic I/O failure by
`require_ready()`.

Compile-time invalidity remains a compile-time diagnostic. Runtime capability
absence remains `NotSupported`. A driver returning an error remains a runtime
Result failure.

## 28. Inspection And Diagnostics

Hardware has no canonical runtime inspection database.

Focused queries may expose direct facts:

```cpp
StatusLed::ready();
ImuBus::descriptor();
ImuBus::native_handle();
```

The generated EDT summary is a build artifact and may be consumed by developer
tools. It does not become a universal runtime snapshot.

Application Devices expose meaningful operational state through their own
focused APIs, Health, Metrics, Events, Logging, and explicit Remote adapters.
Inspection may navigate from a Device to declared endpoint identities without
claiming ownership of hardware state.

## 29. Kconfig And Configuration

Kconfig controls compiled Solar Hardware capabilities and Zephyr driver APIs.
Expected options include focused controls for:

- Hardware wrapper family inclusion;
- generated EDT catalog generation;
- async SPI and I2C support;
- UART interrupt and async APIs;
- ADC async and RTIO support;
- PWM capture;
- Counter alarms;
- Watchdog support;
- native-handle exposure if a constrained build ever needs to disable it;
- diagnostics and compile-time validation strictness.

Kconfig must not duplicate endpoint addresses, pins, channels, or bus topology
that belong in devicetree.

Type policy may select C++ behavior such as serialization or timeout defaults.
It must not contradict immutable devicetree facts. The Phase 1 precedence rule
remains: Kconfig provides compiled defaults and type policy may refine allowed
behavior.

## 30. Portability

The architecture targets:

- native simulation for wrapper logic and fake endpoints;
- Teensy 4.0 and its NXP i.MX RT Zephyr support;
- conventional Zephyr boards with complete devicetree and pinctrl support;
- future boards selected through overlays rather than application rewrites.

Portability means the same wrapper contract can represent a supported Zephyr
driver family. It does not mean every board supports every operation mode.

Board-specific limitations remain explicit:

- absent controllers or channels;
- missing async driver support;
- DMA path requirements;
- pinctrl limitations;
- watchdog capabilities;
- capture and alarm channel counts;
- power-management differences;
- cache and memory placement requirements.

## 31. Test And Emulation Strategy

### 31.1 Compile tests

Compile-pass tests cover:

- structural Zephyr specs as C++23 descriptor inputs;
- generated alias lookup;
- explicit descriptor construction;
- valid role wrappers;
- board variants selecting different resolved endpoints;
- native-handle access;
- wrappers with partial driver capability.

Compile-fail tests cover:

- absent and disabled aliases;
- wrong wrapper family for a descriptor;
- incompatible ADC sequence controllers;
- invalid fixed Counter channels;
- unavailable callback modes;
- contradictory role policy;
- duplicate statically provable callback or channel ownership;
- malformed generator-facing aliases.

### 31.2 Native and fake tests

Fake driver seams test:

- readiness and error translation;
- logical versus raw GPIO polarity;
- synchronous transfer forwarding;
- shared-controller serialization;
- callback registration and removal;
- ISR-to-thread handoff;
- buffer leasing;
- busy and cancellation behavior;
- stale completion rejection;
- RTIO queue and completion handling;
- Watchdog feed-channel typing.

Fakes implement the same focused endpoint backend contract. They do not create
a separate application architecture or require Device code to use different
APIs.

### 31.3 Firmware tests

Firmware tests cover at least:

- one GPIO input, output, and interrupt;
- one SPI endpoint transaction;
- one I2C endpoint transaction where hardware permits;
- UART polling and one asynchronous mode;
- ADC channel read and conversion;
- PWM output;
- Counter value and alarm;
- Watchdog setup and gated feed in a controlled test;
- one driver-managed DMA-capable transfer where the board supports it;
- generated catalog regeneration after an overlay change.

No destructive watchdog reset test runs accidentally in the normal firmware
suite.

## 32. Old STM32 Prototype Decisions

The temporary prototype contributes useful design instincts but not its HAL
ownership model.

Retained:

- semantic board type aliases;
- static endpoint operations;
- role-specific GPIO Input and Output types;
- typed peripheral composition;
- concepts and compile-time diagnostics;
- compile-time binding validation;
- direct and compact global access.

Reshaped:

- manual pin identities become devicetree specs and generated aliases;
- manual alternate-function validation becomes binding and pinctrl truth;
- manually selected peripherals become Zephyr device nodes;
- manual ADC channel maps become `adc_dt_spec`;
- manual interrupt-line maps become GPIO or driver interrupt bindings;
- manual bus pin packs become controller pinctrl configuration;
- explicit DMA channel aliases become driver-managed DMA.

Rejected:

- maintaining PA/PB/PC pin inventories in Solar;
- duplicating MCU alternate-function tables;
- constructing and initializing vendor HAL handles;
- globally assigning direct DMA channels in board C++;
- assuming one STM32-style peripheral model across Zephyr boards;
- making `Board::init()` reconstruct Zephyr initialization.

## 33. Rejected Alternatives

### 33.1 Solar-owned HAL

Rejected because it duplicates Zephyr drivers, initialization, pinctrl, and
board support while reducing portability.

### 33.2 Manual numeric pin API as the primary model

Rejected because a number such as `13` is board-specific and omits controller,
flags, pinctrl, and binding context. A board may still provide `P13` as an alias
when its public numbering genuinely defines that contract.

### 33.3 Runtime hardware registry

Rejected because devicetree already provides compile-time identity and Zephyr
already owns runtime devices. A duplicate registry adds storage and competing
truth.

### 33.4 Hardware as a Facility

Rejected because raw hardware access does not require System binding or
lifecycle. Application Devices provide integration.

### 33.5 Generate every Device automatically

Rejected because devicetree describes physical configuration, not application
semantics, lifecycle, health, recovery, or ownership.

### 33.6 Parse generated C macros

Rejected because `edt.pickle` contains the resolved typed model directly and
is the same source used by Zephyr's generators.

### 33.7 Parse raw DTS independently

Rejected because Solar would need to reproduce overlay, binding, and build
resolution. Generation consumes Zephyr's completed EDT instead.

### 33.8 Infer every driver capability from EDT

Rejected because binding properties do not prove that every runtime API
function is implemented by a selected backend.

### 33.9 Generic direct DMA facade

Rejected because SoC-specific request cells, memory constraints, and controller
behavior make a portable facade misleading. Peripheral drivers own the normal
DMA path.

### 33.10 Hidden asynchronous worker

Rejected because it conceals execution ownership and resource costs. Devices
select Kernel or Execution handoff explicitly.

### 33.11 Force every operation through RTIO

Rejected because ordinary synchronous APIs remain valuable and RTIO support is
not uniform across driver families.

### 33.12 Hide native handles

Rejected because Solar cannot anticipate every stable Zephyr capability and
must remain interoperable with the platform it wraps.

## 34. Final Decisions

1. `solar::hardware` is a standalone namespace of typed wrappers and operation
   objects, not a Facility or System component.
2. Zephyr remains the owner of devices, drivers, initialization, pinctrl, and
   runtime driver state.
3. The final resolved devicetree remains canonical hardware truth.
4. Hardware endpoints do not automatically enter the System graph.
5. Application Devices own lifecycle, domain behavior, health, and recovery.
6. Project board headers own intentional semantic aliases.
7. C++23 structural descriptors are the foundation for typed endpoints.
8. Explicit Zephyr spec and node forms remain permanently supported.
9. Solar adds an EDT-driven generated descriptor catalog to its Zephyr module.
10. Generation consumes each image's `edt.pickle` through public `edtlib` APIs.
11. Generation is enabled by default with Hardware and may be disabled by
    Kconfig without disabling explicit wrappers.
12. Generated aliases, chosen nodes, diagnostics, and endpoint facts have no
    required runtime storage.
13. Generation does not create Devices, graph entries, or lifecycle policy.
14. Generation does not infer semantic application ownership.
15. Generated alias lookup is a polished frontend over explicit descriptors.
16. Hardware exposes readiness and native handles without claiming domain
    health.
17. Result translation preserves Zephyr error detail.
18. Simple singular endpoints use static methods and no Solar-owned state.
19. Descriptor-keyed static state owns singular callbacks where required.
20. Caller-owned Operation objects own independent asynchronous lifetimes.
21. Buffers remain leased until completion or confirmed cancellation.
22. Hardware creates no hidden thread, queue, worker, or allocation.
23. Callback context and ISR restrictions remain explicit.
24. Devices choose Kernel or Execution handoff.
25. Shared-controller authority is separate from addressed endpoint use.
26. GPIO has Pin, Input, Output, Interrupt, and Port roles.
27. SPI has Endpoint, Controller, Session, synchronous, async, and RTIO paths.
28. I2C separates addressed Endpoint operations from Controller-wide authority.
29. UART exposes distinct Polling, InterruptDriven, and Async roles.
30. ADC exposes Channel, compatible Sequence, async, and RTIO streaming paths.
31. PWM separates Output and Capture roles.
32. Counter exposes typed alarms and top behavior.
33. Hardware Watchdog provides the physical primitive while Supervisor owns
    feed policy.
34. Task Watchdog remains in Kernel.
35. Driver-managed DMA is Solar's normal and portable DMA path.
36. Solar Devices do not configure DMA used internally by peripheral drivers.
37. Solar does not provide a generic direct-DMA facade.
38. Direct DMA users use Zephyr, SoC-specific APIs, or dedicated drivers.
39. EDT may report DMA configuration but cannot make it portable.
40. RTIO is first-class and optional, never mandatory for ordinary calls.
41. Zephyr power-management and recovery semantics remain authoritative.
42. Hardware maintains no duplicate runtime devicetree or universal snapshot.
43. Native Zephyr use is an explicit supported escape hatch.
44. The old STM32 prototype's type ergonomics are retained while its manual HAL
    and pin-map ownership are rejected.

## 35. Open Questions

There are no blocking architectural questions for Phase 14.

Implementation may refine without changing this contract:

- exact descriptor storage and fixed-string alias implementation;
- exact generated namespace and header path;
- whether generated node-label access is opt-in or emitted for every label;
- exact generator diagnostics and summary formatting;
- exact Kconfig subdivision for wrapper families;
- exact method names for raw versus logical GPIO operations;
- exact bounded duty-cycle representation;
- exact SPI Session and asynchronous Operation shapes;
- exact I2C register convenience overloads;
- exact UART callback dispatcher representation;
- exact ADC engineering-unit helpers;
- exact Counter 32-bit and 64-bit selection rules;
- exact Watchdog channel-handle representation;
- exact RTIO C++ ownership wrappers;
- exact per-driver documentation format for DMA-backed operation;
- optional later wrappers for additional stable Zephyr driver families;
- whether native-handle access ever needs a hardened-build Kconfig control.
