# System Graph

Solar's central idea is that the robot shape is a static type graph. The application declares the graph once, and Solar derives validation, lifecycle order, focused inspection, and contribution catalogs from that declaration.

```cpp
using Robot = solar::System<
    Board,
    solar::Peripherals<Usb, Uart1>,
    solar::Devices<LeftMotor, Imu>,
    solar::Facilities<solar::facilities::Events, solar::facilities::Metrics>,
    solar::Services<solar::services::Remote<RemoteTransport>>,
    solar::Tasks<>,
    solar::Channels<Telemetry>,
    solar::Runtime<solar::Logging<Logger>>>;
```

## Graph Groups

- `Peripherals<...>`: configured hardware-facing objects such as serial ports or buses.
- `Devices<...>`: robot devices and sensors.
- `Facilities<...>`: passive shared capability and state.
- `Services<...>`: active runtime actors, each with a thread.
- `Tasks<...>`: lower-level Kernel task entries.
- `Channels<...>`: fixed-depth typed queues.
- `Runtime<...>`: temporary C++ type policy such as logger selection. Scalar
  firmware configuration belongs in Kconfig.

Every graph component must expose:

```cpp
using Name = solar::Name<"stable_name">;
```

Names are type-level values. This lets Solar reject duplicates at compile time and perform checked lookup without runtime registries.

## Dependencies

Components declare required dependencies by concrete type:

```cpp
using Dependencies = solar::Dependencies<
    Imu,
    LeftMotor>;
```

Solar validates component and service uniqueness by type, required dependency
presence, diagnostic-name uniqueness, and dependency cycles at compile time.
Initialization and start follow a generated topological order. Stop and deinit
follow the reverse order.

## Static Ownership

`System` is a non-constructible static type. Components own their own static
state and expose application APIs directly. Solar owns lifecycle records,
reports, and one Zephyr runtime wrapper per registered service type; it does not
instantiate component or service objects.

## Lifecycle

Lifecycle hooks are optional. Solar calls the hooks only when they exist:

```cpp
static Status init();
static Status start();
static Status stop();
static Status deinit();
```

Return types can be `void`, `bool`, `solar::Status`, or `solar::Result<void>`. Solar normalizes them into `Status` during boot.

Boot initializes every component in topological order, establishes a complete
initialization barrier, then starts components in the same dependency order.
Services implement `static run(solar::StopToken)` and receive their own thread.

Boot failures are stored in `BootReport` with phase, operation, status, and a
stable component descriptor. Completed work is stopped and deinitialized in
reverse order after a partial failure.

## Focused Queries

The system exposes separate static query areas:

```cpp
auto state = RobotSystem::lifecycle::state();
auto components = RobotSystem::lifecycle::components();
auto remote = RobotSystem::lifecycle::record<RemoteService>();

auto graph = RobotSystem::graph::components();
auto remote_descriptor = RobotSystem::graph::component<RemoteService>();
auto remote_dependencies = RobotSystem::graph::dependencies<RemoteService>();

auto threads = RobotSystem::kernel::service_threads();
auto remote_thread = RobotSystem::kernel::thread<RemoteService>();
```

Lifecycle queries return mutex-protected copies. Graph descriptors are immutable
compile-time data. Kernel queries report Solar-owned execution state and mark
optional diagnostics unavailable when the corresponding Zephyr feature is not
configured. Inspection and Remote format these canonical facts; they do not own
another universal snapshot.
