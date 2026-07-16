#include "catalog_fixture.hpp"

struct MalformedOwner
{
    using Alphas = int;
};

using BadCatalog = solar::collect_catalog_t<catalog_fixture::alpha::Tag,
                                            solar::DirectDeclarations<catalog_fixture::alpha::Tag>,
                                            solar::TypeList<MalformedOwner>>;

static_assert(BadCatalog::size == 0);
