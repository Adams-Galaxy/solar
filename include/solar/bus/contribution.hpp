#pragma once

#include <type_traits>

#include "solar/bus/types.hpp"
#include "solar/catalog/contribution.hpp"

namespace solar::bus
{
template <typename... Types> using Messages = Contribution<MessageTag, Types...>;
template <typename... Types> using Subscriptions = Contribution<SubscriptionTag, Types...>;
template <typename... Types> using ContributeMessages = Messages<Types...>;
template <typename... Types> using ContributeSubscriptions = Subscriptions<Types...>;
} // namespace solar::bus

template <typename Component>
struct solar::contribution_source<solar::bus::MessageTag, Component,
                                  std::void_t<typename Component::Messages>>
{
    using type = typename Component::Messages;
};

template <typename Component>
struct solar::contribution_source<solar::bus::SubscriptionTag, Component,
                                  std::void_t<typename Component::Subscriptions>>
{
    using type = typename Component::Subscriptions;
};
