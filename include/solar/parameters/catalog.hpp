#pragma once

#include "solar/parameters/change.hpp"
#include "solar/parameters/types.hpp"

template <> struct solar::catalog_traits<solar::parameters::Tag>
{
    using Descriptor = solar::parameters::Descriptor;
    using DescriptorView = solar::parameters::DescriptorCatalogView;
    using IdentityDomain = solar::parameters::IdentityDomain;

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
                solar::descriptor_traits<solar::parameters::Tag,
                                         typename Entry::Declaration>::descriptor),
            .owner = Entry::owner_view(),
            .origin = Entry::origin_kind,
        };
    }
};

template <> struct solar::catalog_traits<solar::parameters::ChangeTag>
{
    using Descriptor = solar::parameters::ChangeDescriptor;
    using DescriptorView = solar::parameters::ChangeCatalogView;
    using IdentityDomain = solar::parameters::ChangeIdentityDomain;
    using Dependencies = solar::TypeList<solar::parameters::Tag>;

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
                solar::descriptor_traits<solar::parameters::ChangeTag,
                                         typename Entry::Declaration>::descriptor),
            .owner = Entry::owner_view(),
            .origin = Entry::origin_kind,
        };
    }
};
