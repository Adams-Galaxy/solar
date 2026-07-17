# Inspection API

Include `<solar/inspection.hpp>` with `CONFIG_SOLAR_INSPECTION=y`.

```cpp
auto collections();
template <typename Collection> auto descriptor();
template <typename Collection> auto query(
    const typename Collection::Query&, std::span<typename Collection::Record>);
```

Built-in collection tags cover graph, lifecycle, execution, Bus, Parameters,
Events, Metrics, Logging, Remote, Health, and Supervisor where enabled.
Applications can contribute additional bounded providers. See
{doc}`../../subsystems/inspection`.
