# Solar

Solar is a static-first C++20 runtime layer for robotics firmware. It is designed for systems where the robot shape is known at compile time: boards, peripherals, devices, facilities, services, tasks, channels, and observability vocabulary are declared as a type graph and validated before runtime.

Current version: `0.1.0`

Solar is under active development. APIs are expected to evolve quickly while the architecture settles.

## Core Ideas

- Static graph declaration with `solar::System<...>`.
- Type-level names with `solar::Name<"...">`.
- Tuple-owned runtime objects with checked lookup through `solar::Context`.
- Passive facilities under `solar::facilities`.
- Active threaded services under `solar::services`.
- Static contribution catalogs for metrics, events, and Remote vocabulary.
- Fixed-capacity runtime structures and no registry-driven architecture.
- Portable RTOS facade over project-provided low-level backends.

## Example Shape

```cpp
using Robot = solar::System<
    Board,
    solar::Peripherals<UsbSerial>,
    solar::Devices<LeftMotor, Imu>,
    solar::Facilities<
        solar::facilities::Events,
        solar::facilities::Metrics,
        solar::facilities::Inspection>,
    solar::Services<
        solar::services::Remote<RemoteTransport>>,
    solar::Tasks<>,
    solar::Channels<>,
    solar::Runtime<
        solar::Logging<Logger>,
        solar::Config<AppConfig>>>;
```

## Documentation

Start with [docs/README.md](docs/README.md).

Important sections:

- [Architecture](docs/architecture.md)
- [System Graph](docs/concepts/system-graph.md)
- [Facilities And Services](docs/concepts/facilities-services.md)
- [Contributions](docs/concepts/contributions.md)
- [Logging](docs/observability/logging.md)
- [Events](docs/observability/events.md)
- [Metrics](docs/observability/metrics.md)
- [Remote](docs/remote.md)
- [RTOS](docs/rtos.md)

## Building Host Tests

Solar is header-oriented, but the host tests compile the current graph and simulated low-level backend.

From the repository root:

```sh
cmake -S solar -B solar/build -DSOLAR_BUILD_TESTS=ON
cmake --build solar/build
ctest --test-dir solar/build --output-on-failure
```

If your `low_level/...` headers live outside the default project layout, pass:

```sh
cmake -S solar -B solar/build \
  -DSOLAR_LOW_LEVEL_INCLUDE_DIR=/path/to/include \
  -DSOLAR_LOW_LEVEL=SIMULATED
```

`SOLAR_LOW_LEVEL` can be `SIMULATED`, `TEENSYDUINO`, or `NONE`.

## Repository Layout

```text
include/solar/      Public Solar headers
remote/            Built-in Remote schema definitions
tests/host/        Host compile/runtime checks
docs/              Architecture and subsystem documentation
CMakeLists.txt     Host test and package build entry
VERSION            Current package version
```

## Low-Level Boundary

Solar expects a project-provided `low_level` facade for hardware and RTOS bindings. This keeps Solar portable while allowing firmware and host simulation to share application code.

See [docs/low-level.md](docs/low-level.md).

## License

Solar is licensed under the MIT License. See [LICENSE](LICENSE) for the full text.
