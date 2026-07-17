# Composition API

Include `<solar/component.hpp>`, `<solar/catalog.hpp>`, or their aggregate
through `<solar/solar.hpp>`.

```cpp
struct solar::component::Descriptor;
template <typename... Types> struct solar::Dependencies;
template <typename Tag, typename... Types> struct solar::Contribution;
template <typename Tag, typename... Types> struct solar::CatalogSection;
```

`descriptor_traits<Tag, T>` normalizes descriptor access. Catalog entries
retain declaration type, semantic owner, origin, local ID, and stable ID where
defined. The effective `CatalogSet` is immutable after Blueprint normalization.

Application code usually contributes declarations through subsystem-specific
aliases such as `using Metrics`, `using Events`, or `using RemoteData`; it does
not construct catalog entries directly. See
{doc}`../../concepts/identity-contributions-catalogs`.
