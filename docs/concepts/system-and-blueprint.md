# Optional Static System

`solar::Compose` describes ownership, components, a generated contract, and
named adapters:

```cpp
using Composition = solar::Compose<
    solar::Own<Parameters, Logging, Remote>,
    solar::Components<Cockpit>,
    solar::Connect<ExportMetrics, Metrics, Remote>,
    solar::Contract<generated::Contract>>;

using System = solar::system::System<Application, Composition>;
```

Every owned entry is independently usable and carries a `TypeList` named
`Dependencies`. The composer validates unique ownership, dependency presence,
cycles, adapter signatures, and generated-contract participation at compile
time. It then supplies static `boot()` and `shutdown()` operations.

System has no built-in-module branches, global registration, context object,
or runtime lookup. Dynamic lookup belongs only at dynamic boundaries such as
Remote and host tooling.
