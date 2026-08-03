# System API

The composition API is template-only. The declarations below show its public
shape without exposing normalization internals from generated documentation.

```cpp
template <typename... Entries> struct Compose;
template <typename... Modules> struct Own;
template <typename... Components> struct Components;
template <typename Adapter, typename... Modules> struct Connect;
template <typename GeneratedContract> struct Contract;

template <typename Application, typename Composition>
struct system::System {
    [[nodiscard]] static Result<void> boot() noexcept;
    [[nodiscard]] static Result<void> shutdown() noexcept;
};
```

## Composition entries

```cpp
using Composition = solar::Compose<
    solar::Own<Parameters, Logging, Remote>,
    solar::Components<Cockpit>,
    solar::Connect<ExportMetrics, Metrics, Remote>,
    solar::Contract<generated::Contract>>;
```

Each owned module or component may declare `using Dependencies =
solar::TypeList<...>`. The composer validates ownership, dependency presence,
cycles, adapter signatures, and contract participation at compile time.

See {doc}`../../concepts/system-and-blueprint` for the complete composition
model and {doc}`../../getting-started/first-application` for a working
application.
