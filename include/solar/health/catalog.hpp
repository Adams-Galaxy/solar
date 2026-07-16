#pragma once

#include "solar/health/declaration.hpp"

template <> struct solar::catalog_traits<solar::health::CheckTag>
{
    using Descriptor = solar::health::CheckDescriptor;
    using DescriptorView = solar::health::CheckDescriptorView;
    using IdentityDomain = solar::health::CheckIdentityDomain;

    static constexpr bool unique_names = false;
    template <typename> static constexpr bool requires_stable_id = false;

    static consteval bool validate(const Descriptor& descriptor)
    {
        return descriptor.period_ns >= 0 && descriptor.stale_after_ns >= 0;
    }

    template <typename Entry> static consteval DescriptorView make_view()
    {
        return {{.local_id = Entry::local_id,
                 .descriptor = solar::catalog::descriptor_for_view(
                     solar::descriptor_traits<solar::health::CheckTag,
                                              typename Entry::Declaration>::descriptor),
                 .owner = Entry::owner_view(),
                 .origin = Entry::origin_kind}};
    }
};
