#include "catalog_fixture.hpp"

using BadCatalog = solar::collect_catalog_t<
    catalog_fixture::alpha::Tag,
    solar::DirectDeclarations<catalog_fixture::alpha::Tag, catalog_fixture::AlphaDirect,
                              catalog_fixture::AlphaDirect>,
    solar::TypeList<>>;

static_assert(BadCatalog::size == 2);
