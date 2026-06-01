# Metrics

Metrics are typed observable state. They do not have sinks and they are not emitted record streams. Other services, such as Remote or a future supervisor/exporter, pull metric snapshots when needed.

## Descriptors

```cpp
struct Meters { using Name = solar::Name<"m">; };
struct Volts { using Name = solar::Name<"V">; };
struct Microseconds { using Name = solar::Name<"us">; };

using LoopCount = solar::metrics::Counter<solar::Name<"control.loop.count">>;
using BatteryVoltage = solar::metrics::Gauge<solar::Name<"battery.voltage">, float, Volts>;
using ErrorMean = solar::metrics::Sample<
    solar::Name<"control.error.x.mean">,
    float,
    Meters,
    solar::metrics::WindowMean<10>>;
using LoopTime = solar::metrics::Timer<
    solar::Name<"control.loop.time">,
    solar::metrics::WindowMean<10>,
    Microseconds>;
```

Units are user-defined type tags. Solar does not convert units.

## Policies

Sample and timer descriptors use policies to define what the primary value means.

Built-in policies:

- `Last`: latest observed value.
- `Max`: largest observed value.
- `WindowMean<N>`: mean over the last `N` samples.
- `Ema<N, D>`: exponential moving average with alpha `N / D`.

Custom policies expose `Storage<ValueT>` with:

```cpp
void reset();
void observe(ValueT value);
auto value() const;
```

## Facility API

`solar::facilities::Metrics` aliases `solar::metrics::Facility<>`.

```cpp
Metrics::inc<LoopCount>();
Metrics::set<BatteryVoltage>(12.1f);
Metrics::observe<ErrorMean>(error_x);

{
    auto scope = Metrics::scoped<LoopTime>();
    update_control();
}
```

`get<Metric>()` returns the metric's policy output. `snapshot<Metric>()` returns a uniform descriptor/value packet with one primary value.

## Groups

When one domain observation updates several descriptors, define an explicit group facade:

```cpp
struct ControlXError
{
    using Raw = solar::metrics::Gauge<solar::Name<"control.error.x">, float, Meters>;
    using Mean = solar::metrics::Sample<solar::Name<"control.error.x.mean">, float, Meters, solar::metrics::WindowMean<10>>;
    using Max = solar::metrics::Sample<solar::Name<"control.error.x.max">, float, Meters, solar::metrics::Max>;
    using Metrics = solar::metrics::List<Raw, Mean, Max>;

    template <typename Store>
    static void observe(float value)
    {
        Store::template set<Raw>(value);
        Store::template observe<Mean>(value);
        Store::template observe<Max>(value);
    }
};
```

Groups make multi-metric updates explicit and domain-readable.
