# Metrics

Metrics hold bounded numerical instrumentation for current state, counts,
distributions, and durations. They are operational measurements, not durable
events or application message transport.

```cpp
struct Frames {
    using Value = std::uint64_t;
    using Instrument = solar::metrics::Counter;
    using Unit = solar::metrics::units::Frames;
    static constexpr solar::metrics::Descriptor descriptor{.name = "frames"};
};

SOLAR_TRY(solar::metrics::inc<Frames>());
```

## Instruments

- Counters expose increment and add operations.
- Gauges store the latest value.
- Distributions apply a fixed reducer such as Summary, WindowMean, EMA, or
  Histogram.
- Timers record bounded durations and may provide scoped timing helpers.

`get<T>()` returns an instrument snapshot; `get_view<T, View>()` projects a
specific reducer view. Units are declaration metadata and do not silently
convert values.

## Concurrency and overflow

Declarations choose atomic, spin-locked, or mutex-protected concurrency based
on value and reducer needs. ISR operations exist only for compatible storage.
Arithmetic overflow is explicit: saturate, reject, or wrap. Optional runtime
reset is compiled only when enabled and selected by the declaration.

Event-to-Metric adapters perform increments, additions, gauge sets,
observations, and duration recording from typed event payload fields. Storage
remains owned once by the Metrics Facility.

See {doc}`../reference/api/metrics` and {doc}`../tutorials/data-pipeline`.
