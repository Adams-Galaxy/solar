#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "solar/catalog/catalog.hpp"

namespace solar::component
{

struct Tag
{};

struct IdentityDomain
{};

using Id = StableId<IdentityDomain>;
using LocalId = solar::LocalId<Tag>;

struct Descriptor
{
    std::string_view name;
    std::string_view description{};
    std::optional<Id> stable_id{};
    std::uint16_t version{1};
};

using DescriptorView = catalog::BasicDescriptorView<Tag, Descriptor>;

} // namespace solar::component

template <> struct solar::catalog_traits<solar::component::Tag>
{
    using Descriptor = solar::component::Descriptor;
    using DescriptorView = solar::component::DescriptorView;
    using IdentityDomain = solar::component::IdentityDomain;

    template <typename Declaration> static constexpr bool requires_stable_id = false;

    static consteval bool validate(const Descriptor& descriptor)
    {
        return descriptor.version != 0;
    }

    template <typename Entry> static consteval DescriptorView make_view()
    {
        return {
            .local_id = Entry::local_id,
            .descriptor = solar::catalog::descriptor_for_view(
                solar::descriptor_traits<solar::component::Tag,
                                         typename Entry::Declaration>::descriptor),
            .owner = Entry::owner_view(),
            .origin = Entry::origin_kind,
        };
    }
};
