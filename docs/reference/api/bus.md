# Bus API

Include `<solar/bus.hpp>` with `CONFIG_SOLAR_BUS=y`.

```cpp
template <typename... T> struct Messages;
template <typename... T> struct Subscriptions;
template <typename Message, typename Delivery> struct On;

template <typename Message> auto emit(const Message&);
template <typename Message> auto try_emit(const Message&);
template <typename Message> auto try_emit_isr(const Message&);
```

Delivery policies live in `bus::delivery`; overflow policies live in
`bus::overflow`. Focused records cover messages, routes, and the Facility.
See {doc}`../../subsystems/bus`.
