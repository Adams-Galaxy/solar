#include "catalog_fixture.hpp"

struct MissingDescriptor
{};

using BadCatalog = solar::collect_catalog_t<
    catalog_fixture::alpha::Tag,
    solar::DirectDeclarations<catalog_fixture::alpha::Tag, MissingDescriptor>, solar::TypeList<>>;

static_assert(BadCatalog::size == 1);
