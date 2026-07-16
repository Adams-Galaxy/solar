#pragma once

#include <optional>

#include "solar/catalog/catalog.hpp"
#include "solar/remote/declaration.hpp"

namespace solar::remote
{

struct SchemaTag
{};
struct DataTag
{};
struct ActionTag
{};
struct TopicTag
{};
struct StreamTag
{};
struct LinkTag
{};

struct SchemaIdentityDomain
{};
struct DataIdentityDomain
{};
struct ActionIdentityDomain
{};
struct TopicIdentityDomain
{};
struct StreamIdentityDomain
{};
struct LinkIdentityDomain
{};

template <typename DescriptorT, typename Id>
[[nodiscard]] consteval auto catalog_descriptor(DescriptorT descriptor)
{
    struct Result
    {
        std::string_view name;
        std::string_view description;
        std::optional<Id> stable_id;
        std::uint16_t version;
    };
    return Result{descriptor.name, descriptor.description, descriptor.id, descriptor.version};
}

template <typename Tag, typename DescriptorT, typename Domain> struct TraitsBase
{
    using Descriptor = DescriptorT;
    using DescriptorView = remote::DescriptorView<Tag, Descriptor>;
    using IdentityDomain = Domain;
    template <typename> static constexpr bool requires_stable_id = true;

    static consteval bool validate(const Descriptor& descriptor)
    {
        return descriptor.id.value != 0 && descriptor.version != 0;
    }

    template <typename Entry> static consteval DescriptorView make_view()
    {
        return {
            .local_id = Entry::local_id,
            .descriptor = catalog::descriptor_for_view(
                descriptor_traits<Tag, typename Entry::Declaration>::descriptor),
            .owner = Entry::owner_view(),
            .origin = Entry::origin_kind,
        };
    }
};

} // namespace solar::remote

template <typename Value>
struct solar::descriptor_traits<solar::remote::SchemaTag, Value,
                                std::void_t<decltype(solar::remote::Schema<Value>::descriptor)>>
{
    static constexpr auto descriptor = solar::remote::Schema<Value>::descriptor;
};

#define SOLAR_DETAIL_REMOTE_CATALOG_TRAITS(TAG, DESCRIPTOR, DOMAIN)                                \
    template <>                                                                                    \
    struct solar::catalog_traits<solar::remote::TAG>                                               \
        : solar::remote::TraitsBase<solar::remote::TAG, solar::remote::DESCRIPTOR,                 \
                                    solar::remote::DOMAIN>                                         \
    {}

SOLAR_DETAIL_REMOTE_CATALOG_TRAITS(SchemaTag, SchemaDescriptor, SchemaIdentityDomain);
SOLAR_DETAIL_REMOTE_CATALOG_TRAITS(DataTag, DataDescriptor, DataIdentityDomain);
SOLAR_DETAIL_REMOTE_CATALOG_TRAITS(ActionTag, ActionDescriptor, ActionIdentityDomain);
SOLAR_DETAIL_REMOTE_CATALOG_TRAITS(TopicTag, TopicDescriptor, TopicIdentityDomain);
SOLAR_DETAIL_REMOTE_CATALOG_TRAITS(StreamTag, StreamDescriptor, StreamIdentityDomain);
SOLAR_DETAIL_REMOTE_CATALOG_TRAITS(LinkTag, LinkDescriptor, LinkIdentityDomain);

#undef SOLAR_DETAIL_REMOTE_CATALOG_TRAITS
