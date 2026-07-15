# Zephyr

Solar is built directly on Zephyr.

Zephyr provides:

- kernel startup and scheduling;
- board and devicetree selection;
- drivers for GPIO, UART, I2C, SPI, timers, storage, networking, and USB;
- native simulator targets for host execution;
- build orchestration through west and CMake.

Solar provides:

- static robot graph construction;
- lifecycle dispatch;
- service thread ownership;
- logging, events, metrics, and Remote vocabulary;
- typed runtime wrappers where Solar benefits from a consistent API.

## Boundary

Solar has no custom platform-switching layer. Code that needs kernel
behavior uses `solar::kernel`, which wraps Zephyr primitives. Code that needs
hardware should use Zephyr devices directly or project-owned typed adapters
built from Zephyr devicetree and driver APIs.

## Simulation

The host simulation path is Zephyr's native simulator target. This means the
same Solar profile can run as a Linux/macOS-hosted Zephyr application while
still using the Zephyr kernel model.

## Package Build Shape

Solar is a Zephyr package. It can be consumed in two clean ways:

- added through `ZEPHYR_EXTRA_MODULES` when vendored or symlinked inside an app;
- listed as a west module in the workspace manifest.

When Zephyr loads Solar as a module, Solar's `CMakeLists.txt` calls
`zephyr_include_directories(include)` and its module metadata includes Solar's
Kconfig tree. Outside Zephyr, the same `CMakeLists.txt` exposes an interface
target for editor tooling and lightweight package checks.

## Application Build

A Solar firmware app is a Zephyr application:

```cmake
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/lib/solar)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(robot_firmware LANGUAGES CXX)

target_sources(app PRIVATE src/main.cpp)
target_include_directories(app PRIVATE include)
```

The module must be registered before `find_package(Zephyr ...)` so Zephyr can
load both its CMake integration and Kconfig symbols.

## Kconfig

Solar provides firmware-wide defaults through normal Zephyr Kconfig options:

```ini
CONFIG_SOLAR=y
CONFIG_SOLAR_KERNEL=y
CONFIG_SOLAR_KERNEL_DIAGNOSTICS=y
CONFIG_SOLAR_SERVICE_STOP_TIMEOUT_MS=100
CONFIG_SOLAR_SERVICE_ABORT_ON_STOP_TIMEOUT=y
```

`CONFIG_SOLAR_KERNEL_RUNTIME_STATS` enables Solar's runtime-stat query support,
but the application must also enable the corresponding Zephyr thread runtime
statistics options.

Execution-policy types may override values documented as defaults, such as the
service stop timeout. Kconfig options that exclude code or prohibit a capability
are hard gates and cannot be re-enabled by a C++ type policy.

The precedence for overridable defaults is:

```text
explicit component or execution policy
    overrides Kconfig default
```

Solar does not provide non-Zephyr configuration fallbacks. Public Solar headers
expect Zephyr's generated Kconfig definitions.

## Native Simulator

Use Zephyr's native simulator as the preferred Linux host execution path:

```sh
west build -p auto -b native_sim/native/64 firmware
west build -t run
```

Zephyr's native simulator uses the POSIX architecture and does not configure on
macOS. On macOS, use a QEMU board such as `qemu_x86_64` for local smoke tests,
or run native sim from Linux/a VM.

The app should still enter through a normal Zephyr `main()` and call:

```cpp
return solar::entry::run_zephyr<app::Robot>();
```
