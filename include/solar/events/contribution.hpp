#pragma once

#include <type_traits>

#include "solar/catalog/contribution.hpp"

namespace solar::events
{
struct Tag;
struct ProcessorTag;
struct DefaultProcessorTag;
struct InfrastructureObserver;
template <typename EventT, typename Observer, typename RouteTag> struct Process;
template <typename... Types> using Contribute = Contribution<Tag, Types...>;

template <typename Component, typename = void> struct event_processor_extensions
{
    using type = Contribution<ProcessorTag>;
};

template <typename Component, typename = void> struct event_log_extensions
{
    using type = Contribution<ProcessorTag>;
};
} // namespace solar::events

namespace solar::events::detail
{

template <typename Component, typename = void> struct AuthoredProcessors
{
    using type = Contribution<ProcessorTag>;
};

template <typename Component>
struct AuthoredProcessors<Component, std::void_t<typename Component::EventProcessors>>
{
    using type = typename Component::EventProcessors;
};

template <typename Left, typename Right> struct MergeProcessors;

template <typename... Left, typename... Right>
struct MergeProcessors<Contribution<ProcessorTag, Left...>, Contribution<ProcessorTag, Right...>>
{
    using type = Contribution<ProcessorTag, Left..., Right...>;
};

} // namespace solar::events::detail

template <typename Component>
struct solar::contribution_source<solar::events::Tag, Component,
                                  std::void_t<typename Component::Events>>
{
    using type = typename Component::Events;
};

template <typename Component>
struct solar::contribution_source<solar::events::ProcessorTag, Component>
{
  private:
    using AuthoredAndAdapters = typename solar::events::detail::MergeProcessors<
        typename solar::events::detail::AuthoredProcessors<Component>::type,
        typename solar::events::event_processor_extensions<Component>::type>::type;

  public:
    using type = typename solar::events::detail::MergeProcessors<
        AuthoredAndAdapters, typename solar::events::event_log_extensions<Component>::type>::type;
};

template <typename Component, typename EventT, typename Observer, typename RouteTag>
struct solar::contributed_declaration<solar::events::ProcessorTag, Component,
                                      solar::events::Process<EventT, Observer, RouteTag>>
{
  private:
    static constexpr bool infrastructure = [] {
        if constexpr (requires { typename Observer::EventRole; }) {
            return std::is_same_v<typename Observer::EventRole,
                                  solar::events::InfrastructureObserver>;
        } else {
            return false;
        }
    }();

  public:
    static_assert(std::is_same_v<Observer, Component> || infrastructure,
                  "SOLAR_DIAGNOSTIC_EVENT_PROCESSOR_OWNER: component-local Process observer must "
                  "be its contributing component or a declared infrastructure observer");
    using type =
        solar::events::Process<EventT, std::conditional_t<infrastructure, Observer, Component>,
                               RouteTag>;
};
