# Composition API

Include `<solar/component.hpp>`, `<solar/catalog.hpp>`, or their aggregate
through `<solar/solar.hpp>`.

```cpp
struct solar::component::Descriptor;
template <typename... Types> struct solar::TypeList;
template <typename Tag, typename... Types> struct solar::Contribution;
template <typename... Entries> struct solar::Compose;
template <typename... Types> struct solar::Own;
template <typename... Types> struct solar::Components;
template <typename Adapter, typename... Types> struct solar::Connect;
```

`descriptor_traits<Tag, T>` normalizes descriptor access. Catalog entries
retain declaration type, semantic owner, origin, local ID, and stable ID where
defined. Generic catalogs remain useful inside modules without being owned by
the System composer.

Application code uses generated role aliases and
`solar::Contributes<Component, Role>` to claim behavior for declarations that
already exist in the generated contract.
