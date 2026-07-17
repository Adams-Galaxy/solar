# First Application

This application defines one lifecycle-aware facility, declares a static
System, binds it as the firmware application, and boots it.

## Create The Zephyr Application

Use the CMake setup from {doc}`install` and the required options from
{doc}`requirements`. The complete source is maintained in
`examples/first-application` and compiled in Solar's test matrix.

## Define A Component

A component is an ordinary type. It may provide a descriptor, dependencies,
contributions, and any subset of lifecycle hooks. There is no base class and no
runtime component object.

```{literalinclude} ../../examples/first-application/src/main.cpp
:language: cpp
:start-after: // [component]
:end-before: // [component]
```

Lifecycle hooks return `solar::Result<void, E>` for an `ErrorType`. A missing
hook means that phase has no component-specific work; it is not an error.

## Declare And Bind The System

```{literalinclude} ../../examples/first-application/src/main.cpp
:language: cpp
:start-after: // [system]
:end-before: // [system]
```

The Blueprint is the compile-time application declaration. `Facilities` places
`Platform` in the facility category. `System<Blueprint>` derives the effective
component graph, built-in facilities, catalogs, storage, and lifecycle order.

`SOLAR_BIND_SYSTEM` selects this System for Solar's default global frontend.
Ordinary component headers still include only the Solar and project types they
use; they do not include a composition-root header.

## Boot And Handle Failure

```{literalinclude} ../../examples/first-application/src/main.cpp
:language: cpp
:start-after: // [main]
:end-before: // [main]
```

`solar::boot()` initializes and starts components in dependency order. Success
contains a boot report. Failure contains a typed lifecycle error. This example
classifies the error with `status_of` only at the process-exit boundary.

`solar::stop()` performs stop and deinit in reverse dependency order. Embedded
applications commonly run indefinitely after boot; this native example stops
so the test harness can verify cleanup.

The `printk` call is a Zephyr sample completion signal. It is not Solar's
logging model. Solar does not own an in-firmware text CLI; normal structured
development output is covered by Logging and Remote.

## Build And Run

```sh
west build -b native_sim/native/64 examples/first-application
west build -t run
```

Expected output includes:

```text
Solar first application passed
```

You now have the complete System shape. Continue with
{doc}`../concepts/system-and-blueprint` to understand composition or
{doc}`../tutorials/system-foundations` to add dependencies and execution.
