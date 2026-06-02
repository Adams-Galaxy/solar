# Solar

Solar is a static-first C++20 runtime package for robotics firmware built on Zephyr.

The robot shape is declared as a compile-time graph: boards, peripherals, devices,
facilities, services, tasks, channels, logging, metrics, events, and Remote
vocabulary are all named types that Solar can validate before runtime.

Current version: `0.1.0`

Solar is under active development. APIs are expected to evolve quickly while the
architecture settles.

## Core Ideas

- Static graph declaration with `solar::System<...>`.
- Type-level names with `solar::Name<"...">`.
- Tuple-owned runtime objects with checked lookup through `solar::Context`.
- Passive facilities under `solar::facilities`.
- Active threaded services under `solar::services`.
- Static contribution catalogs for metrics, events, and Remote vocabulary.
- Fixed-capacity runtime structures and no registry-driven architecture.
- Zephyr-backed RTOS primitives exposed through `solar::rtos`.

## Example Shape

```cpp
using Robot = solar::System<
    Board,
    solar::Peripherals<Console>,
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

## Zephyr Package Use

Solar can be included directly from a Zephyr application or loaded as a Zephyr
module. The package exposes headers under `include/solar` and expects Zephyr to
provide kernel, driver, devicetree, board, and simulator support.

If Solar is vendored into an application, add the include path in the app
`CMakeLists.txt`:

```cmake
target_include_directories(app PRIVATE lib/solar/include)
```

If Solar is listed as a west module, `zephyr/module.yml` exposes the include path
to the Zephyr build automatically.

## Zephyr App Entry

Profiles are ordinary C++ types. A Zephyr application can boot one with:

```cpp
#include "app/robot.hpp"

int main()
{
    return solar::entry::run_zephyr<app::Robot>();
}
```

Zephyr owns kernel startup, scheduling, board selection, device drivers, and the
native simulator. Solar owns static graph construction, facility setup, lifecycle
dispatch, service threads, and observability.

## Native Simulator And Local Smoke Builds

From a configured Zephyr workspace on Linux:

```sh
west build -p auto -b native_sim/native/64 firmware
west build -t run
```

On macOS, use `qemu_x86_64` for a local Zephyr smoke build because Zephyr's
native simulator is POSIX/Linux-only:

```sh
west build -p auto -b qemu_x86_64 firmware
west build -t run
```

Use a hardware board name, such as `teensy40`, when moving from the native
simulator to a physical target.

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
- [Zephyr](docs/zephyr.md)

## Repository Layout

```text
include/solar/      Public Solar headers
remote/             Built-in Remote schema definitions
docs/               Architecture and subsystem documentation
zephyr/module.yml   Zephyr module metadata
CMakeLists.txt      Zephyr module include setup and standalone interface target
VERSION             Current package version
```

## License

Solar is licensed under the MIT License. See [LICENSE](LICENSE) for the full text.
