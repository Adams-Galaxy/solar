#include "catalog_fixture.hpp"

struct MissingStableId
{
    static constexpr bool externally_visible = true;
    static constexpr catalog_fixture::alpha::Descriptor descriptor{
        .name = "alpha.external_without_id",
    };
};

using BadCatalog = solar::collect_catalog_t<
    catalog_fixture::alpha::Tag,
    solar::DirectDeclarations<catalog_fixture::alpha::Tag, MissingStableId>, solar::TypeList<>>;

static_assert(BadCatalog::size == 1);
