# Supervisor API

Include `<solar/supervisor.hpp>` with `CONFIG_SOLAR_SUPERVISOR=y`.

```cpp
template <typename... Rules> struct Policy;
template <typename... Policies> using Configuration = /* Blueprint section */;
template <typename Provider> struct Watchdog;

auto state();
auto watchdog();
template <typename Subject> auto record();
auto responses(ResponseCursor, std::span<ResponseRecord>);
void wake();
```

Rules select `OnFault`, `OnDegraded`, `OnStall`, or `OnRecoveryFailure` and a
bounded sequence of response action types. See
{doc}`../../subsystems/supervisor`.
