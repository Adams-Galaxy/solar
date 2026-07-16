#include "catalog_fixture.hpp"

struct EmptyName
{
    static constexpr catalog_fixture::alpha::Descriptor descriptor{.name = ""};
};

using BadCatalog =
    solar::collect_catalog_t<catalog_fixture::alpha::Tag,
                             solar::DirectDeclarations<catalog_fixture::alpha::Tag, EmptyName>,
                             solar::TypeList<>>;

static_assert(BadCatalog::size == 1);
