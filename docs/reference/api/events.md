# Events API

Include `<solar/events.hpp>` with `CONFIG_SOLAR_EVENTS=y`.

```cpp
template <typename... T> struct Events;
template <typename... T> struct Processors;

template <typename Event, typename... Args> auto observe(Args&&... payload);
template <typename Event, typename... Args> auto try_observe(Args&&... payload);
template <typename Event, typename... Args> auto try_observe_isr(Args&&... payload);
```

Capture policies are under `events::capture`, retention under
`events::retention`, and integration adapters under `events::metrics` and
`events::log`. See {doc}`../../subsystems/events`.
