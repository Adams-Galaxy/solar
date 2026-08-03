# First Application

The normal Solar application names its contract, devices, services, platform,
and policy, then boots `solar::System<Application>`. Solar expands that type to
the same statically owned module graph exposed by the advanced composition API.

Modules remain ordinary C++ types with `initialize`, `start`, `stop`, and
`deinitialize`. A conventional device with `init`, `start`, `stop`, and
`deinit` is adapted automatically when placed in `solar::Devices<...>`.

```{literalinclude} ../../examples/first-application/src/main.cpp
:language: cpp
```

`app::System` is a type, not a runtime context object. `System::boot()` walks
the compile-time dependency graph; `System::shutdown()` reverses it. There is
no global binding and no second relaxed mode. Advanced firmware can inspect
`solar::application::Composition<Application>` or write the equivalent
`solar::Compose<...>` explicitly.

Build and run the maintained example:

```sh
west build -b native_sim/native/64 examples/first-application
west build -t run
```
