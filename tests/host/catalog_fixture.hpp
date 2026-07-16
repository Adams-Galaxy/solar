#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

#include <solar/catalog.hpp>

namespace catalog_fixture
{

namespace alpha
{

struct Tag
{};

struct IdentityDomain
{};

using Id = solar::StableId<IdentityDomain>;

struct Descriptor
{
    std::string_view name;
    std::string_view description{};
    std::optional<Id> stable_id{};
    std::uint16_t version{1};
};

using DescriptorView = solar::catalog::BasicDescriptorView<Tag, Descriptor>;

template <typename... Types> using Contribute = solar::Contribution<Tag, Types...>;

} // namespace alpha

namespace beta
{

struct Tag
{};

struct IdentityDomain
{};

using Id = solar::StableId<IdentityDomain>;

struct Descriptor
{
    std::string_view name;
    std::optional<Id> stable_id{};
};

using DescriptorView = solar::catalog::BasicDescriptorView<Tag, Descriptor>;

template <typename... Types> using Contribute = solar::Contribution<Tag, Types...>;

} // namespace beta

struct AlphaGroup;
struct AlphaExpandedA;
struct AlphaExpandedB;

} // namespace catalog_fixture

template <> struct solar::catalog_traits<catalog_fixture::alpha::Tag>
{
    using Descriptor = catalog_fixture::alpha::Descriptor;
    using DescriptorView = catalog_fixture::alpha::DescriptorView;
    using IdentityDomain = catalog_fixture::alpha::IdentityDomain;

    template <typename Declaration>
    static constexpr bool requires_stable_id = [] {
        if constexpr (requires { Declaration::externally_visible; }) {
            return Declaration::externally_visible;
        }
        return false;
    }();

    static consteval bool validate(const Descriptor& descriptor)
    {
        return descriptor.version != 0;
    }

    template <typename Declaration>
        requires std::is_same_v<Declaration, catalog_fixture::AlphaGroup>
    using expand =
        solar::TypeList<catalog_fixture::AlphaExpandedA, catalog_fixture::AlphaExpandedB>;

    template <typename Entry> static consteval DescriptorView make_view()
    {
        return {
            .local_id = Entry::local_id,
            .descriptor = solar::catalog::descriptor_for_view(
                solar::descriptor_traits<catalog_fixture::alpha::Tag,
                                         typename Entry::Declaration>::descriptor),
            .owner = Entry::owner_view(),
            .origin = Entry::origin_kind,
        };
    }
};

template <> struct solar::catalog_traits<catalog_fixture::beta::Tag>
{
    using Descriptor = catalog_fixture::beta::Descriptor;
    using DescriptorView = catalog_fixture::beta::DescriptorView;
    using IdentityDomain = catalog_fixture::beta::IdentityDomain;
    using Dependencies = solar::TypeList<catalog_fixture::alpha::Tag>;

    template <typename Declaration> static constexpr bool requires_stable_id = false;

    template <typename Entry> static consteval DescriptorView make_view()
    {
        return {
            .local_id = Entry::local_id,
            .descriptor = solar::catalog::descriptor_for_view(
                solar::descriptor_traits<catalog_fixture::beta::Tag,
                                         typename Entry::Declaration>::descriptor),
            .owner = Entry::owner_view(),
            .origin = Entry::origin_kind,
        };
    }
};

namespace catalog_fixture
{

inline constexpr alpha::Id alpha_direct_id{0x1001};
inline constexpr alpha::Id alpha_external_id{0x1002};

struct AlphaDirect
{
    static constexpr alpha::Descriptor descriptor{
        .name = "alpha.direct",
        .description = "Direct application declaration",
        .stable_id = alpha_direct_id,
    };
};

struct AlphaExternal
{};

struct AlphaOwned
{
    static constexpr bool externally_visible = true;
    static constexpr alpha::Descriptor descriptor{
        .name = "alpha.owned",
        .stable_id = alpha::Id{0x1003},
    };
};

struct AlphaGeneric
{
    static constexpr alpha::Descriptor descriptor{
        .name = "alpha.generic",
        .stable_id = alpha::Id{0x1004},
    };
};

struct AlphaGroup
{};

struct AlphaExpandedA
{
    static constexpr alpha::Descriptor descriptor{
        .name = "alpha.expanded.a",
        .stable_id = alpha::Id{0x1005},
    };
};

struct AlphaExpandedB
{
    static constexpr alpha::Descriptor descriptor{
        .name = "alpha.expanded.b",
        .stable_id = alpha::Id{0x1006},
    };
};

struct BetaOwned
{
    using Source = solar::CatalogReference<alpha::Tag, AlphaOwned>;

    static constexpr beta::Descriptor descriptor{
        .name = "beta.owned",
        .stable_id = beta::Id{0x1001},
    };
};

struct OwnerA
{
    static constexpr solar::component::Descriptor descriptor{.name = "owner_a"};

    using Alphas = alpha::Contribute<AlphaOwned, AlphaGroup>;
    using Betas = beta::Contribute<BetaOwned>;
    using Contributions = alpha::Contribute<AlphaGeneric>;
};

struct OwnerB
{
    static constexpr solar::component::Descriptor descriptor{.name = "owner_b"};

    using Alphas = alpha::Contribute<>;
    using Betas = beta::Contribute<>;
};

} // namespace catalog_fixture

template <>
struct solar::descriptor_traits<catalog_fixture::alpha::Tag, catalog_fixture::AlphaExternal>
{
    static constexpr catalog_fixture::alpha::Descriptor descriptor{
        .name = "alpha.external",
        .description = "Descriptor supplied by trait specialization",
        .stable_id = catalog_fixture::alpha_external_id,
    };
};

template <typename Component>
struct solar::contribution_source<catalog_fixture::alpha::Tag, Component,
                                  std::void_t<typename Component::Alphas>>
{
    using type = typename Component::Alphas;
};

template <typename Component>
struct solar::contribution_source<catalog_fixture::beta::Tag, Component,
                                  std::void_t<typename Component::Betas>>
{
    using type = typename Component::Betas;
};

namespace catalog_fixture
{

using Components = solar::TypeList<OwnerA, OwnerB>;
using CandidateTags = solar::TypeList<solar::component::Tag, alpha::Tag, beta::Tag>;
using Direct =
    solar::DirectCatalogs<solar::DirectDeclarations<solar::component::Tag, OwnerA, OwnerB>,
                          solar::DirectDeclarations<alpha::Tag, AlphaDirect, AlphaExternal>>;

using Catalogs = solar::collect_catalog_set_t<CandidateTags, Components, Direct>;
using ComponentCatalog = Catalogs::Of<solar::component::Tag>;
using AlphaCatalog = Catalogs::Of<alpha::Tag>;
using BetaCatalog = Catalogs::Of<beta::Tag>;

template <typename Entry> struct OwnedByA : std::is_same<typename Entry::Owner, OwnerA>
{};

using AlphaOwnedEntries = solar::filter_catalog_t<AlphaCatalog, OwnedByA>;
using ResolvedBetaSource = solar::resolve_catalog_reference_t<Catalogs, BetaOwned::Source>;

const alpha::DescriptorView* descriptor_data_from_other_translation_unit();

} // namespace catalog_fixture
