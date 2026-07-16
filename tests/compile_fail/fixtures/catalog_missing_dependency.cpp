#include "catalog_fixture.hpp"

struct DependentTag
{};

struct MissingTag
{};

template <> struct solar::catalog_traits<DependentTag>
{
    using Descriptor = catalog_fixture::alpha::Descriptor;
    using DescriptorView = catalog_fixture::alpha::DescriptorView;
    using IdentityDomain = catalog_fixture::alpha::IdentityDomain;
    using Dependencies = solar::TypeList<MissingTag>;
};

using BadCatalogs = solar::CatalogSet<solar::Catalog<DependentTag>>;

static_assert(BadCatalogs::size == 1);
