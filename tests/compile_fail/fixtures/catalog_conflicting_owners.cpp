#include "catalog_fixture.hpp"

struct FirstOwner
{
    using Alphas = catalog_fixture::alpha::Contribute<catalog_fixture::AlphaOwned>;
};

struct SecondOwner
{
    using Alphas = catalog_fixture::alpha::Contribute<catalog_fixture::AlphaOwned>;
};

using BadCatalog = solar::collect_catalog_t<catalog_fixture::alpha::Tag,
                                            solar::DirectDeclarations<catalog_fixture::alpha::Tag>,
                                            solar::TypeList<FirstOwner, SecondOwner>>;

static_assert(BadCatalog::size == 2);
