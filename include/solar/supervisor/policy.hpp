#pragma once

#include <type_traits>

#include "solar/core/type_list.hpp"
#include "solar/system/sections.hpp"

namespace solar::supervisor
{

#if defined(CONFIG_SOLAR_SUPERVISOR)
inline constexpr bool available = true;
#else
inline constexpr bool available = false;
#endif

struct Tag;
struct ResponsePolicyAxis;
struct WatchdogPolicyAxis;

struct Observe
{};

struct Warn
{};

struct Latch
{};

template <typename Component> struct TryRecover
{
    using Target = Component;
};

template <typename SafeState> struct EnterSafeState
{
    using Target = SafeState;
};

template <typename Component> struct RequestStop
{
    using Target = Component;
};

struct RequestSystemStop
{};

struct RequestReboot
{};

struct StopFeedingWatchdog
{};

struct Panic
{};

template <typename Component, typename... Actions> struct OnFault
{
    using Subject = Component;
    using Responses = TypeList<Actions...>;
};

template <typename Component, typename... Actions> struct OnDegraded
{
    using Subject = Component;
    using Responses = TypeList<Actions...>;
};

template <typename Component, typename... Actions> struct OnStall
{
    using Subject = Component;
    using Responses = TypeList<Actions...>;
};

template <typename Component, typename... Actions> struct OnRecoveryFailure
{
    using Subject = Component;
    using Responses = TypeList<Actions...>;
};

template <typename... Rules> struct Policy
{
    using RuleTypes = TypeList<Rules...>;
};

struct NoWatchdog
{
    static constexpr bool configured = false;
};

template <typename Provider> struct Watchdog
{
    using ProviderType = Provider;
    static constexpr bool configured = true;
};

template <typename... Policies> using Configuration = SubsystemConfiguration<Tag, Policies...>;

namespace detail
{

template <typename T> struct IsRule : std::false_type
{};

template <typename C, typename... A> struct IsRule<OnFault<C, A...>> : std::true_type
{};
template <typename C, typename... A> struct IsRule<OnDegraded<C, A...>> : std::true_type
{};
template <typename C, typename... A> struct IsRule<OnStall<C, A...>> : std::true_type
{};
template <typename C, typename... A> struct IsRule<OnRecoveryFailure<C, A...>> : std::true_type
{};

template <typename T> struct IsAction : std::false_type
{};

template <> struct IsAction<Observe> : std::true_type
{};
template <> struct IsAction<Warn> : std::true_type
{};
template <> struct IsAction<Latch> : std::true_type
{};
template <typename C> struct IsAction<TryRecover<C>> : std::true_type
{};
template <typename A> struct IsAction<EnterSafeState<A>> : std::true_type
{};
template <typename C> struct IsAction<RequestStop<C>> : std::true_type
{};
template <> struct IsAction<RequestSystemStop> : std::true_type
{};
template <> struct IsAction<RequestReboot> : std::true_type
{};
template <> struct IsAction<StopFeedingWatchdog> : std::true_type
{};
template <> struct IsAction<Panic> : std::true_type
{};

template <typename Rule> struct RuleValid : std::false_type
{};

template <template <typename, typename...> class Rule, typename Component, typename... Actions>
struct RuleValid<Rule<Component, Actions...>>
    : std::bool_constant<(sizeof...(Actions) > 0) && (IsAction<Actions>::value && ...)>
{};

template <typename T> struct IsPolicy : std::false_type
{};

template <typename... Rules>
struct IsPolicy<Policy<Rules...>>
    : std::bool_constant<(IsRule<Rules>::value && ...) && (RuleValid<Rules>::value && ...)>
{};

template <typename T> struct IsWatchdog : std::false_type
{};

template <typename Provider> struct IsWatchdog<Watchdog<Provider>> : std::true_type
{};

template <typename Configuration> struct SelectPolicy;

template <> struct SelectPolicy<TypeList<>>
{
    using Responses = Policy<>;
    using WatchdogPolicy = NoWatchdog;
};

template <typename Head, typename... Tail> struct SelectPolicy<TypeList<Head, Tail...>>
{
  private:
    using Rest = SelectPolicy<TypeList<Tail...>>;

  public:
    using Responses = std::conditional_t<IsPolicy<Head>::value, Head, typename Rest::Responses>;
    using WatchdogPolicy =
        std::conditional_t<IsWatchdog<Head>::value, Head, typename Rest::WatchdogPolicy>;
};

} // namespace detail

template <typename Components, typename ConfigurationT> struct Architecture
{
    using ComponentTypes = Components;
    using ResponsePolicy = typename detail::SelectPolicy<ConfigurationT>::Responses;
    using WatchdogPolicy = typename detail::SelectPolicy<ConfigurationT>::WatchdogPolicy;
};

} // namespace solar::supervisor

template <typename... Rules>
struct solar::subsystem_policy_traits<solar::supervisor::Tag, solar::supervisor::Policy<Rules...>>
{
    static constexpr bool recognized =
        solar::supervisor::detail::IsPolicy<solar::supervisor::Policy<Rules...>>::value;
    static constexpr bool available = solar::supervisor::available;
    using Axis = solar::supervisor::ResponsePolicyAxis;
};

template <typename Provider>
struct solar::subsystem_policy_traits<solar::supervisor::Tag, solar::supervisor::Watchdog<Provider>>
{
    static constexpr bool recognized = true;
    static constexpr bool available = solar::supervisor::available;
    using Axis = solar::supervisor::WatchdogPolicyAxis;
};
