#include "catalog_fixture.hpp"

struct DuplicateSourceOwner
{
    using Alphas = catalog_fixture::alpha::Contribute<catalog_fixture::AlphaDirect>;
    using Contributions = catalog_fixture::alpha::Contribute<catalog_fixture::AlphaDirect>;
};

using BadCatalog = solar::collect_catalog_t<catalog_fixture::alpha::Tag,
                                            solar::DirectDeclarations<catalog_fixture::alpha::Tag>,
                                            solar::TypeList<DuplicateSourceOwner>>;

static_assert(BadCatalog::size == 2);
