#include "catalog_fixture.hpp"

struct WrongDescriptor
{
    static constexpr catalog_fixture::beta::Descriptor descriptor{.name = "wrong.kind"};
};

using BadCatalog = solar::collect_catalog_t<
    catalog_fixture::alpha::Tag,
    solar::DirectDeclarations<catalog_fixture::alpha::Tag, WrongDescriptor>, solar::TypeList<>>;

static_assert(BadCatalog::size == 1);
