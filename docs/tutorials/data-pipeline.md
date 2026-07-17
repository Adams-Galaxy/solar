# Build A Typed Data Pipeline

This tutorial follows the runnable `examples/data-pipeline` application. One
Bus command drives four observable effects without a runtime registry or
unbounded allocation.

## Declare the data

The application declares one message, Parameter, Metric, and Event:

```{literalinclude} ../../examples/data-pipeline/src/main.cpp
:language: cpp
:start-after: // [declarations]
:end-before: // [declarations]
```

The declarations define identity and storage behavior. They do not construct
runtime objects.

## Contribute from the owner

`Controller` owns the application meaning and contributes each declaration
through a conventional alias:

```{literalinclude} ../../examples/data-pipeline/src/main.cpp
:language: cpp
:start-after: // [component]
:end-before: // [component]
```

The inline subscription runs in the emitter context. Its handler commits the
validated Parameter, increments a Counter, observes an Event, and captures a
Notice log. Each boundary checks its typed `Result`.

## Compose and run

The Blueprint includes the component. Logging configuration adds bounded
retained history:

```{literalinclude} ../../examples/data-pipeline/src/main.cpp
:language: cpp
:start-after: // [system]
:end-before: // [system]
```

After boot, the global subsystem frontends resolve through the bound System:

```{literalinclude} ../../examples/data-pipeline/src/main.cpp
:language: cpp
:start-after: // [flow]
:end-before: // [flow]
```

Build and run it:

```sh
west twister -T examples/data-pipeline -p native_sim/native/64 --inline-logs
```

Next, change the Bus route to `Queued<SystemWorkQueue, 4>`. The handler then
runs asynchronously, so the application must wait on an explicit completion
condition before asserting downstream state. This is the practical difference
between transport policy and data declaration.
