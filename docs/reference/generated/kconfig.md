# Complete Kconfig Reference

This page is generated from `zephyr/Kconfig`. Change the Kconfig source or
help text, then rebuild the documentation; do not edit this page manually.

Solar currently defines 67 configuration symbols.

## `CONFIG_SOLAR`

Solar C++ framework

- Type: `bool`
- Menu: Main menu
- Defaults: `y`

Enable Solar's modular C++23 facilities for Zephyr applications.

## `CONFIG_SOLAR_APPLICATION_AUTO_INTEGRATION`

Automatically integrate a Solar application manifest

- Type: `bool`
- Menu: Main menu / Application integration
- Defaults: `y` if `SOLAR`
- Depends on: `SOLAR`

Generate and attach the conventional application contract when the
configured project manifest exists. The default path is optional so
standalone Solar modules do not require an application manifest.

## `CONFIG_SOLAR_APPLICATION_DEFAULT_SERVICE_PRIORITY_COOPERATIVE`

Cooperative

- Type: `bool`
- Menu: Main menu / Application integration
- Depends on: `<choice SOLAR_APPLICATION_DEFAULT_SERVICE_PRIORITY_CLASS>`

No additional help text.

## `CONFIG_SOLAR_APPLICATION_DEFAULT_SERVICE_PRIORITY_LEVEL`

Default service runner priority level

- Type: `int`
- Menu: Main menu / Application integration
- Defaults: `2` if `SOLAR_APPLICATION_AUTO_INTEGRATION and SOLAR`
- Depends on: `SOLAR_APPLICATION_AUTO_INTEGRATION and SOLAR`
- Range: `0` to `255`

Zero-based level within the selected Zephyr priority class. Solar validates
this value against CONFIG_NUM_PREEMPT_PRIORITIES or
CONFIG_NUM_COOP_PRIORITIES after Kconfig is resolved.

## `CONFIG_SOLAR_APPLICATION_DEFAULT_SERVICE_PRIORITY_PREEMPTIVE`

Preemptive

- Type: `bool`
- Menu: Main menu / Application integration
- Depends on: `<choice SOLAR_APPLICATION_DEFAULT_SERVICE_PRIORITY_CLASS>`

No additional help text.

## `CONFIG_SOLAR_APPLICATION_DEFAULT_SERVICE_STACK`

Default service runner stack size

- Type: `int`
- Menu: Main menu / Application integration
- Defaults: `2048` if `SOLAR_APPLICATION_AUTO_INTEGRATION and SOLAR`
- Depends on: `SOLAR_APPLICATION_AUTO_INTEGRATION and SOLAR`
- Range: `512` to `65536`

No additional help text.

## `CONFIG_SOLAR_APPLICATION_EXPLAIN`

Generate application expansion reports

- Type: `bool`
- Menu: Main menu / Application integration
- Defaults: `y` if `SOLAR_APPLICATION_AUTO_INTEGRATION and SOLAR`
- Depends on: `SOLAR_APPLICATION_AUTO_INTEGRATION and SOLAR`

No additional help text.

## `CONFIG_SOLAR_APPLICATION_GENERATE_PYTHON`

Generate the typed Python application client

- Type: `bool`
- Menu: Main menu / Application integration
- Defaults: `y` if `SOLAR_REMOTE and SOLAR_APPLICATION_AUTO_INTEGRATION and SOLAR`
- Depends on: `SOLAR_APPLICATION_AUTO_INTEGRATION and SOLAR`

No additional help text.

## `CONFIG_SOLAR_APPLICATION_GENERATE_SHIPMENT`

Verify and package the linked application shipment

- Type: `bool`
- Menu: Main menu / Application integration
- Defaults: `y` if `SOLAR_REMOTE and SOLAR_APPLICATION_AUTO_INTEGRATION and SOLAR`
- Depends on: `SOLAR_APPLICATION_AUTO_INTEGRATION and SOLAR`

Requires linked Remote manifest generation when an application manifest
is present. The CMake integration diagnoses that requirement without
forcing it on standalone Remote users.

## `CONFIG_SOLAR_APPLICATION_LOCK`

Application interface lock

- Type: `string`
- Menu: Main menu / Application integration
- Defaults: `"solar.interface.lock"` if `SOLAR_APPLICATION_AUTO_INTEGRATION and SOLAR`
- Depends on: `SOLAR_APPLICATION_AUTO_INTEGRATION and SOLAR`

No additional help text.

## `CONFIG_SOLAR_APPLICATION_LOG_HISTORY_CAPACITY`

Retained structured log record capacity

- Type: `int`
- Menu: Main menu / Application integration
- Defaults: `24` if `SOLAR_LOG and SOLAR_APPLICATION_AUTO_INTEGRATION and SOLAR`
- Depends on: `SOLAR_LOG and SOLAR_APPLICATION_AUTO_INTEGRATION and SOLAR`
- Range: `1` to `1024`

No additional help text.

## `CONFIG_SOLAR_APPLICATION_PROJECT`

Application project manifest

- Type: `string`
- Menu: Main menu / Application integration
- Defaults: `"solar.project.yaml"` if `SOLAR_APPLICATION_AUTO_INTEGRATION and SOLAR`
- Depends on: `SOLAR_APPLICATION_AUTO_INTEGRATION and SOLAR`

No additional help text.

## `CONFIG_SOLAR_APPLICATION_REQUIRE_LOCK`

Require an existing interface lock

- Type: `bool`
- Menu: Main menu / Application integration
- Defaults: `y` if `SOLAR_APPLICATION_AUTO_INTEGRATION and SOLAR`
- Depends on: `SOLAR_APPLICATION_AUTO_INTEGRATION and SOLAR`

Normal builds never modify the identity lock. Use solar-codegen with
--update-lock when intentionally changing the interface.

## `CONFIG_SOLAR_DESCRIPTOR_STRINGS`

Retain descriptor strings

- Type: `bool`
- Menu: Main menu
- Defaults: `y` if `SOLAR`
- Depends on: `SOLAR`

No additional help text.

## `CONFIG_SOLAR_DIAGNOSTIC_DETAIL`

Retain detailed diagnostics

- Type: `bool`
- Menu: Main menu
- Defaults: `y` if `SOLAR`
- Depends on: `SOLAR`

No additional help text.

## `CONFIG_SOLAR_FATAL_BRIDGE`

Install the Solar fatal error bridge

- Type: `bool`
- Menu: Main menu / Kernel integration
- Defaults: `n` if `SOLAR`
- Depends on: `SOLAR`

No additional help text.

## `CONFIG_SOLAR_HARDWARE`

Enable typed Zephyr hardware wrappers

- Type: `bool`
- Menu: Main menu / Hardware
- Defaults: `n` if `SOLAR`
- Depends on: `SOLAR`

No additional help text.

## `CONFIG_SOLAR_HARDWARE_ADC`

Enable ADC wrappers

- Type: `bool`
- Menu: Main menu / Hardware
- Defaults: `y` if `ADC and SOLAR_HARDWARE and SOLAR`
- Depends on: `ADC and SOLAR_HARDWARE and SOLAR`

No additional help text.

## `CONFIG_SOLAR_HARDWARE_ADC_ASYNC`

Enable asynchronous ADC reads

- Type: `bool`
- Menu: Main menu / Hardware
- Defaults: `y` if `SOLAR_HARDWARE_ADC and ADC_ASYNC and SOLAR_HARDWARE and SOLAR`
- Depends on: `SOLAR_HARDWARE_ADC and ADC_ASYNC and SOLAR_HARDWARE and SOLAR`

No additional help text.

## `CONFIG_SOLAR_HARDWARE_COUNTER`

Enable Counter wrappers

- Type: `bool`
- Menu: Main menu / Hardware
- Defaults: `y` if `COUNTER and SOLAR_HARDWARE and SOLAR`
- Depends on: `COUNTER and SOLAR_HARDWARE and SOLAR`

No additional help text.

## `CONFIG_SOLAR_HARDWARE_GENERATE_DEVICETREE`

Generate ergonomic devicetree aliases

- Type: `bool`
- Menu: Main menu / Hardware
- Defaults: `y` if `SOLAR_HARDWARE and SOLAR`
- Depends on: `SOLAR_HARDWARE and SOLAR`

No additional help text.

## `CONFIG_SOLAR_HARDWARE_GPIO`

Enable GPIO wrappers

- Type: `bool`
- Menu: Main menu / Hardware
- Defaults: `y` if `GPIO and SOLAR_HARDWARE and SOLAR`
- Depends on: `GPIO and SOLAR_HARDWARE and SOLAR`

No additional help text.

## `CONFIG_SOLAR_HARDWARE_GPIO_INTERRUPTS`

Enable GPIO interrupt ownership wrappers

- Type: `bool`
- Menu: Main menu / Hardware
- Defaults: `y` if `SOLAR_HARDWARE_GPIO and SOLAR_HARDWARE and SOLAR`
- Depends on: `SOLAR_HARDWARE_GPIO and SOLAR_HARDWARE and SOLAR`

No additional help text.

## `CONFIG_SOLAR_HARDWARE_I2C`

Enable I2C wrappers

- Type: `bool`
- Menu: Main menu / Hardware
- Defaults: `y` if `I2C and SOLAR_HARDWARE and SOLAR`
- Depends on: `I2C and SOLAR_HARDWARE and SOLAR`

No additional help text.

## `CONFIG_SOLAR_HARDWARE_I2C_ASYNC`

Enable callback-driven I2C operations

- Type: `bool`
- Menu: Main menu / Hardware
- Defaults: `y` if `SOLAR_HARDWARE_I2C and I2C_CALLBACK and SOLAR_HARDWARE and SOLAR`
- Depends on: `SOLAR_HARDWARE_I2C and I2C_CALLBACK and SOLAR_HARDWARE and SOLAR`

No additional help text.

## `CONFIG_SOLAR_HARDWARE_PWM`

Enable PWM wrappers

- Type: `bool`
- Menu: Main menu / Hardware
- Defaults: `y` if `PWM and SOLAR_HARDWARE and SOLAR`
- Depends on: `PWM and SOLAR_HARDWARE and SOLAR`

No additional help text.

## `CONFIG_SOLAR_HARDWARE_PWM_CAPTURE`

Enable PWM capture wrappers

- Type: `bool`
- Menu: Main menu / Hardware
- Defaults: `y` if `SOLAR_HARDWARE_PWM and PWM_CAPTURE and SOLAR_HARDWARE and SOLAR`
- Depends on: `SOLAR_HARDWARE_PWM and PWM_CAPTURE and SOLAR_HARDWARE and SOLAR`

No additional help text.

## `CONFIG_SOLAR_HARDWARE_RTIO`

Enable native RTIO adapters

- Type: `bool`
- Menu: Main menu / Hardware
- Defaults: `y` if `RTIO and (SPI_RTIO or I2C_RTIO or ADC_STREAM) and SOLAR_HARDWARE and SOLAR`
- Depends on: `RTIO and (SPI_RTIO or I2C_RTIO or ADC_STREAM) and SOLAR_HARDWARE and SOLAR`

No additional help text.

## `CONFIG_SOLAR_HARDWARE_SPI`

Enable SPI wrappers

- Type: `bool`
- Menu: Main menu / Hardware
- Defaults: `y` if `SPI and SOLAR_HARDWARE and SOLAR`
- Depends on: `SPI and SOLAR_HARDWARE and SOLAR`

No additional help text.

## `CONFIG_SOLAR_HARDWARE_SPI_ASYNC`

Enable asynchronous SPI operations

- Type: `bool`
- Menu: Main menu / Hardware
- Defaults: `y` if `SOLAR_HARDWARE_SPI and SPI_ASYNC and SOLAR_HARDWARE and SOLAR`
- Depends on: `SOLAR_HARDWARE_SPI and SPI_ASYNC and SOLAR_HARDWARE and SOLAR`

No additional help text.

## `CONFIG_SOLAR_HARDWARE_UART`

Enable UART wrappers

- Type: `bool`
- Menu: Main menu / Hardware
- Defaults: `y` if `SERIAL and SOLAR_HARDWARE and SOLAR`
- Depends on: `SERIAL and SOLAR_HARDWARE and SOLAR`

No additional help text.

## `CONFIG_SOLAR_HARDWARE_UART_ASYNC`

Enable asynchronous UART ownership wrappers

- Type: `bool`
- Menu: Main menu / Hardware
- Defaults: `y` if `SOLAR_HARDWARE_UART and UART_ASYNC_API and SOLAR_HARDWARE and SOLAR`
- Depends on: `SOLAR_HARDWARE_UART and UART_ASYNC_API and SOLAR_HARDWARE and SOLAR`

No additional help text.

## `CONFIG_SOLAR_HARDWARE_UART_INTERRUPT`

Enable interrupt-driven UART ownership wrappers

- Type: `bool`
- Menu: Main menu / Hardware
- Defaults: `y` if `SOLAR_HARDWARE_UART and UART_INTERRUPT_DRIVEN and SOLAR_HARDWARE and SOLAR`
- Depends on: `SOLAR_HARDWARE_UART and UART_INTERRUPT_DRIVEN and SOLAR_HARDWARE and SOLAR`

No additional help text.

## `CONFIG_SOLAR_HARDWARE_WATCHDOG`

Enable hardware Watchdog wrappers

- Type: `bool`
- Menu: Main menu / Hardware
- Defaults: `y` if `WATCHDOG and SOLAR_HARDWARE and SOLAR`
- Depends on: `WATCHDOG and SOLAR_HARDWARE and SOLAR`

No additional help text.

## `CONFIG_SOLAR_KERNEL_RUNTIME_DIAGNOSTICS`

Enable typed thread runtime statistics

- Type: `bool`
- Menu: Main menu / Kernel integration
- Defaults: `n` if `SOLAR`
- Depends on: `SOLAR`
- Selects: `THREAD_RUNTIME_STATS` if `SOLAR`

No additional help text.

## `CONFIG_SOLAR_KERNEL_RUNTIME_STACK_SAFETY`

Enable runtime stack safety checks

- Type: `bool`
- Menu: Main menu / Kernel integration
- Defaults: `n` if `SOLAR`
- Depends on: `SOLAR`
- Selects: `SOLAR_KERNEL_STACK_DIAGNOSTICS` if `SOLAR`, `THREAD_RUNTIME_STACK_SAFETY` if `SOLAR`

No additional help text.

## `CONFIG_SOLAR_KERNEL_STACK_DIAGNOSTICS`

Enable typed thread stack diagnostics

- Type: `bool`
- Menu: Main menu / Kernel integration
- Defaults: `n` if `SOLAR`
- Depends on: `SOLAR`
- Selects: `INIT_STACKS` if `SOLAR`, `THREAD_STACK_INFO` if `SOLAR`

No additional help text.

## `CONFIG_SOLAR_KERNEL_THREAD_ENUMERATION`

Enable typed global thread enumeration

- Type: `bool`
- Menu: Main menu / Kernel integration
- Defaults: `n` if `SOLAR`
- Depends on: `SOLAR`
- Selects: `THREAD_MONITOR` if `SOLAR`

No additional help text.

## `CONFIG_SOLAR_LOG`

Enable Solar structured logging

- Type: `bool`
- Menu: Main menu / Structured logging
- Defaults: `n` if `SOLAR`
- Depends on: `SOLAR`

Provide bounded, explicitly owned logging. Applications select sinks;
Solar does not replace Zephyr's logging frontend.

## `CONFIG_SOLAR_LOG_MAX_HEXDUMP_BYTES`

Maximum copied hexdump size

- Type: `int`
- Menu: Main menu / Structured logging
- Defaults: `128` if `SOLAR_LOG and SOLAR`
- Depends on: `SOLAR_LOG and SOLAR`
- Range: `1` to `4096`

No additional help text.

## `CONFIG_SOLAR_LOG_MAX_RECORD_BYTES`

Maximum encoded log payload bytes

- Type: `int`
- Menu: Main menu / Structured logging
- Defaults: `384` if `SOLAR_LOG and SOLAR`
- Depends on: `SOLAR_LOG and SOLAR`
- Range: `64` to `4096`

No additional help text.

## `CONFIG_SOLAR_LOG_MAX_STRING_BYTES`

Maximum copied string argument size

- Type: `int`
- Menu: Main menu / Structured logging
- Defaults: `96` if `SOLAR_LOG and SOLAR`
- Depends on: `SOLAR_LOG and SOLAR`
- Range: `1` to `1024`

No additional help text.

## `CONFIG_SOLAR_LOG_RENDER_BUFFER_BYTES`

Bounded rendering buffer size

- Type: `int`
- Menu: Main menu / Structured logging
- Defaults: `384` if `SOLAR_LOG and SOLAR`
- Depends on: `SOLAR_LOG and SOLAR`
- Range: `64` to `4096`

No additional help text.

## `CONFIG_SOLAR_REMOTE`

Solar typed Remote protocol

- Type: `bool`
- Menu: Main menu / Remote protocol
- Defaults: `n` if `SOLAR`
- Depends on: `SOLAR`
- Selects: `COBS` if `SOLAR`, `CRC` if `SOLAR`, `ZCBOR` if `SOLAR`, `ZCBOR_CANONICAL` if `SOLAR`

No additional help text.

## `CONFIG_SOLAR_REMOTE_BUILD_ID`

Remote firmware build identity

- Type: `hex`
- Menu: Main menu / Remote protocol
- Defaults: `0` if `SOLAR_REMOTE and SOLAR`
- Depends on: `SOLAR_REMOTE and SOLAR`

No additional help text.

## `CONFIG_SOLAR_REMOTE_EVENT_QUEUE_DEPTH`

Remote service event queue depth

- Type: `int`
- Menu: Main menu / Remote protocol
- Defaults: `32` if `SOLAR_REMOTE and SOLAR`
- Depends on: `SOLAR_REMOTE and SOLAR`
- Range: `4` to `256`

No additional help text.

## `CONFIG_SOLAR_REMOTE_GENERATE_MANIFEST`

Generate host manifest artifacts after linking

- Type: `bool`
- Menu: Main menu / Remote protocol
- Defaults: `y` if `SOLAR_REMOTE and SOLAR`
- Depends on: `SOLAR_REMOTE and SOLAR`

No additional help text.

## `CONFIG_SOLAR_REMOTE_ISR_PUBLICATION`

Enable constrained ISR publication

- Type: `bool`
- Menu: Main menu / Remote protocol
- Defaults: `n` if `SOLAR_REMOTE and SOLAR`
- Depends on: `SOLAR_REMOTE and SOLAR`

No additional help text.

## `CONFIG_SOLAR_REMOTE_MANIFEST_RETRIEVAL`

Allow bounded runtime manifest retrieval

- Type: `bool`
- Menu: Main menu / Remote protocol
- Defaults: `y` if `SOLAR_REMOTE and SOLAR`
- Depends on: `SOLAR_REMOTE and SOLAR`

No additional help text.

## `CONFIG_SOLAR_REMOTE_MAX_ENDPOINTS`

Maximum Remote endpoints per identity domain

- Type: `int`
- Menu: Main menu / Remote protocol
- Defaults: `128` if `SOLAR_REMOTE and SOLAR`
- Depends on: `SOLAR_REMOTE and SOLAR`
- Range: `1` to `1024`

No additional help text.

## `CONFIG_SOLAR_REMOTE_MAX_FRAME_BYTES`

Maximum decoded protocol frame bytes

- Type: `int`
- Menu: Main menu / Remote protocol
- Defaults: `1024` if `SOLAR_REMOTE and SOLAR`
- Depends on: `SOLAR_REMOTE and SOLAR`
- Range: `64` to `65535`

No additional help text.

## `CONFIG_SOLAR_REMOTE_MAX_INBOUND_WINDOW`

Maximum inbound stream window per link

- Type: `int`
- Menu: Main menu / Remote protocol
- Defaults: `8` if `SOLAR_REMOTE and SOLAR`
- Depends on: `SOLAR_REMOTE and SOLAR`
- Range: `1` to `64`

No additional help text.

## `CONFIG_SOLAR_REMOTE_MAX_MESSAGE_BYTES`

Maximum logical message bytes

- Type: `int`
- Menu: Main menu / Remote protocol
- Defaults: `8192` if `SOLAR_REMOTE and SOLAR`
- Depends on: `SOLAR_REMOTE and SOLAR`
- Range: `64` to `1048576`

No additional help text.

## `CONFIG_SOLAR_REMOTE_MAX_REQUESTS`

Maximum concurrently admitted Remote requests

- Type: `int`
- Menu: Main menu / Remote protocol
- Defaults: `8` if `SOLAR_REMOTE and SOLAR`
- Depends on: `SOLAR_REMOTE and SOLAR`
- Range: `1` to `128`

No additional help text.

## `CONFIG_SOLAR_REMOTE_MAX_SCHEMAS`

Maximum effective Remote schemas

- Type: `int`
- Menu: Main menu / Remote protocol
- Defaults: `128` if `SOLAR_REMOTE and SOLAR`
- Depends on: `SOLAR_REMOTE and SOLAR`
- Range: `1` to `1024`

No additional help text.

## `CONFIG_SOLAR_REMOTE_MAX_SCHEMA_FIELDS`

Maximum fields in one schema

- Type: `int`
- Menu: Main menu / Remote protocol
- Defaults: `32` if `SOLAR_REMOTE and SOLAR`
- Depends on: `SOLAR_REMOTE and SOLAR`
- Range: `1` to `64`

No additional help text.

## `CONFIG_SOLAR_REMOTE_MAX_STREAM_RATE_HZ`

Maximum negotiated stream rate

- Type: `int`
- Menu: Main menu / Remote protocol
- Defaults: `1000` if `SOLAR_REMOTE and SOLAR`
- Depends on: `SOLAR_REMOTE and SOLAR`
- Range: `1` to `1000000`

No additional help text.

## `CONFIG_SOLAR_REMOTE_OUTBOUND_MESSAGE_SLOTS`

Fragmented outbound messages per link

- Type: `int`
- Menu: Main menu / Remote protocol
- Defaults: `2` if `SOLAR_REMOTE and SOLAR`
- Depends on: `SOLAR_REMOTE and SOLAR`
- Range: `1` to `16`

No additional help text.

## `CONFIG_SOLAR_REMOTE_OUTPUT_LANES`

SOLAR_REMOTE_OUTPUT_LANES

- Type: `int`
- Menu: Main menu / Remote protocol
- Defaults: `5` if `SOLAR_REMOTE and SOLAR`
- Depends on: `SOLAR_REMOTE and SOLAR`

No additional help text.

## `CONFIG_SOLAR_REMOTE_OUTPUT_LANE_DEPTH`

Queued frames per output lane

- Type: `int`
- Menu: Main menu / Remote protocol
- Defaults: `4` if `SOLAR_REMOTE and SOLAR`
- Depends on: `SOLAR_REMOTE and SOLAR`
- Range: `1` to `64`

No additional help text.

## `CONFIG_SOLAR_REMOTE_PACKED`

Enable explicit packed stream schemas

- Type: `bool`
- Menu: Main menu / Remote protocol
- Defaults: `y` if `SOLAR_REMOTE and SOLAR`
- Depends on: `SOLAR_REMOTE and SOLAR`

No additional help text.

## `CONFIG_SOLAR_REMOTE_REASSEMBLY_SLOTS`

Fragment reassembly slots per link

- Type: `int`
- Menu: Main menu / Remote protocol
- Defaults: `2` if `SOLAR_REMOTE and SOLAR`
- Depends on: `SOLAR_REMOTE and SOLAR`
- Range: `1` to `16`

No additional help text.

## `CONFIG_SOLAR_REMOTE_REASSEMBLY_TIMEOUT_MS`

Fragment reassembly timeout in milliseconds

- Type: `int`
- Menu: Main menu / Remote protocol
- Defaults: `1000` if `SOLAR_REMOTE and SOLAR`
- Depends on: `SOLAR_REMOTE and SOLAR`
- Range: `1` to `60000`

No additional help text.

## `CONFIG_SOLAR_REMOTE_RESPONSE_CACHE_BYTES`

Remote response cache bytes per link

- Type: `int`
- Menu: Main menu / Remote protocol
- Defaults: `512` if `SOLAR_REMOTE and SOLAR`
- Depends on: `SOLAR_REMOTE and SOLAR`
- Range: `64` to `8192`

No additional help text.

## `CONFIG_SOLAR_REMOTE_RUNTIME_INTROSPECTION`

Enable bounded Remote runtime introspection

- Type: `bool`
- Menu: Main menu / Remote protocol
- Defaults: `n` if `SOLAR_REMOTE and SOLAR`
- Depends on: `SOLAR_REMOTE and SOLAR`

No additional help text.

## `CONFIG_SOLAR_REMOTE_SERVICE_PRIORITY`

Remote service preemptive priority

- Type: `int`
- Menu: Main menu / Remote protocol
- Defaults: `1` if `SOLAR_REMOTE and SOLAR`
- Depends on: `SOLAR_REMOTE and SOLAR`
- Range: `0` to `15`

No additional help text.

## `CONFIG_SOLAR_REMOTE_SERVICE_STACK_SIZE`

Remote service stack size

- Type: `int`
- Menu: Main menu / Remote protocol
- Defaults: `4096` if `SOLAR_REMOTE and SOLAR`
- Depends on: `SOLAR_REMOTE and SOLAR`
- Range: `1024` to `65536`

No additional help text.
