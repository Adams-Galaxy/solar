# System API

The composition API is template-only. The declarations below show its public
shape without exposing normalization internals from generated documentation.

```cpp
template <typename... Sections>
struct Blueprint;

template <typename BlueprintT>
struct System {
    using Blueprint = BlueprintT;
    using Effective = effective_blueprint_t<BlueprintT>;
    using Components = typename Effective::Components;
    using Graph = typename Effective::Graph;
    using Catalogs = typename Effective::Catalogs;

    template <typename Application = DefaultApplication>
    [[nodiscard]] static auto boot() noexcept;

    [[nodiscard]] static auto stop() noexcept;
};
```

## Component sections

```cpp
template <typename... Types> struct Devices;
template <typename... Types> struct Facilities;
template <typename... Types> struct Services;
template <typename... Types> struct Executors;
template <typename... Types> struct Execution;
template <typename... Types> struct Dependencies;
```

`Devices`, `Facilities`, `Services`, and `Executors` place components in the
System graph. `Execution` registers jobs with executors. A component's
optional `Dependencies` alias controls graph and lifecycle ordering.

See {doc}`../../concepts/system-and-blueprint` for the complete composition
model and {doc}`../../tutorials/system-foundations` for a working application.
