# Solar Architecture

Solar is a static-first C++20 runtime layer for robotics firmware. The robot is declared as a compile-time graph, validated with type traits/concepts, and instantiated as tuple-owned runtime objects.

Solar is under active development. The current package documents the intended architecture as it exists today.

## Design Principles

- Robot shape is explicit in `solar::System<...>`.
- Component identity is type-level through `solar::Name<"...">`.
- Normal runtime paths avoid heap allocation and runtime registries.
- Facilities are passive state/capability.
- Services are active threaded actors.
- Metrics, events, and Remote vocabulary are contributed by their owning components and collected by `System`.
- Platform and low-level hardware bindings are project-owned boundaries, not Solar core.

## Static Graph

```cpp
using Robot = solar::System<
    Board,
    solar::Peripherals<...>,
    solar::Devices<...>,
    solar::Facilities<...>,
    solar::Services<...>,
    solar::Tasks<...>,
    solar::Channels<...>,
    solar::Runtime<...>>;
```

Solar validates component names, dependencies, and contribution catalog uniqueness at compile time. Runtime access goes through `solar::Context` and typed lookup:

```cpp
auto& imu = ctx.Get<solar::Name<"imu">>();
```

See [System Graph](concepts/system-graph.md).

## Runtime Model

Lifecycle hooks are optional:

```cpp
init(ctx)
start(ctx)
stop(ctx)
```

Solar calls hooks only when implemented and normalizes `void`, `bool`, `Status`, and `Result<void>` into `Status`.

Services additionally implement:

```cpp
run(ctx, stop_token)
```

Solar starts each service on its own RTOS thread. There is no Solar polling service model.

See [Facilities And Services](concepts/facilities-services.md).

## Observability

Solar keeps observability APIs separate while sharing naming, contribution, and Remote exposure patterns.

- [Logging](observability/logging.md): direct fan-out records through typed sinks.
- [Events](observability/events.md): typed facts with fixed history and optional sinks.
- [Metrics](observability/metrics.md): typed observable state with policies, units, timers, and snapshots.

The important distinction is:

- logs/events are emitted records;
- metrics are queryable state.

## Remote

`solar::services::Remote` is the binary control and introspection service. It uses COBS-framed packets with CRC16, generated schema descriptors, and stable 32-bit IDs.

Remote uses system-collected catalogs by default, so component-owned methods, topics, observables, and types appear without runtime registration.

See [Remote](remote.md).

## Entry And Simulation

Applications define profile headers such as:

- `include/app/robot.hpp`
- `include/app/simulated.hpp`

Solar entry owns construction and boot. Profiles provide the graph and optional hooks such as `preflight`, `awake`, `failed`, `finished`, and `exit_code`.

See [Entry And Profiles](entry-and-profiles.md).

## Low Level And RTOS

Solar core depends on stable public APIs such as `solar::rtos` and project-owned `low_level` facade headers. Build configuration selects a concrete low-level implementation such as Teensyduino/FreeRTOS or host simulation.

See [Low Level Boundary](low-level.md) and [RTOS](rtos.md).

## Conventions

Solar's public graph vocabulary uses PascalCase (`System`, `Facilities`, `Services`, `Runtime`). Namespaces are singular and responsibility-focused. Passive facilities live under `solar::facilities`; active services live under `solar::services`.

See [Style And Naming](style-and-naming.md).
