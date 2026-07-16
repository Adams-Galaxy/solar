#pragma once

#include "solar/events/declaration.hpp"

template <> struct solar::catalog_traits<solar::events::Tag>
{
    using Descriptor = solar::events::Descriptor;
    using DescriptorView = solar::events::DescriptorCatalogView;
    using IdentityDomain = solar::events::IdentityDomain;

    template <typename Declaration> static constexpr bool requires_stable_id = false;

    static consteval bool validate(const Descriptor& descriptor)
    {
        return descriptor.version != 0 && descriptor.domain.id != 0 &&
               !descriptor.domain.name.empty();
    }

    template <typename Entry> static consteval DescriptorView make_view()
    {
        return {
            .local_id = Entry::local_id,
            .descriptor = solar::catalog::descriptor_for_view(
                solar::descriptor_traits<solar::events::Tag,
                                         typename Entry::Declaration>::descriptor),
            .owner = Entry::owner_view(),
            .origin = Entry::origin_kind,
        };
    }
};

template <> struct solar::catalog_traits<solar::events::ProcessorTag>
{
    using Descriptor = solar::events::ProcessorDescriptor;
    using DescriptorView = solar::events::ProcessorCatalogView;
    using IdentityDomain = solar::events::ProcessorIdentityDomain;
    using Dependencies = solar::TypeList<solar::events::Tag>;

    static constexpr bool unique_names = false;

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
                solar::descriptor_traits<solar::events::ProcessorTag,
                                         typename Entry::Declaration>::descriptor),
            .owner = Entry::owner_view(),
            .origin = Entry::origin_kind,
        };
    }
};
