# System Foundations

This tutorial extends the first application with dependencies and a contributed
execution registration. Its canonical source is
`examples/system-composition` and is tested in relaxed and strict binding modes.

## Express Dependencies

```{literalinclude} ../../examples/system-composition/src/main.cpp
:language: cpp
:start-after: // [components]
:end-before: // [components]
```

`Sensor` names `Platform` in `Dependencies`. The graph validates that the
dependency exists and is acyclic. Lifecycle initializes `Platform` before
`Sensor` and tears them down in reverse order. Components use project headers
and direct type names; they do not ask Solar to locate dependencies at runtime.

## Contribute Work

```{literalinclude} ../../examples/system-composition/src/main.cpp
:language: cpp
:start-after: // [contribution]
:end-before: // [contribution]
```

`Controller::Tasks` contributes `SampleWork` to the execution catalog. The
registration is a leaf job, not another component. Its target is Zephyr's
system workqueue and its dependency prevents execution from being activated
before `Sensor` is available.

## Compose Once

```{literalinclude} ../../examples/system-composition/src/main.cpp
:language: cpp
:start-after: // [blueprint]
:end-before: // [blueprint]
```

The composition root is the only place that needs the complete application
shape. Solar derives contributed catalogs and inserts enabled built-in
facilities according to Kconfig and demand.

## Submit And Inspect

After boot, the global frontend can submit the contributed registration:

```cpp
const auto submitted = solar::execution::submit<app::SampleWork>();
if (!submitted) {
    const auto classification = solar::status_of(submitted.error());
    // Application-specific response.
}
```

Lifecycle state is queried by type:

```cpp
const auto sensor = solar::lifecycle::record<app::Sensor>();
```

These are bounded queries into canonical static storage; no System object is
passed through the application.

## Compare Binding Modes

Relaxed binding is the default. A frontend can represent disabled,
not-yet-bound, or unregistered operations as runtime errors, which keeps rapid
prototyping compact.

Strict binding is enabled with:

```text
CONFIG_SOLAR_STRICT_CATALOG_BINDING=y
```

Strict mode resolves catalog membership and dispatch at compile time. Calling
an unregistered declaration becomes a compile error. The application source is
otherwise unchanged; the example is built in both modes by Twister.
