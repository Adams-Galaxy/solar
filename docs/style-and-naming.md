# Style And Naming

Solar is a C++20 static-first runtime. Names should make the type graph easy to read.

## Namespaces

- `solar::core` concepts are generally promoted to `solar::` through umbrella headers.
- `solar::facilities` contains passive runtime capabilities.
- `solar::services` contains active threaded actors and service-adjacent graph utilities.
- `solar::metrics`, `solar::events`, `solar::log`, and `solar::remote` contain subsystem-specific vocabulary.
- `solar::kernel` is the stable Kernel API over Zephyr kernel primitives.

## Type Names

Public type graph APIs use PascalCase:

```cpp
System
Devices
Facilities
Services
Runtime
Name<"imu">
```

Runtime methods use lower snake/camel where appropriate to match existing C++ style:

```cpp
system.Boot();
ctx.Get<Name<"imu">>();
Metrics::observe<ErrorX>(value);
```

## Component Names

Every graph component should expose:

```cpp
using Name = solar::Name<"stable_name">;
```

Prefer dot-separated names for observability catalogs:

```cpp
control.loop.time
remote.rx.frames
supervisor.level
```

Prefer short underscore names for graph components:

```cpp
left_motor
remote
events
```

## Compatibility

Solar is in active development. Prefer clean, direct APIs over compatibility shims for abandoned concepts.
