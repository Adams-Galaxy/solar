#pragma once

#include "solar/metrics/declaration.hpp"

template <> struct solar::catalog_traits<solar::metrics::Tag>
{
    using Descriptor = solar::metrics::Descriptor;
    using DescriptorView = solar::metrics::DescriptorCatalogView;
    using IdentityDomain = solar::metrics::IdentityDomain;

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
                solar::descriptor_traits<solar::metrics::Tag,
                                         typename Entry::Declaration>::descriptor),
            .owner = Entry::owner_view(),
            .origin = Entry::origin_kind,
        };
    }
};
