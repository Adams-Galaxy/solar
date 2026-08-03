# Execution API

Include `<solar/execution.hpp>`.

```cpp
template <std::size_t Capacity> class TaskQueue;
template <typename Application, typename Service, std::size_t StackBytes,
          typename PriorityPolicy, typename Dependencies = TypeList<>>
class ServiceRunner;
```

`TaskQueue` is bounded and explicitly pumped. `ServiceRunner` owns the Zephyr
thread and stop/join lifecycle for one static service. See
{doc}`../../subsystems/execution`.
