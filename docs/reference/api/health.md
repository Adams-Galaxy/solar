# Health API

Include `<solar/health.hpp>` with `CONFIG_SOLAR_HEALTH=y`.

```cpp
template <typename Subject> auto report(Assessment);
template <typename Subject> auto assess();
template <typename Subject, typename Check> auto check();
template <typename Subject> auto progress();
template <typename Subject> auto record();
auto refresh();
auto state();
```

`health::history::read` returns bounded transition pages. No-wait ISR reporting
uses compact observations. See {doc}`../../subsystems/health`.
