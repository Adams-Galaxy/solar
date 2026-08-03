# Supervisor API

Include `<solar/supervisor.hpp>`.

```cpp
enum class Condition { Unknown, Healthy, Degraded, Fault };
template <typename... Checks> struct Schema;
template <typename Schema> class Monitor;
template <typename Application, typename Schema> struct StaticMonitor;
```

`Monitor` owns typed condition slots. Recovery policy and application restart
behavior remain explicit adapters or services. See {doc}`../../subsystems/supervisor`.
