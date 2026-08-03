# First Application

Solar modules are ordinary owned C++ types. They may expose `initialize`,
`start`, `stop`, and `deinitialize`, plus a `TypeList` of dependencies. Small
applications can call a module directly. Larger applications can ask the
optional static composer to order the same modules.

```{literalinclude} ../../examples/first-application/src/main.cpp
:language: cpp
```

`app::System` is a type, not a runtime context object. `System::boot()` walks
the compile-time dependency graph; `System::shutdown()` reverses it. There is
no global binding and no second relaxed mode.

Build and run the maintained example:

```sh
west build -b native_sim/native/64 examples/first-application
west build -t run
```
