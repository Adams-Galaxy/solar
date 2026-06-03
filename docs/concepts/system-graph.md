# System Graph

Solar's central idea is that the robot shape is a static type graph. The application declares the graph once, and Solar derives ownership, lookup, validation, lifecycle order, snapshots, and contribution catalogs from that declaration.

```cpp
using Robot = solar::System<
    Board,
    solar::Peripherals<Usb, Uart1>,
    solar::Devices<LeftMotor, Imu>,
    solar::Facilities<solar::facilities::Events, solar::facilities::Metrics>,
    solar::Services<solar::services::Remote<RemoteTransport>>,
    solar::Tasks<>,
    solar::Channels<Telemetry>,
    solar::Runtime<
        solar::Logging<Logger>,
        solar::Config<AppConfig>>>;
```

## Graph Groups

- `Peripherals<...>`: configured hardware-facing objects such as serial ports or buses.
- `Devices<...>`: robot devices and sensors.
- `Facilities<...>`: passive shared capability and state.
- `Services<...>`: active runtime actors, each with a thread.
- `Tasks<...>`: lower-level Kernel task entries.
- `Channels<...>`: fixed-depth typed queues.
- `Runtime<...>`: cross-system policy such as logger and config.

Every graph component must expose:

```cpp
using Name = solar::Name<"stable_name">;
```

Names are type-level values. This lets Solar reject duplicates at compile time and perform checked lookup without runtime registries.

## Dependencies

Components can declare dependencies by name:

```cpp
using Dependencies = solar::Dependencies<
    solar::Name<"imu">,
    solar::Name<"left_motor">>;
```

Solar validates that every dependency exists somewhere in the graph. Dependencies are currently validation and documentation vocabulary; deeper lifecycle ordering can build on the same model.

## Runtime Ownership

`System` owns runtime objects in tuples. Access can be by concrete type:

```cpp
auto& imu = system.Device<Imu>();
auto& remote = system.Service<Remote>();
```

or by stable name:

```cpp
auto& imu = ctx.Get<solar::Name<"imu">>();
```

`Context<SystemT>` is the lightweight view passed to lifecycle hooks and service run loops. It does not own anything; it only provides typed access to the owning system.

## Lifecycle

Lifecycle hooks are optional. Solar calls the hooks only when they exist:

```cpp
Status init(Context&);
Status start(Context&);
Status stop(Context&);
```

Return types can be `void`, `bool`, `solar::Status`, or `solar::Result<void>`. Solar normalizes them into `Status` during boot.

Boot order is deterministic:

1. Board init.
2. Peripheral init/start.
3. Facility init/start.
4. Device init.
5. Service init.
6. Device start.
7. Service threads start.
8. Task init/start.

Boot failures are stored in `BootReport` with phase, status, and component label.

## Snapshots

`System::Snapshots()` returns lightweight static graph entries:

```cpp
struct ComponentSnapshot {
    const char* name;
    const char* kind;
    LifecycleState state;
};
```

These snapshots are intended for Remote/inspection surfaces. Richer runtime health and metrics should be exposed through facilities and observability catalogs.
