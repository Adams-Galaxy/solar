# Metrics API

Include `<solar/metrics.hpp>` with `CONFIG_SOLAR_METRICS=y`.

```cpp
template <typename... T> struct Metrics;
template <typename Metric> auto inc();
template <typename Metric> auto add(typename Metric::Value amount);
template <typename Metric> auto set(typename Metric::Value value);
template <typename Metric> auto observe(typename Metric::Value value);
template <typename Metric> auto get();
template <typename Metric, typename View> auto get_view();
```

Instrument, reducer, unit, concurrency, overflow, and reset policies are
compile-time declaration traits. See {doc}`../../subsystems/metrics`.
