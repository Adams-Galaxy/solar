# Add A Log Sink

Define a static sink with a descriptor and bounded `consume` operation:

```cpp
struct TelemetrySink {
    static constexpr solar::log::SinkDescriptor descriptor{.name = "telemetry"};
    static solar::Result<void> consume(solar::log::RecordView,
                                        std::string_view rendered);
};
```

Add it through `log::Configuration<log::Sinks<log::To<TelemetrySink, ...>>>`.
Choose a minimum level and optional panic-safe marker deliberately.

`consume` runs in the Logging drain context. It must bound its own transport
latency and return backpressure or transport failure rather than block forever.
The rendered view is valid only for the call. Copy it into caller-owned bounded
storage if an asynchronous transport needs it later.

## Customize the Zephyr console line

`ZephyrConsole<>` uses `DefaultZephyrConsoleRenderer`, which writes:

```text
[solar 12.345] [drive.controller] NOTICE  message
```

The source is resolved from the system log-source catalog. Platform records
that do not have a Solar source use their origin, such as `zephyr`.

Supply a renderer type to replace the complete console-line presentation:

```cpp
struct CompactConsoleRenderer {
    static solar::Result<void> render(solar::log::RecordView record,
                                      std::string_view message) noexcept
    {
        printk("%s: %.*s\n", solar::log::to_string(record.header.level),
               static_cast<int>(message.size()), message.data());
        return {};
    }
};

using ConsoleSink = solar::log::ZephyrConsole<CompactConsoleRenderer>;
```

A renderer that needs catalog metadata may instead declare
`template <typename System> static render(...)`. Renderer selection and
dispatch are compile-time only: the sink stores no renderer and performs no
virtual call or runtime policy branch.
