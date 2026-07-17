# Hardware

`solar::hardware` is a typed C++23 layer over Zephyr devicetree descriptors and
driver APIs. Devicetree, pinctrl, Zephyr devices, and Zephyr drivers remain the
canonical hardware model.

## Boundary

Hardware wrappers are not Solar components and have no automatic lifecycle,
Health, Inspection, or graph entry. A project gives endpoints semantic board
names; an application Device adds lifecycle, domain behavior, and recovery.

```cpp
namespace board {
using StatusLed = solar::hardware::gpio::Output<
    solar::hardware::dt::alias<"status-led">>;
}
```

## Driver families

| Family | Principal surfaces |
| --- | --- |
| GPIO | Input, Output, InterruptInput, callback ownership |
| SPI | Endpoint, Controller, Session, callback and RTIO operations |
| I2C | Endpoint, Controller, transfer/register helpers, callback and RTIO |
| UART | Polling, interrupt-driven, and asynchronous roles |
| ADC | Channel, caller-buffered Sequence, async reads, RTIO stream |
| PWM | Output, validated duty cycle, capture |
| Counter | Counter, fixed-channel Alarm, Top |
| Watchdog | Device, validated Timeout, move-only installed channel |

Each family is selected by its `CONFIG_SOLAR_HARDWARE_*` option and the
corresponding Zephyr driver capability. Intentional use of a disabled family
fails at compile time rather than becoming a runtime stub.

## Async and DMA

Async operations and buffers are caller-owned and must remain alive until
completion or confirmed cancellation. Wrappers create no worker, queue, or
heap allocation.

Ordinary peripheral calls automatically benefit when their Zephyr driver uses
DMA. Solar does not add a portable direct-DMA abstraction; advanced
SoC-specific DMA code should use Zephyr directly. RTIO adapters are available
where the underlying driver supports them, with caller-owned queues and
completion entries.

Solar's EDT generator emits typed selectors and metadata from Zephyr's resolved
build artifact. It does not replace DTS or infer application Devices. See
{doc}`../how-to/generate-hardware-aliases`,
{doc}`../reference/api/hardware`, and
{doc}`../architecture/hardware-and-devicetree`.
