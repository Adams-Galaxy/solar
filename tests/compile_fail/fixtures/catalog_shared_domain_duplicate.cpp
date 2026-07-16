#include "catalog_fixture.hpp"

struct SharedTag
{};

struct SharedEntry
{
    static constexpr catalog_fixture::alpha::Descriptor descriptor{
        .name = "shared.entry",
        .stable_id = catalog_fixture::alpha_direct_id,
    };
};

template <> struct solar::catalog_traits<SharedTag>
{
    using Descriptor = catalog_fixture::alpha::Descriptor;
    using DescriptorView = solar::catalog::BasicDescriptorView<SharedTag, Descriptor>;
    using IdentityDomain = catalog_fixture::alpha::IdentityDomain;

    template <typename Entry> static consteval DescriptorView make_view()
    {
        return {
            .local_id = Entry::local_id,
            .descriptor =
                solar::descriptor_traits<SharedTag, typename Entry::Declaration>::descriptor,
            .owner = Entry::owner_view(),
            .origin = Entry::origin_kind,
        };
    }
};

using SharedCatalog =
    solar::collect_catalog_t<SharedTag, solar::DirectDeclarations<SharedTag, SharedEntry>,
                             solar::TypeList<>>;
using BadCatalogs = solar::CatalogSet<catalog_fixture::AlphaCatalog, SharedCatalog>;

static_assert(BadCatalogs::size == 2);
