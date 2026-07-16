#include "catalog_fixture.hpp"

struct ContributingOwner
{
    using Alphas = catalog_fixture::alpha::Contribute<catalog_fixture::AlphaDirect>;
};

using BadCatalog = solar::collect_catalog_t<
    catalog_fixture::alpha::Tag,
    solar::DirectDeclarations<catalog_fixture::alpha::Tag, catalog_fixture::AlphaDirect>,
    solar::TypeList<ContributingOwner>>;

static_assert(BadCatalog::size == 2);
