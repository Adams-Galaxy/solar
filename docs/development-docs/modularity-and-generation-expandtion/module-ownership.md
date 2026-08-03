# Module ownership convention

Solar modules have one ordinary, explicitly owned implementation. Small
programs instantiate that object directly. Applications wanting canonical
global access use a typed static facade whose identity includes the
`Application` type; System owns and orchestrates those same facades.

```cpp
solar::parameters::Store<MySchema> local;

struct Application;
using parameters = solar::parameters::StaticStore<Application, MySchema>;
using system = solar::system::System<
    Application,
    solar::Compose<solar::Own<parameters>>>;
```

Use an object when a test or program needs multiple independent instances. Use
a static facade for the one canonical application instance. Use System only
when dependency ordering, rollback, and coordinated lifecycle add value.
Operations never receive a context object, and modules do not inherit from a
runtime base class.

Capabilities and inspection are optional typed providers. Capacity is always a
template or structural configuration value; exhaustion returns a stable
`Status` rather than allocating. Connections between modules live in explicit
adapters and `Connect<...>` declarations, not inside either sibling.

Current production examples are `parameters::Store`, `remote::Server`,
`events::EventBus`, `metrics::MetricStore`, `log::RecordStore`,
`persistence::MemoryStorage`, `execution::TaskQueue`, and
`supervisor::Monitor`.
