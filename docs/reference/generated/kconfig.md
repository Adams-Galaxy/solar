# Complete Kconfig Reference

This page is generated from `zephyr/Kconfig`. Change the Kconfig source or
help text, then rebuild the documentation; do not edit this page manually.

Solar currently defines 175 configuration symbols.

## `CONFIG_SOLAR`

Solar firmware orchestration

- Type: `bool`
- Menu: Main menu
- Defaults: `y`

Enable the Solar C++23 firmware framework and Zephyr module.

## `CONFIG_SOLAR_ALLOW_DYNAMIC_ALLOCATION`

Allow dynamic allocation in Solar

- Type: `bool`
- Menu: Main menu
- Defaults: `n` if `SOLAR`
- Depends on: `SOLAR`

Permit optional facilities to use dynamic allocation only where their
contracts explicitly support it. Solar core remains statically owned.

## `CONFIG_SOLAR_BUS`

Enable the typed application bus

- Type: `bool`
- Menu: Main menu / Bus
- Defaults: `y` if `SOLAR_EXECUTION and SOLAR`
- Depends on: `SOLAR_EXECUTION and SOLAR`

Enable compile-time message catalogs, static subscription routes, and
bounded route-owned delivery state. The facility is included only when
the effective System declares Bus messages or subscriptions.

## `CONFIG_SOLAR_BUS_DEFAULT_OVERFLOW_DROP_NEWEST`

Drop the new message

- Type: `bool`
- Menu: Main menu / Bus
- Depends on: `<choice SOLAR_BUS_DEFAULT_OVERFLOW>`

No additional help text.

## `CONFIG_SOLAR_BUS_DEFAULT_OVERFLOW_DROP_OLDEST`

Drop the oldest pending message

- Type: `bool`
- Menu: Main menu / Bus
- Depends on: `<choice SOLAR_BUS_DEFAULT_OVERFLOW>`

No additional help text.

## `CONFIG_SOLAR_BUS_DEFAULT_OVERFLOW_REJECT`

Reject the new message

- Type: `bool`
- Menu: Main menu / Bus
- Depends on: `<choice SOLAR_BUS_DEFAULT_OVERFLOW>`

No additional help text.

## `CONFIG_SOLAR_BUS_DEFAULT_QUEUE_CAPACITY`

Default queued Bus route capacity

- Type: `int`
- Menu: Main menu / Bus
- Defaults: `4` if `SOLAR_BUS and SOLAR`
- Depends on: `SOLAR_BUS and SOLAR`
- Range: `1` to `SOLAR_BUS_MAX_ROUTE_CAPACITY`

No additional help text.

## `CONFIG_SOLAR_BUS_DEFAULT_STOP_CANCEL_PENDING`

Cancel pending messages

- Type: `bool`
- Menu: Main menu / Bus
- Depends on: `<choice SOLAR_BUS_DEFAULT_STOP>`

No additional help text.

## `CONFIG_SOLAR_BUS_DEFAULT_STOP_DRAIN`

Drain accepted messages

- Type: `bool`
- Menu: Main menu / Bus
- Depends on: `<choice SOLAR_BUS_DEFAULT_STOP>`

No additional help text.

## `CONFIG_SOLAR_BUS_MAX_ASYNC_PAYLOAD_ALIGNMENT`

Maximum asynchronous Bus payload alignment

- Type: `int`
- Menu: Main menu / Bus
- Defaults: `16` if `SOLAR_BUS and SOLAR`
- Depends on: `SOLAR_BUS and SOLAR`
- Range: `1` to `256`

No additional help text.

## `CONFIG_SOLAR_BUS_MAX_ASYNC_PAYLOAD_BYTES`

Maximum asynchronous Bus payload size

- Type: `int`
- Menu: Main menu / Bus
- Defaults: `256` if `SOLAR_BUS and SOLAR`
- Depends on: `SOLAR_BUS and SOLAR`
- Range: `1` to `65536`

No additional help text.

## `CONFIG_SOLAR_BUS_MAX_MESSAGES`

Maximum Bus messages

- Type: `int`
- Menu: Main menu / Bus
- Defaults: `128` if `SOLAR_BUS and SOLAR`
- Depends on: `SOLAR_BUS and SOLAR`
- Range: `1` to `65534`

No additional help text.

## `CONFIG_SOLAR_BUS_MAX_ROUTE_CAPACITY`

Maximum queued messages per Bus route

- Type: `int`
- Menu: Main menu / Bus
- Defaults: `32` if `SOLAR_BUS and SOLAR`
- Depends on: `SOLAR_BUS and SOLAR`
- Range: `1` to `65535`

No additional help text.

## `CONFIG_SOLAR_BUS_MAX_SUBSCRIPTIONS`

Maximum Bus subscriptions

- Type: `int`
- Menu: Main menu / Bus
- Defaults: `256` if `SOLAR_BUS and SOLAR`
- Depends on: `SOLAR_BUS and SOLAR`
- Range: `1` to `65534`

No additional help text.

## `CONFIG_SOLAR_BUS_STOP_TIMEOUT_MS`

Bus route stop timeout (milliseconds)

- Type: `int`
- Menu: Main menu / Bus
- Defaults: `100` if `SOLAR_BUS and SOLAR`
- Depends on: `SOLAR_BUS and SOLAR`
- Range: `0` to `60000`

No additional help text.

## `CONFIG_SOLAR_DESCRIPTOR_STRINGS`

Retain descriptor strings

- Type: `bool`
- Menu: Main menu
- Defaults: `y` if `SOLAR`
- Depends on: `SOLAR`

Retain human-readable descriptor names and descriptions where the owning
subsystem supports stripping them.

## `CONFIG_SOLAR_DIAGNOSTIC_DETAIL`

Retain detailed diagnostics

- Type: `bool`
- Menu: Main menu
- Defaults: `y` if `SOLAR`
- Depends on: `SOLAR`

Retain optional diagnostic metadata used to produce richer build and
runtime error reports.

## `CONFIG_SOLAR_EVENTS`

Enable structured observability events

- Type: `bool`
- Menu: Main menu / Events
- Defaults: `y` if `SOLAR`
- Depends on: `SOLAR`

Enable compile-time event catalogs, bounded thread and ISR capture,
deferred processing, compact retained history, and focused accounting.

## `CONFIG_SOLAR_EVENTS_DEFAULT_RETENTION_BUFFERED`

Buffered history

- Type: `bool`
- Menu: Main menu / Events
- Depends on: `<choice SOLAR_EVENTS_DEFAULT_RETENTION>`

No additional help text.

## `CONFIG_SOLAR_EVENTS_DEFAULT_RETENTION_TRANSIENT`

Transient processing only

- Type: `bool`
- Menu: Main menu / Events
- Depends on: `<choice SOLAR_EVENTS_DEFAULT_RETENTION>`

No additional help text.

## `CONFIG_SOLAR_EVENTS_DEFAULT_STOP_CANCEL`

Cancel pending processing

- Type: `bool`
- Menu: Main menu / Events
- Depends on: `<choice SOLAR_EVENTS_DEFAULT_STOP>`

No additional help text.

## `CONFIG_SOLAR_EVENTS_DEFAULT_STOP_DRAIN`

Final bounded drain

- Type: `bool`
- Menu: Main menu / Events
- Depends on: `<choice SOLAR_EVENTS_DEFAULT_STOP>`

No additional help text.

## `CONFIG_SOLAR_EVENTS_HISTORY_BYTES`

Buffered event history bytes

- Type: `int`
- Menu: Main menu / Events
- Defaults: `4096` if `SOLAR_EVENTS and SOLAR`
- Depends on: `SOLAR_EVENTS and SOLAR`
- Range: `64` to `65535`

No additional help text.

## `CONFIG_SOLAR_EVENTS_INGRESS_DEPTH`

Thread event ingress depth

- Type: `int`
- Menu: Main menu / Events
- Defaults: `16` if `SOLAR_EVENTS and SOLAR`
- Depends on: `SOLAR_EVENTS and SOLAR`
- Range: `1` to `4096`

No additional help text.

## `CONFIG_SOLAR_EVENTS_ISR_INGRESS_DEPTH`

ISR event ingress depth

- Type: `int`
- Menu: Main menu / Events
- Defaults: `8` if `SOLAR_EVENTS and SOLAR`
- Depends on: `SOLAR_EVENTS and SOLAR`
- Range: `0` to `4096`

No additional help text.

## `CONFIG_SOLAR_EVENTS_MAX_AGGREGATION_KEYS`

Maximum keys in one event aggregation policy

- Type: `int`
- Menu: Main menu / Events
- Defaults: `16` if `SOLAR_EVENTS and SOLAR`
- Depends on: `SOLAR_EVENTS and SOLAR`
- Range: `1` to `4096`

No additional help text.

## `CONFIG_SOLAR_EVENTS_MAX_CRITICAL_RESERVED_SLOTS`

Maximum total critical reserved slots

- Type: `int`
- Menu: Main menu / Events
- Defaults: `16` if `SOLAR_EVENTS and SOLAR`
- Depends on: `SOLAR_EVENTS and SOLAR`
- Range: `1` to `4096`

No additional help text.

## `CONFIG_SOLAR_EVENTS_MAX_EVENTS`

Maximum events

- Type: `int`
- Menu: Main menu / Events
- Defaults: `128` if `SOLAR_EVENTS and SOLAR`
- Depends on: `SOLAR_EVENTS and SOLAR`
- Range: `1` to `65534`

No additional help text.

## `CONFIG_SOLAR_EVENTS_MAX_PAYLOAD_ALIGNMENT`

Maximum event payload alignment

- Type: `int`
- Menu: Main menu / Events
- Defaults: `8` if `SOLAR_EVENTS and SOLAR`
- Depends on: `SOLAR_EVENTS and SOLAR`
- Range: `1` to `256`

No additional help text.

## `CONFIG_SOLAR_EVENTS_MAX_PAYLOAD_BYTES`

Maximum copied event payload size

- Type: `int`
- Menu: Main menu / Events
- Defaults: `64` if `SOLAR_EVENTS and SOLAR`
- Depends on: `SOLAR_EVENTS and SOLAR`
- Range: `1` to `65535`

No additional help text.

## `CONFIG_SOLAR_EVENTS_MAX_PROCESSORS`

Maximum event processors

- Type: `int`
- Menu: Main menu / Events
- Defaults: `256` if `SOLAR_EVENTS and SOLAR`
- Depends on: `SOLAR_EVENTS and SOLAR`
- Range: `1` to `65534`

No additional help text.

## `CONFIG_SOLAR_EVENTS_PERSISTENCE`

Enable persistent event retention adapters

- Type: `bool`
- Menu: Main menu / Events
- Defaults: `n` if `SOLAR_EVENTS and SOLAR`
- Depends on: `SOLAR_EVENTS and SOLAR`

No additional help text.

## `CONFIG_SOLAR_EVENTS_PROCESSOR_SYSTEM_WORKQUEUE_DEFAULT`

Use Zephyr's system workqueue for event processing

- Type: `bool`
- Menu: Main menu / Events
- Defaults: `y` if `SOLAR_EXECUTION and SOLAR_EVENTS and SOLAR`
- Depends on: `SOLAR_EXECUTION and SOLAR_EVENTS and SOLAR`

Use Zephyr's existing system workqueue when event configuration does not
name an explicit processor executor. No Solar thread is created.

## `CONFIG_SOLAR_EVENTS_STOP_TIMEOUT_MS`

Event final-drain timeout (milliseconds)

- Type: `int`
- Menu: Main menu / Events
- Defaults: `100` if `SOLAR_EVENTS and SOLAR`
- Depends on: `SOLAR_EVENTS and SOLAR`
- Range: `0` to `60000`

No additional help text.

## `CONFIG_SOLAR_EXECUTION`

Enable system-integrated execution

- Type: `bool`
- Menu: Main menu / Execution
- Defaults: `y` if `SOLAR`
- Depends on: `SOLAR`

Enable Solar service execution, registered work, and owned workqueue
integration. Direct solar::kernel work remains available independently.

## `CONFIG_SOLAR_EXECUTION_DEFAULT_SYSTEM_WORKQUEUE`

Use Zephyr's system workqueue for omitted targets

- Type: `bool`
- Menu: Main menu / Execution
- Defaults: `n` if `SOLAR_EXECUTION and SOLAR`
- Depends on: `SOLAR_EXECUTION and SOLAR`

Resolve an execution registration with no explicit target to Zephyr's
existing system workqueue. This creates no Solar thread, queue, or stack.
When disabled, every work registration must name a target explicitly.

## `CONFIG_SOLAR_EXECUTION_MAX_REGISTRATIONS`

Maximum execution registrations

- Type: `int`
- Menu: Main menu / Execution
- Defaults: `128` if `SOLAR_EXECUTION and SOLAR`
- Depends on: `SOLAR_EXECUTION and SOLAR`
- Range: `1` to `65534`

Hard compile-time ceiling for the effective execution catalog. Runtime
storage remains derived exactly from registered work.

## `CONFIG_SOLAR_EXECUTION_QUIESCENCE_TIMEOUT_MS`

Default registration quiescence timeout (milliseconds)

- Type: `int`
- Menu: Main menu / Execution
- Defaults: `100` if `SOLAR_EXECUTION and SOLAR`
- Depends on: `SOLAR_EXECUTION and SOLAR`
- Range: `0` to `60000`

Default bounded wait for in-flight work during lifecycle containment.

## `CONFIG_SOLAR_EXECUTOR_ABORT_ON_STOP_TIMEOUT`

Abort owned workqueues after stop timeout

- Type: `bool`
- Menu: Main menu / Execution
- Defaults: `y` if `SOLAR_EXECUTION and SOLAR`
- Depends on: `SOLAR_EXECUTION and SOLAR`

Permit Solar to abort only workqueue threads it created and owns after
bounded quiescence or native queue stop times out.

## `CONFIG_SOLAR_EXECUTOR_STOP_TIMEOUT_MS`

Default owned workqueue stop timeout (milliseconds)

- Type: `int`
- Menu: Main menu / Execution
- Defaults: `100` if `SOLAR_EXECUTION and SOLAR`
- Depends on: `SOLAR_EXECUTION and SOLAR`
- Range: `0` to `60000`

Default bounded timeout for stopping a Solar-owned workqueue thread.

## `CONFIG_SOLAR_FATAL_BRIDGE`

Install the Solar fatal error bridge

- Type: `bool`
- Menu: Main menu / Kernel integration
- Defaults: `n` if `SOLAR`
- Depends on: `SOLAR`

Replace Zephyr's weak application fatal handler with Solar's panic-safe
atomic latch and one optional observer, then halt through Zephyr. Select
this only when the application does not provide another fatal handler.

## `CONFIG_SOLAR_HARDWARE`

Enable typed Zephyr hardware wrappers

- Type: `bool`
- Menu: Main menu / Hardware
- Defaults: `n` if `SOLAR`
- Depends on: `SOLAR`

Enable Solar's standalone C++23 endpoint descriptors and driver wrappers.
Hardware remains independent of System, lifecycle, and application Devices.

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

Read the resolved image edt.pickle and emit deterministic build-local C++
alias, chosen-node, node-label, and inventory declarations. Explicit Zephyr
descriptor construction remains available when this is disabled.

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

Expose caller-owned C++ adapters over native RTIO contexts and I/O
devices. This does not create an RTIO context, queue, pool, or worker.

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

## `CONFIG_SOLAR_HEALTH`

Enable passive system Health assessment

- Type: `bool`
- Menu: Main menu / Health
- Defaults: `n` if `SOLAR`
- Depends on: `SOLAR`

Build the type-derived Health facility, component reports, checks,
progress markers, bounded histories, and focused evidence adapters.
This does not enable the active Supervisor service.

## `CONFIG_SOLAR_HEALTH_DEFAULT_STALE_MS`

Default self-report stale time in milliseconds

- Type: `int`
- Menu: Main menu / Health
- Defaults: `5000` if `SOLAR_HEALTH and SOLAR`
- Depends on: `SOLAR_HEALTH and SOLAR`
- Range: `1` to `3600000`

No additional help text.

## `CONFIG_SOLAR_HEALTH_HISTORY_DEPTH`

Health transition history depth

- Type: `int`
- Menu: Main menu / Health
- Defaults: `32` if `SOLAR_HEALTH and SOLAR`
- Depends on: `SOLAR_HEALTH and SOLAR`
- Range: `1` to `1024`

No additional help text.

## `CONFIG_SOLAR_HEALTH_ISR_INGRESS_DEPTH`

Health ISR report ingress depth

- Type: `int`
- Menu: Main menu / Health
- Defaults: `16` if `SOLAR_HEALTH and SOLAR`
- Depends on: `SOLAR_HEALTH and SOLAR`
- Range: `1` to `256`

No additional help text.

## `CONFIG_SOLAR_HEALTH_MAX_MONITORS`

Maximum Health monitors

- Type: `int`
- Menu: Main menu / Health
- Defaults: `128` if `SOLAR_HEALTH and SOLAR`
- Depends on: `SOLAR_HEALTH and SOLAR`
- Range: `1` to `1024`

No additional help text.

## `CONFIG_SOLAR_HEALTH_MAX_SUBJECTS`

Maximum Health subjects

- Type: `int`
- Menu: Main menu / Health
- Defaults: `64` if `SOLAR_HEALTH and SOLAR`
- Depends on: `SOLAR_HEALTH and SOLAR`
- Range: `1` to `512`

No additional help text.

## `CONFIG_SOLAR_HEALTH_PROGRESS_GRACE_MS`

Default initial progress grace in milliseconds

- Type: `int`
- Menu: Main menu / Health
- Defaults: `250` if `SOLAR_HEALTH and SOLAR`
- Depends on: `SOLAR_HEALTH and SOLAR`
- Range: `0` to `3600000`

No additional help text.

## `CONFIG_SOLAR_INSPECTION`

Enable generic Inspection collections

- Type: `bool`
- Menu: Main menu / Inspection
- Defaults: `n` if `SOLAR`
- Depends on: `SOLAR`

Enable the passive, stateless collection catalog and bounded query adapters
used by generic local diagnostics and optional Remote discovery. Direct
typed subsystem APIs remain available when this option is disabled.

## `CONFIG_SOLAR_INSPECTION_CBOR`

Enable deterministic Inspection CBOR formatting

- Type: `bool`
- Menu: Main menu / Inspection
- Defaults: `n` if `ZCBOR and SOLAR_INSPECTION and SOLAR`
- Depends on: `ZCBOR and SOLAR_INSPECTION and SOLAR`

No additional help text.

## `CONFIG_SOLAR_INSPECTION_MAX_COLLECTIONS`

Maximum effective Inspection collections

- Type: `int`
- Menu: Main menu / Inspection
- Defaults: `32` if `SOLAR_INSPECTION and SOLAR`
- Depends on: `SOLAR_INSPECTION and SOLAR`
- Range: `1` to `1024`

No additional help text.

## `CONFIG_SOLAR_INSPECTION_MAX_PAGE_RECORDS`

Maximum records in one Inspection page

- Type: `int`
- Menu: Main menu / Inspection
- Defaults: `32` if `SOLAR_INSPECTION and SOLAR`
- Depends on: `SOLAR_INSPECTION and SOLAR`
- Range: `1` to `1024`

No additional help text.

## `CONFIG_SOLAR_INSPECTION_REMOTE`

Expose eligible Inspection collections to Remote

- Type: `bool`
- Menu: Main menu / Inspection
- Defaults: `y` if `SOLAR_REMOTE and SOLAR_REMOTE_RUNTIME_INTROSPECTION and SOLAR_INSPECTION and SOLAR`
- Depends on: `SOLAR_REMOTE and SOLAR_REMOTE_RUNTIME_INTROSPECTION and SOLAR_INSPECTION and SOLAR`
- Selects: `SOLAR_INSPECTION_CBOR` if `SOLAR_REMOTE and SOLAR_REMOTE_RUNTIME_INTROSPECTION and SOLAR_INSPECTION and SOLAR`

Compile Remote discovery and bounded collection query support. Remote
session grants remain authoritative for runtime authorization.

## `CONFIG_SOLAR_INSPECTION_REMOTE_MAX_PAGE_RECORDS`

Maximum records in one Remote Inspection page

- Type: `int`
- Menu: Main menu / Inspection
- Defaults: `8` if `SOLAR_INSPECTION_REMOTE and SOLAR_INSPECTION and SOLAR`
- Depends on: `SOLAR_INSPECTION_REMOTE and SOLAR_INSPECTION and SOLAR`
- Range: `1` to `SOLAR_INSPECTION_MAX_PAGE_RECORDS`

No additional help text.

## `CONFIG_SOLAR_INSPECTION_REMOTE_RESPONSE_BYTES`

Remote Inspection response buffer

- Type: `int`
- Menu: Main menu / Inspection
- Defaults: `1024` if `SOLAR_INSPECTION_REMOTE and SOLAR_INSPECTION and SOLAR`
- Depends on: `SOLAR_INSPECTION_REMOTE and SOLAR_INSPECTION and SOLAR`
- Range: `128` to `65535`

No additional help text.

## `CONFIG_SOLAR_INSPECTION_TEXT_FORMATTING`

Enable bounded Inspection text formatting

- Type: `bool`
- Menu: Main menu / Inspection
- Defaults: `y` if `SOLAR_INSPECTION and SOLAR`
- Depends on: `SOLAR_INSPECTION and SOLAR`

No additional help text.

## `CONFIG_SOLAR_KERNEL_RUNTIME_DIAGNOSTICS`

Enable typed thread runtime statistics

- Type: `bool`
- Menu: Main menu / Kernel integration
- Defaults: `n` if `SOLAR`
- Depends on: `SOLAR`
- Selects: `THREAD_RUNTIME_STATS` if `SOLAR`

Enable Zephyr thread runtime accounting for Solar's focused typed runtime
statistics queries.

## `CONFIG_SOLAR_KERNEL_RUNTIME_STACK_SAFETY`

Enable runtime stack safety checks

- Type: `bool`
- Menu: Main menu / Kernel integration
- Defaults: `n` if `SOLAR`
- Depends on: `SOLAR`
- Selects: `SOLAR_KERNEL_STACK_DIAGNOSTICS` if `SOLAR`, `THREAD_RUNTIME_STACK_SAFETY` if `SOLAR`

Enable Zephyr's full and threshold stack safety checks through Solar's
typed diagnostic surface.

## `CONFIG_SOLAR_KERNEL_STACK_DIAGNOSTICS`

Enable typed thread stack diagnostics

- Type: `bool`
- Menu: Main menu / Kernel integration
- Defaults: `n` if `SOLAR`
- Depends on: `SOLAR`
- Selects: `INIT_STACKS` if `SOLAR`, `THREAD_STACK_INFO` if `SOLAR`

Initialize thread stacks and retain native stack metadata so Solar's
focused stack-usage queries can report high-water usage. This has startup
and storage cost and is therefore opt-in.

## `CONFIG_SOLAR_KERNEL_THREAD_ENUMERATION`

Enable typed global thread enumeration

- Type: `bool`
- Menu: Main menu / Kernel integration
- Defaults: `n` if `SOLAR`
- Depends on: `SOLAR`
- Selects: `THREAD_MONITOR` if `SOLAR`

Retain Zephyr's global thread list and expose explicitly locked and
unlocked typed iteration adapters. Known Solar-owned threads do not need
this option for direct diagnostics.

## `CONFIG_SOLAR_LIFECYCLE_MAX_COMPONENTS`

Maximum effective lifecycle components

- Type: `int`
- Menu: Main menu / Lifecycle
- Defaults: `64` if `SOLAR`
- Depends on: `SOLAR`
- Range: `1` to `65534`

Hard compile-time ceiling for components in one effective Solar System.
Lifecycle storage itself remains derived exactly from the effective graph.

## `CONFIG_SOLAR_LIFECYCLE_REPORT_FAILURE_CAPACITY`

Retained lifecycle report failures

- Type: `int`
- Menu: Main menu / Lifecycle
- Defaults: `8` if `SOLAR`
- Depends on: `SOLAR`
- Range: `1` to `1024`

Number of cleanup or stop failure details retained in bounded summary
reports. Total and truncation counts remain available, and per-component
lifecycle records retain focused outcomes.

## `CONFIG_SOLAR_LOG`

Enable unified Solar logging

- Type: `bool`
- Menu: Main menu / Logging
- Defaults: `n` if `SOLAR_EXECUTION and LOG and SOLAR`
- Depends on: `SOLAR_EXECUTION and LOG and SOLAR`
- Selects: `LOG_FRONTEND` if `SOLAR_EXECUTION and LOG and SOLAR`, `LOG_FRONTEND_ONLY` if `SOLAR_EXECUTION and LOG and SOLAR`

Route typed Solar diagnostics and Zephyr-native LOG_* records through one
bounded Solar-owned stream. Processing uses registered Solar execution;
this option does not create a logging thread or Zephyr backend.

## `CONFIG_SOLAR_LOG_ACCOUNTING`

Retain focused logging accounting

- Type: `bool`
- Menu: Main menu / Logging
- Defaults: `y` if `SOLAR_LOG and SOLAR`
- Depends on: `SOLAR_LOG and SOLAR`

No additional help text.

## `CONFIG_SOLAR_LOG_COMPILE_LEVEL_DEBUG`

Debug

- Type: `bool`
- Menu: Main menu / Logging
- Depends on: `<choice SOLAR_LOG_COMPILE_LEVEL>`

No additional help text.

## `CONFIG_SOLAR_LOG_COMPILE_LEVEL_ERROR`

Error

- Type: `bool`
- Menu: Main menu / Logging
- Depends on: `<choice SOLAR_LOG_COMPILE_LEVEL>`

No additional help text.

## `CONFIG_SOLAR_LOG_COMPILE_LEVEL_INFO`

Info

- Type: `bool`
- Menu: Main menu / Logging
- Depends on: `<choice SOLAR_LOG_COMPILE_LEVEL>`

No additional help text.

## `CONFIG_SOLAR_LOG_COMPILE_LEVEL_NOTICE`

Notice

- Type: `bool`
- Menu: Main menu / Logging
- Depends on: `<choice SOLAR_LOG_COMPILE_LEVEL>`

No additional help text.

## `CONFIG_SOLAR_LOG_COMPILE_LEVEL_TRACE`

Trace

- Type: `bool`
- Menu: Main menu / Logging
- Depends on: `<choice SOLAR_LOG_COMPILE_LEVEL>`

No additional help text.

## `CONFIG_SOLAR_LOG_COMPILE_LEVEL_WARNING`

Warning

- Type: `bool`
- Menu: Main menu / Logging
- Depends on: `<choice SOLAR_LOG_COMPILE_LEVEL>`

No additional help text.

## `CONFIG_SOLAR_LOG_EARLY_RECORDS`

Early Zephyr frontend records retained before System activation

- Type: `int`
- Menu: Main menu / Logging
- Defaults: `4` if `SOLAR_LOG and SOLAR`
- Depends on: `SOLAR_LOG and SOLAR`
- Range: `1` to `32`

No additional help text.

## `CONFIG_SOLAR_LOG_ELEVATED_RESERVE_BYTES`

Warning and error reserve in bytes

- Type: `int`
- Menu: Main menu / Logging
- Defaults: `512` if `SOLAR_LOG and SOLAR`
- Depends on: `SOLAR_LOG and SOLAR`
- Range: `0` to `65535`

No additional help text.

## `CONFIG_SOLAR_LOG_EMERGENCY_BYTES`

Fatal emergency reserve in bytes

- Type: `int`
- Menu: Main menu / Logging
- Defaults: `256` if `SOLAR_LOG and SOLAR`
- Depends on: `SOLAR_LOG and SOLAR`
- Range: `0` to `65535`

No additional help text.

## `CONFIG_SOLAR_LOG_HISTORY`

Enable retained complete-record history

- Type: `bool`
- Menu: Main menu / Logging
- Defaults: `y` if `SOLAR_LOG and SOLAR`
- Depends on: `SOLAR_LOG and SOLAR`

No additional help text.

## `CONFIG_SOLAR_LOG_HISTORY_BYTES`

Retained log history bytes

- Type: `int`
- Menu: Main menu / Logging
- Defaults: `4096` if `SOLAR_LOG_HISTORY and SOLAR_LOG and SOLAR`
- Depends on: `SOLAR_LOG_HISTORY and SOLAR_LOG and SOLAR`
- Range: `128` to `65535`

No additional help text.

## `CONFIG_SOLAR_LOG_INGRESS_BYTES`

Log ingress capacity in bytes

- Type: `int`
- Menu: Main menu / Logging
- Defaults: `4096` if `SOLAR_LOG and SOLAR`
- Depends on: `SOLAR_LOG and SOLAR`
- Range: `256` to `65535`

No additional help text.

## `CONFIG_SOLAR_LOG_MAX_DOMAINS`

Maximum effective log domains

- Type: `int`
- Menu: Main menu / Logging
- Defaults: `32` if `SOLAR_LOG and SOLAR`
- Depends on: `SOLAR_LOG and SOLAR`
- Range: `1` to `65534`

No additional help text.

## `CONFIG_SOLAR_LOG_MAX_HEXDUMP_BYTES`

Maximum copied hexdump size

- Type: `int`
- Menu: Main menu / Logging
- Defaults: `128` if `SOLAR_LOG and SOLAR`
- Depends on: `SOLAR_LOG and SOLAR`
- Range: `1` to `4096`

No additional help text.

## `CONFIG_SOLAR_LOG_MAX_RECORD_BYTES`

Maximum complete log record size

- Type: `int`
- Menu: Main menu / Logging
- Defaults: `384` if `SOLAR_LOG and SOLAR`
- Depends on: `SOLAR_LOG and SOLAR`
- Range: `64` to `4096`

No additional help text.

## `CONFIG_SOLAR_LOG_MAX_SOURCES`

Maximum effective log sources

- Type: `int`
- Menu: Main menu / Logging
- Defaults: `128` if `SOLAR_LOG and SOLAR`
- Depends on: `SOLAR_LOG and SOLAR`
- Range: `1` to `65534`

No additional help text.

## `CONFIG_SOLAR_LOG_MAX_STRING_BYTES`

Maximum copied string argument size

- Type: `int`
- Menu: Main menu / Logging
- Defaults: `96` if `SOLAR_LOG and SOLAR`
- Depends on: `SOLAR_LOG and SOLAR`
- Range: `1` to `1024`

No additional help text.

## `CONFIG_SOLAR_LOG_PROCESSOR_SYSTEM_WORKQUEUE_DEFAULT`

Use Zephyr's system workqueue for log processing

- Type: `bool`
- Menu: Main menu / Logging
- Defaults: `y` if `SOLAR_EXECUTION and SOLAR_LOG and SOLAR`
- Depends on: `SOLAR_EXECUTION and SOLAR_LOG and SOLAR`

Use Zephyr's existing system workqueue when logging configuration does
not name an explicit processor executor. No Solar thread is created.

## `CONFIG_SOLAR_LOG_RENDER_BUFFER_BYTES`

Shared bounded rendering buffer size

- Type: `int`
- Menu: Main menu / Logging
- Defaults: `384` if `SOLAR_LOG and SOLAR`
- Depends on: `SOLAR_LOG and SOLAR`
- Range: `64` to `4096`

No additional help text.

## `CONFIG_SOLAR_LOG_RUNTIME_FILTERING`

Enable mutable runtime log thresholds

- Type: `bool`
- Menu: Main menu / Logging
- Defaults: `y` if `SOLAR_LOG and SOLAR`
- Depends on: `SOLAR_LOG and SOLAR`

No additional help text.

## `CONFIG_SOLAR_LOG_SOURCE_LOCATION`

Retain source location metadata

- Type: `bool`
- Menu: Main menu / Logging
- Defaults: `n` if `SOLAR_LOG and SOLAR`
- Depends on: `SOLAR_LOG and SOLAR`

No additional help text.

## `CONFIG_SOLAR_LOG_STOP_TIMEOUT_MS`

Final log drain timeout (milliseconds)

- Type: `int`
- Menu: Main menu / Logging
- Defaults: `100` if `SOLAR_LOG and SOLAR`
- Depends on: `SOLAR_LOG and SOLAR`
- Range: `0` to `60000`

No additional help text.

## `CONFIG_SOLAR_METRICS`

Enable typed metrics

- Type: `bool`
- Menu: Main menu / Metrics
- Defaults: `y` if `SOLAR`
- Depends on: `SOLAR`

Enable catalog-derived passive counter, gauge, distribution, and timer
instruments with focused readings and records.

## `CONFIG_SOLAR_METRICS_ATOMIC_BACKEND`

Enable lock-free scalar backend

- Type: `bool`
- Menu: Main menu / Metrics
- Defaults: `y` if `SOLAR_METRICS and SOLAR`
- Depends on: `SOLAR_METRICS and SOLAR`

No additional help text.

## `CONFIG_SOLAR_METRICS_DEFAULT_CONCURRENCY_ATOMIC`

Lock-free scalar

- Type: `bool`
- Menu: Main menu / Metrics
- Depends on: `SOLAR_METRICS_ATOMIC_BACKEND and <choice SOLAR_METRICS_DEFAULT_CONCURRENCY>`

No additional help text.

## `CONFIG_SOLAR_METRICS_DEFAULT_CONCURRENCY_AUTOMATIC`

Automatic per-instrument selection

- Type: `bool`
- Menu: Main menu / Metrics
- Depends on: `<choice SOLAR_METRICS_DEFAULT_CONCURRENCY>`

No additional help text.

## `CONFIG_SOLAR_METRICS_DEFAULT_CONCURRENCY_MUTEX`

Zephyr mutex

- Type: `bool`
- Menu: Main menu / Metrics
- Depends on: `SOLAR_METRICS_MUTEX_BACKEND and <choice SOLAR_METRICS_DEFAULT_CONCURRENCY>`

No additional help text.

## `CONFIG_SOLAR_METRICS_DEFAULT_CONCURRENCY_SPINLOCKED`

Zephyr spinlock

- Type: `bool`
- Menu: Main menu / Metrics
- Depends on: `SOLAR_METRICS_SPINLOCK_BACKEND and <choice SOLAR_METRICS_DEFAULT_CONCURRENCY>`

No additional help text.

## `CONFIG_SOLAR_METRICS_DEFAULT_OVERFLOW_REJECT`

Reject

- Type: `bool`
- Menu: Main menu / Metrics
- Depends on: `<choice SOLAR_METRICS_DEFAULT_OVERFLOW>`

No additional help text.

## `CONFIG_SOLAR_METRICS_DEFAULT_OVERFLOW_SATURATE`

Saturate

- Type: `bool`
- Menu: Main menu / Metrics
- Depends on: `<choice SOLAR_METRICS_DEFAULT_OVERFLOW>`

No additional help text.

## `CONFIG_SOLAR_METRICS_DEFAULT_OVERFLOW_WRAP`

Wrap

- Type: `bool`
- Menu: Main menu / Metrics
- Depends on: `<choice SOLAR_METRICS_DEFAULT_OVERFLOW>`

No additional help text.

## `CONFIG_SOLAR_METRICS_MAX_HISTOGRAM_BOUNDARIES`

Maximum histogram boundaries

- Type: `int`
- Menu: Main menu / Metrics
- Defaults: `16` if `SOLAR_METRICS and SOLAR`
- Depends on: `SOLAR_METRICS and SOLAR`
- Range: `1` to `4096`

No additional help text.

## `CONFIG_SOLAR_METRICS_MAX_METRICS`

Maximum metrics

- Type: `int`
- Menu: Main menu / Metrics
- Defaults: `128` if `SOLAR_METRICS and SOLAR`
- Depends on: `SOLAR_METRICS and SOLAR`
- Range: `1` to `65534`

No additional help text.

## `CONFIG_SOLAR_METRICS_MAX_VALUE_BYTES`

Maximum scalar metric value size

- Type: `int`
- Menu: Main menu / Metrics
- Defaults: `8` if `SOLAR_METRICS and SOLAR`
- Depends on: `SOLAR_METRICS and SOLAR`
- Range: `1` to `16`

No additional help text.

## `CONFIG_SOLAR_METRICS_MAX_WINDOW_SIZE`

Maximum exact window reducer capacity

- Type: `int`
- Menu: Main menu / Metrics
- Defaults: `64` if `SOLAR_METRICS and SOLAR`
- Depends on: `SOLAR_METRICS and SOLAR`
- Range: `1` to `4096`

No additional help text.

## `CONFIG_SOLAR_METRICS_MUTEX_BACKEND`

Enable Zephyr mutex backend

- Type: `bool`
- Menu: Main menu / Metrics
- Defaults: `y` if `SOLAR_METRICS and SOLAR`
- Depends on: `SOLAR_METRICS and SOLAR`

No additional help text.

## `CONFIG_SOLAR_METRICS_RUNTIME_RESET`

Enable declared runtime metric reset

- Type: `bool`
- Menu: Main menu / Metrics
- Defaults: `n` if `SOLAR_METRICS and SOLAR`
- Depends on: `SOLAR_METRICS and SOLAR`

No additional help text.

## `CONFIG_SOLAR_METRICS_SPINLOCK_BACKEND`

Enable Zephyr spinlock backend

- Type: `bool`
- Menu: Main menu / Metrics
- Defaults: `y` if `SOLAR_METRICS and SOLAR`
- Depends on: `SOLAR_METRICS and SOLAR`

No additional help text.

## `CONFIG_SOLAR_METRICS_TIMESTAMPS`

Track metric update timestamps

- Type: `bool`
- Menu: Main menu / Metrics
- Defaults: `y` if `SOLAR_METRICS and SOLAR`
- Depends on: `SOLAR_METRICS and SOLAR`

No additional help text.

## `CONFIG_SOLAR_PARAMETERS`

Enable typed runtime parameters

- Type: `bool`
- Menu: Main menu / Parameters
- Defaults: `y` if `SOLAR`
- Depends on: `SOLAR`

Enable compile-time parameter catalogs, exact typed storage, validation,
coherent access, change hooks, and optional persistence. The facility is
included only when the effective System declares parameter architecture.

## `CONFIG_SOLAR_PARAMETERS_DEFAULT_LOAD_FAIL_BOOT`

Fail system boot

- Type: `bool`
- Menu: Main menu / Parameters
- Depends on: `<choice SOLAR_PARAMETERS_DEFAULT_LOAD_FAILURE>`

No additional help text.

## `CONFIG_SOLAR_PARAMETERS_DEFAULT_LOAD_USE_DEFAULT`

Use the declared default and report

- Type: `bool`
- Menu: Main menu / Parameters
- Depends on: `<choice SOLAR_PARAMETERS_DEFAULT_LOAD_FAILURE>`

No additional help text.

## `CONFIG_SOLAR_PARAMETERS_DEFAULT_STOP_CANCEL`

Leave deferred values dirty

- Type: `bool`
- Menu: Main menu / Parameters
- Depends on: `<choice SOLAR_PARAMETERS_DEFAULT_STOP>`

No additional help text.

## `CONFIG_SOLAR_PARAMETERS_DEFAULT_STOP_FLUSH`

Flush deferred values

- Type: `bool`
- Menu: Main menu / Parameters
- Depends on: `<choice SOLAR_PARAMETERS_DEFAULT_STOP>`

No additional help text.

## `CONFIG_SOLAR_PARAMETERS_DEFERRED_SYSTEM_WORKQUEUE_DEFAULT`

Use Zephyr's system workqueue for deferred persistence

- Type: `bool`
- Menu: Main menu / Parameters
- Defaults: `y` if `SOLAR_PARAMETERS_PERSISTENCE and SOLAR_EXECUTION and SOLAR_PARAMETERS and SOLAR`
- Depends on: `SOLAR_PARAMETERS_PERSISTENCE and SOLAR_EXECUTION and SOLAR_PARAMETERS and SOLAR`

Use Zephyr's existing system workqueue when parameter configuration does
not name an explicit persistence executor. No Solar thread is created.

## `CONFIG_SOLAR_PARAMETERS_MAX_CHANGE_HOOKS`

Maximum parameter change hooks

- Type: `int`
- Menu: Main menu / Parameters
- Defaults: `256` if `SOLAR_PARAMETERS and SOLAR`
- Depends on: `SOLAR_PARAMETERS and SOLAR`
- Range: `1` to `65534`

No additional help text.

## `CONFIG_SOLAR_PARAMETERS_MAX_ENCODED_RECORD_BYTES`

Maximum encoded parameter record size

- Type: `int`
- Menu: Main menu / Parameters
- Defaults: `512` if `SOLAR_PARAMETERS_PERSISTENCE and SOLAR_PARAMETERS and SOLAR`
- Depends on: `SOLAR_PARAMETERS_PERSISTENCE and SOLAR_PARAMETERS and SOLAR`
- Range: `32` to `65536`

No additional help text.

## `CONFIG_SOLAR_PARAMETERS_MAX_GROUP_RECORD_BYTES`

Maximum transactional group record size

- Type: `int`
- Menu: Main menu / Parameters
- Defaults: `1024` if `SOLAR_PARAMETERS_PERSISTENCE and SOLAR_PARAMETERS and SOLAR`
- Depends on: `SOLAR_PARAMETERS_PERSISTENCE and SOLAR_PARAMETERS and SOLAR`
- Range: `32` to `65536`

No additional help text.

## `CONFIG_SOLAR_PARAMETERS_MAX_PARAMETERS`

Maximum parameters

- Type: `int`
- Menu: Main menu / Parameters
- Defaults: `128` if `SOLAR_PARAMETERS and SOLAR`
- Depends on: `SOLAR_PARAMETERS and SOLAR`
- Range: `1` to `65534`

No additional help text.

## `CONFIG_SOLAR_PARAMETERS_MAX_VALUE_BYTES`

Maximum parameter value size

- Type: `int`
- Menu: Main menu / Parameters
- Defaults: `256` if `SOLAR_PARAMETERS and SOLAR`
- Depends on: `SOLAR_PARAMETERS and SOLAR`
- Range: `1` to `65536`

No additional help text.

## `CONFIG_SOLAR_PARAMETERS_MIGRATION`

Enable parameter schema migration

- Type: `bool`
- Menu: Main menu / Parameters
- Defaults: `y` if `SOLAR_PARAMETERS_PERSISTENCE and SOLAR_PARAMETERS and SOLAR`
- Depends on: `SOLAR_PARAMETERS_PERSISTENCE and SOLAR_PARAMETERS and SOLAR`

No additional help text.

## `CONFIG_SOLAR_PARAMETERS_PERSISTENCE`

Enable parameter persistence adapters

- Type: `bool`
- Menu: Main menu / Parameters
- Defaults: `y` if `SOLAR_PARAMETERS and SOLAR`
- Depends on: `SOLAR_PARAMETERS and SOLAR`

Compile bounded persistence envelopes and adapter integration. Volatile
parameters do not own persistence buffers or execution work.

## `CONFIG_SOLAR_PARAMETERS_PERSISTENCE_TIMEOUT_MS`

Parameter persistence timeout (milliseconds)

- Type: `int`
- Menu: Main menu / Parameters
- Defaults: `250` if `SOLAR_PARAMETERS_PERSISTENCE and SOLAR_PARAMETERS and SOLAR`
- Depends on: `SOLAR_PARAMETERS_PERSISTENCE and SOLAR_PARAMETERS and SOLAR`
- Range: `0` to `60000`

No additional help text.

## `CONFIG_SOLAR_PARAMETERS_SETTINGS_INITIALIZE`

Initialize the Zephyr settings subsystem from Solar

- Type: `bool`
- Menu: Main menu / Parameters
- Defaults: `n` if `SOLAR_PARAMETERS_ZEPHYR_SETTINGS and SOLAR_PARAMETERS and SOLAR`
- Depends on: `SOLAR_PARAMETERS_ZEPHYR_SETTINGS and SOLAR_PARAMETERS and SOLAR`

Call settings_subsys_init() when the adapter initializes. Leave disabled
when the board or application owns global settings/backend initialization.

## `CONFIG_SOLAR_PARAMETERS_SETTINGS_NAMESPACE`

Zephyr settings namespace

- Type: `string`
- Menu: Main menu / Parameters
- Defaults: `"solar"` if `SOLAR_PARAMETERS_ZEPHYR_SETTINGS and SOLAR_PARAMETERS and SOLAR`
- Depends on: `SOLAR_PARAMETERS_ZEPHYR_SETTINGS and SOLAR_PARAMETERS and SOLAR`

Root key used by the typed Zephyr settings adapter. Stable parameter and
group IDs form the remainder of each key.

## `CONFIG_SOLAR_PARAMETERS_ZEPHYR_SETTINGS`

Enable the Zephyr settings persistence adapter

- Type: `bool`
- Menu: Main menu / Parameters
- Defaults: `n` if `SOLAR_PARAMETERS_PERSISTENCE and SETTINGS and SOLAR_PARAMETERS and SOLAR`
- Depends on: `SOLAR_PARAMETERS_PERSISTENCE and SETTINGS and SOLAR_PARAMETERS and SOLAR`

No additional help text.

## `CONFIG_SOLAR_REMOTE`

Solar typed Remote protocol

- Type: `bool`
- Menu: Main menu / Remote protocol
- Defaults: `n` if `SOLAR`
- Depends on: `SOLAR`
- Selects: `COBS` if `SOLAR`, `CRC` if `SOLAR`, `ZCBOR` if `SOLAR`, `ZCBOR_CANONICAL` if `SOLAR`

Include Remote's typed schemas, deterministic CBOR and packed codecs,
protocol envelope, CRC32C integrity, and COBS framing. Runtime links and
service resources are emitted only by an effective Stage 14 configuration.

## `CONFIG_SOLAR_REMOTE_BUILD_ID`

Remote firmware build identity

- Type: `hex`
- Menu: Main menu / Remote protocol
- Defaults: `0` if `SOLAR_REMOTE and SOLAR`
- Depends on: `SOLAR_REMOTE and SOLAR`

Application or release-specific 64-bit build identity reported by Remote
server information. The manifest digest remains the interface identity.

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

Extract the bound Remote manifest from the final Zephyr ELF and emit
canonical CBOR, reviewable JSON, Python/C++ constants, and SHA-256 digest
under the build directory. Disable only for protocol-only library tests
that intentionally bind no System.

## `CONFIG_SOLAR_REMOTE_ISR_PUBLICATION`

Enable constrained ISR Push publication

- Type: `bool`
- Menu: Main menu / Remote protocol
- Defaults: `n` if `SOLAR_REMOTE and SOLAR`
- Depends on: `SOLAR_REMOTE and SOLAR`

Expose remote::write_from_isr for trivially copyable Push values whose
Latest ingress path can complete without allocation, waiting, or mutexes.

## `CONFIG_SOLAR_REMOTE_MANIFEST_RETRIEVAL`

Allow bounded runtime manifest retrieval

- Type: `bool`
- Menu: Main menu / Remote protocol
- Defaults: `y` if `SOLAR_REMOTE and SOLAR`
- Depends on: `SOLAR_REMOTE and SOLAR`

Advertise the exact embedded manifest digest and allow authenticated
clients with Observe permission to retrieve the image in bounded chunks.

## `CONFIG_SOLAR_REMOTE_MAX_ENDPOINTS`

Maximum effective Remote endpoints per identity domain

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

Maximum slots in one inbound stream window per link

- Type: `int`
- Menu: Main menu / Remote protocol
- Defaults: `8` if `SOLAR_REMOTE and SOLAR`
- Depends on: `SOLAR_REMOTE and SOLAR`
- Range: `1` to `64`

Compile-time ceiling for a typed ReliableWindow. Storage remains absent
for Data declarations without an InStream capability.

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

Maximum negotiated Remote stream rate

- Type: `int`
- Menu: Main menu / Remote protocol
- Defaults: `1000` if `SOLAR_REMOTE and SOLAR`
- Depends on: `SOLAR_REMOTE and SOLAR`
- Range: `1` to `1000000`

Hard runtime ceiling applied after an endpoint's typed MaxRate policy.
Subscription responses report the effective clamped interval.

## `CONFIG_SOLAR_REMOTE_OUTBOUND_MESSAGE_SLOTS`

Concurrent fragmented outbound messages per link

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

Number of independently admitted output classes: protocol control,
responses, important publications, telemetry, and bulk streams.

## `CONFIG_SOLAR_REMOTE_OUTPUT_LANE_DEPTH`

Queued frames per Remote output lane

- Type: `int`
- Menu: Main menu / Remote protocol
- Defaults: `4` if `SOLAR_REMOTE and SOLAR`
- Depends on: `SOLAR_REMOTE and SOLAR`
- Range: `1` to `64`

Bounds independently queued protocol, response, important, telemetry,
and bulk frames for each link session. Response payload ownership remains
reserved separately by CONFIG_SOLAR_REMOTE_MAX_REQUESTS.

## `CONFIG_SOLAR_REMOTE_PACKED`

Enable explicit packed stream schemas

- Type: `bool`
- Menu: Main menu / Remote protocol
- Defaults: `y` if `SOLAR_REMOTE and SOLAR`
- Depends on: `SOLAR_REMOTE and SOLAR`

No additional help text.

## `CONFIG_SOLAR_REMOTE_REASSEMBLY_SLOTS`

Concurrent fragmented-message reassembly slots per link

- Type: `int`
- Menu: Main menu / Remote protocol
- Defaults: `2` if `SOLAR_REMOTE and SOLAR`
- Depends on: `SOLAR_REMOTE and SOLAR`
- Range: `1` to `16`

No additional help text.

## `CONFIG_SOLAR_REMOTE_REASSEMBLY_TIMEOUT_MS`

Fragmented-message reassembly timeout (milliseconds)

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

Admit the protocol-summary introspection request using the same effective
catalogs that generate the host manifest. Generic descriptor paging is
supplied by Solar Inspection and remains absent when that subsystem is not
selected.

## `CONFIG_SOLAR_REMOTE_SERVICE_PRIORITY`

Remote service cooperative priority offset

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

## `CONFIG_SOLAR_SERVICE_ABORT_ON_STOP_TIMEOUT`

Abort Solar-owned service execution after stop timeout

- Type: `bool`
- Menu: Main menu / Lifecycle
- Defaults: `y` if `SOLAR`
- Depends on: `SOLAR`

Permit the default service containment policy to abort only execution
created and owned by Solar after cooperative stop times out.

## `CONFIG_SOLAR_SERVICE_STACK_SIZE`

Default service thread stack size

- Type: `int`
- Menu: Main menu / Lifecycle
- Defaults: `2048` if `SOLAR`
- Depends on: `SOLAR`
- Range: `256` to `1048576`

Default stack allocation used by an explicit Solar service whose typed
execution declaration omits StackSize. Every service remains visible in
the application Blueprint and owns exactly one such stack.

## `CONFIG_SOLAR_SERVICE_STOP_TIMEOUT_MS`

Default service stop timeout (milliseconds)

- Type: `int`
- Menu: Main menu / Lifecycle
- Defaults: `100` if `SOLAR`
- Depends on: `SOLAR`
- Range: `0` to `60000`

Default finite join timeout for Solar-owned service execution. Typed
component or Blueprint policy may override this value.

## `CONFIG_SOLAR_STRICT_CATALOG_BINDING`

Use strict compile-time catalog binding

- Type: `bool`
- Menu: Main menu
- Defaults: `n` if `SOLAR`
- Depends on: `SOLAR`

Resolve bound subsystem operations and catalog membership at compile time.
Leave disabled for the lower-friction runtime-bound frontend used during
normal development and prototyping.

## `CONFIG_SOLAR_SUPERVISOR`

Solar active system supervisor

- Type: `bool`
- Menu: Main menu / Supervisor
- Defaults: `n` if `SOLAR_HEALTH and SOLAR_EXECUTION and SOLAR`
- Depends on: `SOLAR_HEALTH and SOLAR_EXECUTION and SOLAR`

Include one dedicated Solar Supervisor service. The service refreshes
Health, evaluates explicit typed response policy, and gates an optional
watchdog provider from its own execution domain.

## `CONFIG_SOLAR_SUPERVISOR_CYCLE_GRACE_MS`

Supervision cycle overrun grace (milliseconds)

- Type: `int`
- Menu: Main menu / Supervisor
- Defaults: `100` if `SOLAR_SUPERVISOR and SOLAR`
- Depends on: `SOLAR_SUPERVISOR and SOLAR`
- Range: `0` to `60000`

No additional help text.

## `CONFIG_SOLAR_SUPERVISOR_MAX_RESPONSES_PER_CYCLE`

Maximum response attempts per cycle

- Type: `int`
- Menu: Main menu / Supervisor
- Defaults: `8` if `SOLAR_SUPERVISOR and SOLAR`
- Depends on: `SOLAR_SUPERVISOR and SOLAR`
- Range: `1` to `256`

No additional help text.

## `CONFIG_SOLAR_SUPERVISOR_PERIOD_MS`

Base supervision period (milliseconds)

- Type: `int`
- Menu: Main menu / Supervisor
- Defaults: `500` if `SOLAR_SUPERVISOR and SOLAR`
- Depends on: `SOLAR_SUPERVISOR and SOLAR`
- Range: `10` to `60000`

No additional help text.

## `CONFIG_SOLAR_SUPERVISOR_RECOVERY_ATTEMPTS`

Default recovery attempt ceiling per incident

- Type: `int`
- Menu: Main menu / Supervisor
- Defaults: `3` if `SOLAR_SUPERVISOR and SOLAR`
- Depends on: `SOLAR_SUPERVISOR and SOLAR`
- Range: `1` to `32`

No additional help text.

## `CONFIG_SOLAR_SUPERVISOR_RECOVERY_COOLDOWN_MS`

Default delay between recovery attempts (milliseconds)

- Type: `int`
- Menu: Main menu / Supervisor
- Defaults: `1000` if `SOLAR_SUPERVISOR and SOLAR`
- Depends on: `SOLAR_SUPERVISOR and SOLAR`
- Range: `0` to `60000`

No additional help text.

## `CONFIG_SOLAR_SUPERVISOR_RESPONSE_HISTORY_DEPTH`

Retained Supervisor response records

- Type: `int`
- Menu: Main menu / Supervisor
- Defaults: `32` if `SOLAR_SUPERVISOR and SOLAR`
- Depends on: `SOLAR_SUPERVISOR and SOLAR`
- Range: `1` to `256`

No additional help text.

## `CONFIG_SOLAR_SUPERVISOR_SERVICE_PRIORITY`

Supervisor preemptive priority

- Type: `int`
- Menu: Main menu / Supervisor
- Defaults: `1` if `SOLAR_SUPERVISOR and SOLAR`
- Depends on: `SOLAR_SUPERVISOR and SOLAR`
- Range: `0` to `15`

No additional help text.

## `CONFIG_SOLAR_SUPERVISOR_SERVICE_STACK_SIZE`

Supervisor service stack size

- Type: `int`
- Menu: Main menu / Supervisor
- Defaults: `4096` if `SOLAR_SUPERVISOR and SOLAR`
- Depends on: `SOLAR_SUPERVISOR and SOLAR`
- Range: `1024` to `65536`

No additional help text.
