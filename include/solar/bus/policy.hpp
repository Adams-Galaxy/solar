#pragma once

#include <type_traits>

#include "solar/bus/delivery.hpp"
#include "solar/system/sections.hpp"

namespace solar::bus::detail
{

template <typename Axis, typename Policies> struct PolicyForAxis;

template <typename Axis> struct PolicyForAxis<Axis, TypeList<>>
{
    using type = NoPolicy;
};

template <typename Axis, typename Head, typename... Tail>
struct PolicyForAxis<Axis, TypeList<Head, Tail...>>
{
  private:
    using Remaining = typename PolicyForAxis<Axis, TypeList<Tail...>>::type;
    using HeadAxis = typename subsystem_policy_traits<Tag, Head>::Axis;

  public:
    using type = std::conditional_t<std::is_same_v<Axis, HeadAxis>, Head, Remaining>;
};

template <typename Axis, typename Policies>
using policy_for_axis_t = typename PolicyForAxis<Axis, Policies>::type;

#if defined(CONFIG_SOLAR_BUS_DEFAULT_OVERFLOW_DROP_NEWEST)
using KconfigOverflow = overflow::DropNewest;
#elif defined(CONFIG_SOLAR_BUS_DEFAULT_OVERFLOW_DROP_OLDEST)
using KconfigOverflow = overflow::DropOldest;
#else
using KconfigOverflow = overflow::Reject;
#endif

#if defined(CONFIG_SOLAR_BUS_DEFAULT_STOP_CANCEL_PENDING)
using KconfigStop = stop::CancelPending;
#else
using KconfigStop = stop::Drain;
#endif

#if defined(CONFIG_SOLAR_BUS_DEFAULT_QUEUE_CAPACITY)
inline constexpr std::size_t kconfig_capacity = CONFIG_SOLAR_BUS_DEFAULT_QUEUE_CAPACITY;
#else
inline constexpr std::size_t kconfig_capacity = 1;
#endif

template <typename Policy, bool Default = std::is_same_v<Policy, NoPolicy>>
struct ResolveConfiguredOverflow
{
    using type = typename Policy::PolicyType;
};

template <typename Policy> struct ResolveConfiguredOverflow<Policy, true>
{
    using type = KconfigOverflow;
};

template <typename Config>
struct ConfiguredOverflow
    : ResolveConfiguredOverflow<policy_for_axis_t<DefaultOverflowAxis, Config>>
{};

template <typename Policy, bool Default = std::is_same_v<Policy, NoPolicy>>
struct ResolveConfiguredStop
{
    using type = typename Policy::PolicyType;
};

template <typename Policy> struct ResolveConfiguredStop<Policy, true>
{
    using type = KconfigStop;
};

template <typename Config>
struct ConfiguredStop : ResolveConfiguredStop<policy_for_axis_t<DefaultStopAxis, Config>>
{};

template <typename Config> struct ConfiguredCapacity
{
    using Policy = policy_for_axis_t<DefaultCapacityAxis, Config>;
    static constexpr std::size_t value = [] {
        if constexpr (std::is_same_v<Policy, NoPolicy>) {
            return kconfig_capacity;
        } else {
            return Policy::value;
        }
    }();
};

template <typename Authored, typename Config>
using effective_overflow_t =
    std::conditional_t<std::is_same_v<Authored, UseDefaultOverflow>,
                       typename ConfiguredOverflow<Config>::type, Authored>;

template <typename Authored, typename Config>
using effective_stop_t = std::conditional_t<std::is_same_v<Authored, UseDefaultStop>,
                                            typename ConfiguredStop<Config>::type, Authored>;

template <typename Delivery, typename Config> struct EffectiveDelivery;

template <typename Config> struct EffectiveDelivery<delivery::Inline, Config>
{
    static constexpr DeliveryKind kind = DeliveryKind::Inline;
    static constexpr bool asynchronous = false;
    static constexpr bool isr_compatible = false;
    static constexpr std::size_t capacity = 0;
    using Target = void;
    using Overflow = overflow::Reject;
    using Stop = stop::Drain;
};

template <typename Config> struct EffectiveDelivery<delivery::InlineIsr, Config>
{
    static constexpr DeliveryKind kind = DeliveryKind::InlineIsr;
    static constexpr bool asynchronous = false;
    static constexpr bool isr_compatible = true;
    static constexpr std::size_t capacity = 0;
    using Target = void;
    using Overflow = overflow::Reject;
    using Stop = stop::Drain;
};

template <typename Executor, std::size_t AuthoredCapacity, typename AuthoredOverflow,
          typename AuthoredStop, typename Config>
struct EffectiveDelivery<
    delivery::Queued<Executor, AuthoredCapacity, AuthoredOverflow, AuthoredStop>, Config>
{
    static constexpr DeliveryKind kind = DeliveryKind::Queued;
    static constexpr bool asynchronous = true;
    static constexpr std::size_t capacity =
        AuthoredCapacity == default_capacity ? ConfiguredCapacity<Config>::value : AuthoredCapacity;
    using Target = Executor;
    using Overflow = effective_overflow_t<AuthoredOverflow, Config>;
    using Stop = effective_stop_t<AuthoredStop, Config>;
    static constexpr bool isr_compatible = !requires { Overflow::timeout; };
};

template <typename Executor, typename AuthoredStop, typename Config>
struct EffectiveDelivery<delivery::Latest<Executor, AuthoredStop>, Config>
{
    static constexpr DeliveryKind kind = DeliveryKind::Latest;
    static constexpr bool asynchronous = true;
    static constexpr bool isr_compatible = true;
    static constexpr std::size_t capacity = 1;
    using Target = Executor;
    using Overflow = overflow::DropOldest;
    using Stop = effective_stop_t<AuthoredStop, Config>;
};

template <typename Executor, typename AuthoredStop, typename Config>
struct EffectiveDelivery<delivery::Coalesced<Executor, AuthoredStop>, Config>
{
    static constexpr DeliveryKind kind = DeliveryKind::Coalesced;
    static constexpr bool asynchronous = true;
    static constexpr bool isr_compatible = true;
    static constexpr std::size_t capacity = 1;
    using Target = Executor;
    using Overflow = overflow::DropNewest;
    using Stop = effective_stop_t<AuthoredStop, Config>;
};

} // namespace solar::bus::detail
