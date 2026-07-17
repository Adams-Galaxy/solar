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
