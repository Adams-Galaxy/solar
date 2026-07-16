#pragma once

#include <concepts>
#include <type_traits>

#include "solar/events/policy.hpp"

namespace solar::events
{

namespace detail
{

template <typename Payload>
struct BorrowedPayload
    : std::bool_constant<std::is_pointer_v<Payload> || std::is_reference_v<Payload>>
{};

template <typename Element, std::size_t Extent>
struct BorrowedPayload<std::span<Element, Extent>> : std::true_type
{};

template <typename Character, typename Traits>
struct BorrowedPayload<std::basic_string_view<Character, Traits>> : std::true_type
{};

template <typename Payload>
inline constexpr bool borrowed_payload_v = BorrowedPayload<Payload>::value;

} // namespace detail

template <typename EventT>
concept Event = EventDeclaration<EventT> &&
                (payload_free_v<EventT> || (!detail::borrowed_payload_v<PayloadOf<EventT>> &&
                                            std::is_trivially_copyable_v<PayloadOf<EventT>> &&
                                            std::is_trivially_destructible_v<PayloadOf<EventT>>));

struct DefaultProcessorTag
{};

struct InfrastructureObserver
{};

template <typename EventT, typename Observer, typename RouteTag = DefaultProcessorTag>
struct Process
{
    static_assert(Event<EventT>,
                  "SOLAR_DIAGNOSTIC_EVENT_PROCESSOR_EVENT: processor requires an event "
                  "declaration");
    using EventType = EventT;
    using ObserverType = Observer;
    using RouteTagType = RouteTag;

    static constexpr ProcessorDescriptor descriptor{
        .name = descriptor_traits<Tag, EventT>::descriptor.name,
    };
};

template <typename... EventsT> using Events = Contribution<Tag, EventsT...>;
template <typename... ProcessorTypes>
using Processors = Contribution<ProcessorTag, ProcessorTypes...>;

} // namespace solar::events
