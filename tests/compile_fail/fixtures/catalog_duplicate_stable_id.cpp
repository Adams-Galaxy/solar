#include "catalog_fixture.hpp"

struct DuplicateStableId
{
    static constexpr catalog_fixture::alpha::Descriptor descriptor{
        .name = "alpha.other",
        .stable_id = catalog_fixture::alpha_direct_id,
    };
};

using BadCatalog = solar::collect_catalog_t<
    catalog_fixture::alpha::Tag,
    solar::DirectDeclarations<catalog_fixture::alpha::Tag, catalog_fixture::AlphaDirect,
                              DuplicateStableId>,
    solar::TypeList<>>;

static_assert(BadCatalog::size == 2);
