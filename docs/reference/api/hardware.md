# Hardware API

Include `<solar/hardware.hpp>`. Driver families are conditionally present
according to Kconfig and Zephyr driver support.

- `solar::hardware::dt`: endpoint descriptors and generated selectors;
- `gpio`, `spi`, `i2c`, `uart`, `adc`, `pwm`, `counter`, `watchdog`: driver roles;
- `async`: caller-owned operation support;
- `rtio`: caller-owned RTIO context and completion entries.

Every fallible operation returns `Result<T, hardware::Error>`. The error keeps
the broad `Status`, focused reason and operation, native Zephyr result, and
resolved endpoint path where available. See {doc}`../../subsystems/hardware`.
