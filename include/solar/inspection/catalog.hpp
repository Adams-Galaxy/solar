#pragma once

#include "solar/inspection/declaration.hpp"

template <> struct solar::catalog_traits<solar::inspection::Tag>
{
    using Descriptor = solar::inspection::Descriptor;
    using DescriptorView = solar::inspection::DescriptorView;
    using IdentityDomain = solar::inspection::IdentityDomain;

    template <typename Declaration> static constexpr bool requires_stable_id = true;

    template <typename Declaration> static consteval bool validate_declaration()
    {
        static_assert(solar::inspection::CollectionType<Declaration>,
                      "SOLAR_DIAGNOSTIC_INVALID_INSPECTION_COLLECTION: collection requires "
                      "bounded Record and Query types plus an Inspection descriptor");
        return true;
    }

    static consteval bool validate(const Descriptor& descriptor)
    {
        return descriptor.stable_id.value != 0 && descriptor.version != 0 &&
               descriptor.maximum_page != 0 && descriptor.record_size != 0 &&
               descriptor.query_size != 0;
    }

    template <typename Entry> static consteval DescriptorView make_view()
    {
        return {
            .local_id = Entry::local_id,
            .descriptor = solar::catalog::descriptor_for_view(
                solar::descriptor_traits<solar::inspection::Tag,
                                         typename Entry::Declaration>::descriptor),
            .owner = Entry::owner_view(),
            .origin = Entry::origin_kind,
        };
    }
};
