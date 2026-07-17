# Requirements

Solar is a Zephyr module and follows Zephyr's workspace, board, Kconfig, and
devicetree model. Before using Solar, prepare:

- a Zephyr workspace with `west`;
- Zephyr 4.4 or a compatible newer revision;
- a compiler and standard library with C++23 `std::expected` support;
- CMake 3.20 or newer;
- Python for Zephyr and Solar's optional generators;
- a board supported by the selected Zephyr revision.

The examples use `native_sim/native/64`, which builds firmware as a native host
executable while retaining Zephyr's kernel and configuration behavior. It is
the fastest way to learn and test Solar without hardware.

## Required Kconfig

Every Solar application enables:

```text
CONFIG_CPP=y
CONFIG_STD_CPP23=y
CONFIG_REQUIRES_FULL_LIBCPP=y
CONFIG_SOLAR=y
```

Subsystems add their own symbols. Do not enable all subsystems preemptively:
their Kconfig options own storage capacities, Zephyr dependencies, and runtime
resources.

## No Dynamic Runtime Requirement

Solar's core model is statically owned and does not require dynamic allocation.
`CONFIG_SOLAR_ALLOW_DYNAMIC_ALLOCATION` defaults off. Optional capabilities may
use allocation only when their own contracts say so.

See {doc}`../reference/compatibility` for the verified version and target
matrix.
