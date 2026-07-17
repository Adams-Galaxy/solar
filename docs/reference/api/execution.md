# Execution API

Include `<solar/execution.hpp>` with `CONFIG_SOLAR_EXECUTION=y`.

```cpp
template <fixed_string Name, typename Behavior, typename Target, typename... Policies>
struct OnDemand;
template <fixed_string Name, typename Behavior, typename Target, typename... Policies>
struct Delayable;
template <fixed_string Name, typename Behavior, auto Period, typename Target,
          typename... Policies>
struct Periodic;
template <fixed_string Name, typename Behavior, typename PollSet, typename Target,
          typename... Policies>
struct PollTriggered;

template <typename Registration> auto submit();
template <typename Registration> auto try_submit_isr();
template <typename Registration> auto cancel();
template <typename Registration> auto cancel_sync();
template <typename Registration> auto flush();
```

Focused queries expose registration, service, executor, and target records
without a universal runtime snapshot. See {doc}`../../subsystems/execution`.
