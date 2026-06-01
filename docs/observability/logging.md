# Logging

Logging is a static direct-fanout facility under `solar::log`. A logger is usually selected in the system runtime:

```cpp
using Logger = solar::log::Logger<
    solar::Name<"log">,
    solar::log::Sinks<UsbSink, MemorySink>,
    solar::log::Policy::Direct>;

using System = solar::System<
    Board,
    Peripherals,
    Devices,
    Facilities,
    Services,
    Tasks,
    Channels,
    solar::Runtime<solar::Logging<Logger>>>;
```

## Sources And Categories

Sources and categories are type-level names:

```cpp
using ControlLog = Logger::Log<
    solar::Name<"control">,
    solar::log::Categories<solar::Name<"loop">, solar::Name<"fault">>>;
```

Call sites are printf-style:

```cpp
ControlLog::info<solar::Name<"loop">>("tick=%u volts=%.2f", tick, volts);
ControlLog::error_id<solar::Name<"fault">>(SOLAR_LOG_ID(), "fault=%u", code);
```

Fire-and-forget calls return `void`. `try_*` variants return `solar::Status` when a caller cares about sink failure.

## Sinks

A sink is:

```cpp
solar::log::Sink<Name, Writer, Format, Filter>
```

Writers handle bytes. Formats produce bytes from `log::Record`. Filters decide whether the sink receives a record. This gives each sink independent formatting and level policy without a registry or worker thread.

## Null Logger

`solar::log::NullLogger` is the default. It accepts the same static API and emits nothing, letting Solar internals log without requiring every project to configure sinks.
