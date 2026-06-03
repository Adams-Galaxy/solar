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

- vendored or symlinked inside an app, with `target_include_directories`;
- listed as a west module, where `zephyr/module.yml` adds Solar's include path.

When Zephyr loads Solar as a module, Solar's `CMakeLists.txt` calls
`zephyr_include_directories(include)`. Outside Zephyr, the same `CMakeLists.txt`
exposes an interface target for editor tooling and lightweight package checks.

## Application Build

A Solar firmware app is a Zephyr application:

```cmake
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(robot_firmware LANGUAGES CXX)

target_sources(app PRIVATE src/main.cpp)
target_include_directories(app PRIVATE include lib/solar/include)
```

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
