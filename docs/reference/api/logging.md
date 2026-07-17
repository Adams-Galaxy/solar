# Logging API

Include `<solar/log.hpp>` with `CONFIG_SOLAR_LOG=y`.

```cpp
template <typename Source, typename... Args> auto trace(format_string<Args...>, Args&&...);
template <typename Source, typename... Args> auto debug(format_string<Args...>, Args&&...);
template <typename Source, typename... Args> auto info(format_string<Args...>, Args&&...);
template <typename Source, typename... Args> auto notice(format_string<Args...>, Args&&...);
template <typename Source, typename... Args> auto warn(format_string<Args...>, Args&&...);
template <typename Source, typename... Args> auto error(format_string<Args...>, Args&&...);
template <typename Source, typename... Args> auto critical(format_string<Args...>, Args&&...);
```

No-wait and ISR frontends use `try_*` and `try_*_isr`. Query APIs expose
history, latest record, source/domain/sink records, filtering, and flush.
See {doc}`../../subsystems/logging`.
