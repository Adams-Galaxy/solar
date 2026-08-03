# Runtime Ownership

Solar has no instantiated root context. Ordinary objects or application-scoped
static facades own module state. The optional `system::System` type only orders
those modules and adapters.

| Resource | Owner |
| --- | --- |
| Parameter values | `parameters::Store` |
| Event observers and retention | `events::EventBus` |
| Metric samples | `metrics::MetricStore` |
| Encoded log history | `log::StaticLogger` |
| Task slots and service threads | Execution modules |
| Remote links, sessions, requests, and streams | `remote::ByteRuntime` |
| Hardware resources | Zephyr drivers and typed hardware endpoints |

Capacity is a type parameter or Kconfig ceiling. Sibling integration belongs
in a named adapter that depends on both modules.
