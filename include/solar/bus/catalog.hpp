#pragma once

#include "solar/bus/subscription.hpp"
#include "solar/component.hpp"

template <> struct solar::catalog_traits<solar::bus::MessageTag>
{
    using Descriptor = solar::bus::Descriptor;
    using DescriptorView = solar::bus::MessageDescriptorView;
    using IdentityDomain = solar::bus::MessageIdentityDomain;

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
                solar::descriptor_traits<solar::bus::MessageTag,
                                         typename Entry::Declaration>::descriptor),
            .owner = Entry::owner_view(),
            .origin = Entry::origin_kind,
        };
    }
};

template <> struct solar::catalog_traits<solar::bus::SubscriptionTag>
{
    using Descriptor = solar::bus::SubscriptionDescriptor;
    using DescriptorView = solar::bus::SubscriptionCatalogView;
    using IdentityDomain = solar::bus::SubscriptionIdentityDomain;
    using Dependencies = solar::TypeList<solar::bus::MessageTag>;

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
                solar::descriptor_traits<solar::bus::SubscriptionTag,
                                         typename Entry::Declaration>::descriptor),
            .owner = Entry::owner_view(),
            .origin = Entry::origin_kind,
        };
    }
};
