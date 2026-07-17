# Devicetree Inputs

Solar Hardware consumes Zephyr's resolved EDT; it defines no parallel board
description. Supported generated selectors originate from:

- `/aliases` properties;
- `/chosen` properties;
- node labels;
- enabled GPIO specifiers;
- addressed SPI and I2C child nodes;
- ADC `io-channels` and PWM `pwms` property endpoints;
- enabled UART, Counter, and Watchdog controller nodes.

The exact generated catalog follows the enabled Solar hardware family options,
the matching Zephyr driver options, node status, binding data, and the final
overlay-resolved image.

## Generated files

The module build writes:

- `include/generated/solar/hardware/generated/devicetree.hpp`: compile-time
  selectors and descriptors;
- `solar/hardware/devicetree.json`: resolved metadata for diagnostics and host
  tooling.

Both are build products. The DTS, overlay, bindings, Kconfig, and generator are
canonical source.

## Target limits

Not every Zephyr driver family or asynchronous mode is supported by every
target. A generated selector proves an endpoint was resolved; `ready()` and
driver results still report runtime device and capability state. Direct DMA
programming and SoC-specific APIs remain direct Zephyr responsibilities.
