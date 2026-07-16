#pragma once

#include <concepts>
#include <type_traits>

#include "solar/bus/contribution.hpp"
#include "solar/bus/delivery.hpp"

namespace solar::bus
{

struct DefaultRouteTag
{};

template <typename Declaration>
concept Message = requires {
    { descriptor_traits<MessageTag, Declaration>::descriptor } -> std::same_as<const Descriptor&>;
};

template <typename MessageT, typename Delivery> struct On
{
    using MessageType = MessageT;
    using DeliveryType = Delivery;
};

template <typename MessageT, typename SubscriberT, typename Delivery,
          typename RouteTag = DefaultRouteTag>
struct To
{
    using MessageType = MessageT;
    using Subscriber = SubscriberT;
    using DeliveryType = Delivery;
    using Tag = RouteTag;
};

template <typename RouteTag, typename MessageT, typename HandlerT, typename Delivery> struct Route
{
    using MessageType = MessageT;
    using Handler = HandlerT;
    using DeliveryType = Delivery;
    using Tag = RouteTag;
};

template <typename Subscriber, typename Declaration> struct BoundSubscription
{
    using SubscriberType = Subscriber;
    using DeclarationType = Declaration;
};

template <typename Subscription> struct subscription_traits
{
    static constexpr bool valid = false;
};

template <typename Subscriber, typename MessageT, typename Delivery>
struct subscription_traits<BoundSubscription<Subscriber, On<MessageT, Delivery>>>
{
    static constexpr bool valid = true;
    using MessageType = MessageT;
    using SubscriberType = Subscriber;
    using HandlerType = Subscriber;
    using RouteTag = DefaultRouteTag;
    using DeliveryType = Delivery;
};

template <typename Subscriber, typename RouteTagT, typename MessageT, typename Handler,
          typename Delivery>
struct subscription_traits<
    BoundSubscription<Subscriber, Route<RouteTagT, MessageT, Handler, Delivery>>>
{
    static constexpr bool valid = true;
    using MessageType = MessageT;
    using SubscriberType = Subscriber;
    using HandlerType = Handler;
    using RouteTag = RouteTagT;
    using DeliveryType = Delivery;
};

template <typename MessageT, typename Subscriber, typename Delivery, typename RouteTagT>
struct subscription_traits<To<MessageT, Subscriber, Delivery, RouteTagT>>
{
    static constexpr bool valid = true;
    using MessageType = MessageT;
    using SubscriberType = Subscriber;
    using HandlerType = Subscriber;
    using RouteTag = RouteTagT;
    using DeliveryType = Delivery;
};

template <typename DeclarationT>
concept Subscription = subscription_traits<DeclarationT>::valid;

template <typename Subscription>
inline constexpr SubscriptionDescriptor subscription_descriptor{
    .name =
        descriptor_traits<MessageTag,
                          typename subscription_traits<Subscription>::MessageType>::descriptor.name,
};

} // namespace solar::bus

template <typename Component, typename Declaration>
struct solar::contributed_declaration<solar::bus::SubscriptionTag, Component, Declaration>
{
    using type = solar::bus::BoundSubscription<Component, Declaration>;
};

template <solar::bus::Subscription Subscription>
struct solar::descriptor_traits<solar::bus::SubscriptionTag, Subscription>
{
    static constexpr auto descriptor = solar::bus::subscription_descriptor<Subscription>;
};
