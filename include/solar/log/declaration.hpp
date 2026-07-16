#pragma once

#include <optional>
#include <string_view>
#include <type_traits>

#include "solar/log/types.hpp"

namespace solar::log
{

struct SourceTag
{};

struct DomainTag
{};

struct SourceIdentityDomain
{};

struct DomainIdentityDomain
{};

struct SourceDescriptor
{
    std::string_view name;
    std::string_view description{};
    std::optional<StableSourceId> stable_id{};
    std::uint16_t version{1};
};

struct DomainDescriptor
{
    std::string_view name;
    std::string_view description{};
    std::optional<StableDomainId> stable_id{};
    std::uint16_t version{1};
};

struct SinkDescriptor
{
    std::string_view name;
    std::string_view description{};
};

using SourceDescriptorView = catalog::BasicDescriptorView<SourceTag, SourceDescriptor>;
using DomainDescriptorView = catalog::BasicDescriptorView<DomainTag, DomainDescriptor>;

template <typename T>
concept Source = requires {
    { descriptor_traits<SourceTag, T>::descriptor } -> std::convertible_to<SourceDescriptor>;
};

template <typename T>
concept Domain = requires {
    { descriptor_traits<DomainTag, T>::descriptor } -> std::convertible_to<DomainDescriptor>;
};

namespace domain
{

#define SOLAR_DETAIL_LOG_DOMAIN(TYPE, NAME, DESCRIPTION)                                           \
    struct TYPE                                                                                   \
    {                                                                                             \
        static constexpr DomainDescriptor descriptor{.name = NAME, .description = DESCRIPTION};   \
    }

SOLAR_DETAIL_LOG_DOMAIN(Unclassified, "unclassified", "No explicit diagnostic domain");
SOLAR_DETAIL_LOG_DOMAIN(Lifecycle, "lifecycle", "System and component lifecycle");
SOLAR_DETAIL_LOG_DOMAIN(Transport, "transport", "Physical and logical transports");
SOLAR_DETAIL_LOG_DOMAIN(Communication, "communication", "Application communication");
SOLAR_DETAIL_LOG_DOMAIN(Storage, "storage", "Persistence and storage");
SOLAR_DETAIL_LOG_DOMAIN(Control, "control", "Control behavior");
SOLAR_DETAIL_LOG_DOMAIN(Device, "device", "Hardware-facing device behavior");
SOLAR_DETAIL_LOG_DOMAIN(Scheduling, "scheduling", "Execution and scheduling");
SOLAR_DETAIL_LOG_DOMAIN(Security, "security", "Security-relevant behavior");
SOLAR_DETAIL_LOG_DOMAIN(Resources, "resources", "Bounded resource use");

#undef SOLAR_DETAIL_LOG_DOMAIN

} // namespace domain

using BuiltinDomains = TypeList<domain::Unclassified, domain::Lifecycle, domain::Transport,
                                domain::Communication, domain::Storage, domain::Control,
                                domain::Device, domain::Scheduling, domain::Security,
                                domain::Resources>;

} // namespace solar::log

template <> struct solar::catalog_traits<solar::log::SourceTag>
{
    using Descriptor = solar::log::SourceDescriptor;
    using DescriptorView = solar::log::SourceDescriptorView;
    using IdentityDomain = solar::log::SourceIdentityDomain;

    template <typename> static constexpr bool requires_stable_id = false;

    static consteval bool validate(const Descriptor& descriptor)
    {
        return descriptor.version != 0;
    }

    template <typename Entry> static consteval DescriptorView make_view()
    {
        return {
            .local_id = Entry::local_id,
            .descriptor = solar::catalog::descriptor_for_view(
                solar::descriptor_traits<solar::log::SourceTag,
                                         typename Entry::Declaration>::descriptor),
            .owner = Entry::owner_view(),
            .origin = Entry::origin_kind,
        };
    }
};

template <> struct solar::catalog_traits<solar::log::DomainTag>
{
    using Descriptor = solar::log::DomainDescriptor;
    using DescriptorView = solar::log::DomainDescriptorView;
    using IdentityDomain = solar::log::DomainIdentityDomain;

    template <typename> static constexpr bool requires_stable_id = false;

    static consteval bool validate(const Descriptor& descriptor)
    {
        return descriptor.version != 0;
    }

    template <typename Entry> static consteval DescriptorView make_view()
    {
        return {
            .local_id = Entry::local_id,
            .descriptor = solar::catalog::descriptor_for_view(
                solar::descriptor_traits<solar::log::DomainTag,
                                         typename Entry::Declaration>::descriptor),
            .owner = Entry::owner_view(),
            .origin = Entry::origin_kind,
        };
    }
};

template <typename Declaration>
struct solar::descriptor_override<
    solar::log::SourceTag, Declaration,
    std::enable_if_t<std::is_same_v<
        std::remove_cvref_t<decltype(Declaration::descriptor)>, solar::component::Descriptor>>>
{
    static constexpr solar::log::SourceDescriptor descriptor{
        .name = Declaration::descriptor.name,
        .description = Declaration::descriptor.description,
        .stable_id = Declaration::descriptor.stable_id
                         ? std::optional<solar::log::StableSourceId>{solar::log::StableSourceId{
                               static_cast<std::uint32_t>(
                                   Declaration::descriptor.stable_id->raw())}}
                         : std::nullopt,
        .version = Declaration::descriptor.version,
    };
};
