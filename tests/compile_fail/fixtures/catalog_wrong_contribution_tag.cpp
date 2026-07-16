#include "catalog_fixture.hpp"

struct WrongTagOwner
{
    using Alphas = catalog_fixture::beta::Contribute<catalog_fixture::BetaOwned>;
};

using BadCatalog = solar::collect_catalog_t<catalog_fixture::alpha::Tag,
                                            solar::DirectDeclarations<catalog_fixture::alpha::Tag>,
                                            solar::TypeList<WrongTagOwner>>;

static_assert(BadCatalog::size == 0);
