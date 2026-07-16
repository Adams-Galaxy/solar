#include "catalog_fixture.hpp"

struct CycleA
{};

struct CycleB
{};

template <> struct solar::catalog_traits<CycleA>
{
    using Descriptor = catalog_fixture::alpha::Descriptor;
    using DescriptorView = catalog_fixture::alpha::DescriptorView;
    using IdentityDomain = catalog_fixture::alpha::IdentityDomain;
    using Dependencies = solar::TypeList<CycleB>;
};

template <> struct solar::catalog_traits<CycleB>
{
    using Descriptor = catalog_fixture::alpha::Descriptor;
    using DescriptorView = catalog_fixture::alpha::DescriptorView;
    using IdentityDomain = catalog_fixture::beta::IdentityDomain;
    using Dependencies = solar::TypeList<CycleA>;
};

using BadCatalogs = solar::CatalogSet<solar::Catalog<CycleA>, solar::Catalog<CycleB>>;

static_assert(BadCatalogs::size == 2);
