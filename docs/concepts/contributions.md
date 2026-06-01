# Contributions

Contributions let components declare the observability and Remote vocabulary they own without central registries.

Any board, peripheral, device, facility, service, task, or channel type can expose optional aliases:

```cpp
using Metrics = solar::metrics::List<...>;
using Events = solar::events::List<...>;
using RemoteTypes = solar::remote::Types<...>;
using RemoteMethods = solar::remote::Methods<...>;
using RemoteTopics = solar::remote::Topics<...>;
using RemoteObservables = solar::remote::Observables<...>;
```

These aliases mean "this component owns/provides this vocabulary." They do not mean "this component consumes this vocabulary."

## System Catalogs

`solar::System` collects contributions from the board and every graph group:

```cpp
using MetricsCatalog = ...;
using EventsCatalog = ...;
using RemoteTypesCatalog = ...;
using RemoteMethodsCatalog = ...;
using RemoteTopicsCatalog = ...;
using RemoteObservablesCatalog = ...;
```

The collected catalogs are available to facilities and services through `ContextT::SystemType`.

## Validation

Solar validates these catalogs at compile time:

- metric names must be unique;
- event names must be unique;
- Remote type IDs must be unique;
- Remote method/topic/observable IDs must be unique.

This keeps service-owned metrics and events ergonomic. A future supervisor can contribute `supervisor.level`, `supervisor.check.fail`, and `supervisor.degraded` from its own type, and the app does not have to re-list those entries manually.

## Design Rule

Prefer contributing vocabulary from the component that owns the behavior. Avoid declaring another component's metric/event in a consumer just because the consumer reads it.
