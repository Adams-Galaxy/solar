#include "catalog_fixture.hpp"

struct DuplicateName
{
    static constexpr catalog_fixture::alpha::Descriptor descriptor{
        .name = "alpha.direct",
        .stable_id = catalog_fixture::alpha::Id{0x2001},
    };
};

using BadCatalog =
    solar::collect_catalog_t<catalog_fixture::alpha::Tag,
                             solar::DirectDeclarations<catalog_fixture::alpha::Tag,
                                                       catalog_fixture::AlphaDirect, DuplicateName>,
                             solar::TypeList<>>;

static_assert(BadCatalog::size == 2);
