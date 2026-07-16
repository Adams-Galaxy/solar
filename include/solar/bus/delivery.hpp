#pragma once

#include <cstddef>
#include <type_traits>

#include "solar/bus/types.hpp"
#include "solar/core/time.hpp"
#include "solar/execution/types.hpp"

namespace solar
{
template <typename SubsystemTag, typename Policy> struct subsystem_policy_traits;
}

namespace solar::bus
{

namespace overflow
{
struct Reject
{};

struct DropNewest
{};

struct DropOldest
{};

template <DurationValue Timeout> struct WaitFor
{
    static_assert(Timeout.positive(),
                  "SOLAR_DIAGNOSTIC_BUS_NONPOSITIVE_WAIT: bounded wait must be positive");
    static constexpr DurationValue timeout = Timeout;
};
} // namespace overflow

namespace stop
{
struct Drain
{};

struct CancelPending
{};
} // namespace stop

struct UseDefaultCapacity
{};

struct UseDefaultOverflow
{};

struct UseDefaultStop
{};

inline constexpr std::size_t default_capacity = static_cast<std::size_t>(-1);

namespace delivery
{
struct Inline
{};

struct InlineIsr
{};

template <typename Executor, std::size_t Capacity = default_capacity,
          typename Overflow = UseDefaultOverflow, typename Stop = UseDefaultStop>
struct Queued
{};

template <typename Executor, typename Stop = UseDefaultStop> struct Latest
{};

template <typename Executor, typename Stop = UseDefaultStop> struct Coalesced
{};
} // namespace delivery

template <std::size_t Value> struct Capacity
{
    static constexpr std::size_t value = Value;
};

template <typename Policy> struct DefaultOverflow
{
    using PolicyType = Policy;
};

template <typename Policy> struct DefaultStop
{
    using PolicyType = Policy;
};

template <std::size_t Value> struct DefaultCapacity
{
    static constexpr std::size_t value = Value;
};

template <DurationValue Value> struct StopTimeout
{
    static constexpr DurationValue value = Value;
};

template <typename Message> struct RequireSubscriber
{
    using MessageType = Message;
};

template <typename Delivery> struct delivery_traits
{
    static constexpr bool valid = false;
};

template <> struct delivery_traits<delivery::Inline>
{
    static constexpr bool valid = true;
    static constexpr DeliveryKind kind = DeliveryKind::Inline;
    static constexpr bool asynchronous = false;
    static constexpr bool isr_compatible = false;
    using Target = void;
    using CapacityPolicy = Capacity<0>;
    using Overflow = overflow::Reject;
    using Stop = stop::Drain;
};

template <> struct delivery_traits<delivery::InlineIsr>
{
    static constexpr bool valid = true;
    static constexpr DeliveryKind kind = DeliveryKind::InlineIsr;
    static constexpr bool asynchronous = false;
    static constexpr bool isr_compatible = true;
    using Target = void;
    using CapacityPolicy = Capacity<0>;
    using Overflow = overflow::Reject;
    using Stop = stop::Drain;
};

template <typename Executor, std::size_t CapacityValue, typename Overflow, typename Stop>
struct delivery_traits<delivery::Queued<Executor, CapacityValue, Overflow, Stop>>
{
    static constexpr bool valid = true;
    static constexpr DeliveryKind kind = DeliveryKind::Queued;
    static constexpr bool asynchronous = true;
    static constexpr bool isr_compatible = !requires { Overflow::timeout; };
    using Target = Executor;
    using CapacityPolicyType = Capacity<CapacityValue>;
    using OverflowPolicy = Overflow;
    using StopPolicy = Stop;
};

template <typename Executor, typename Stop> struct delivery_traits<delivery::Latest<Executor, Stop>>
{
    static constexpr bool valid = true;
    static constexpr DeliveryKind kind = DeliveryKind::Latest;
    static constexpr bool asynchronous = true;
    static constexpr bool isr_compatible = true;
    using Target = Executor;
    using CapacityPolicyType = Capacity<1>;
    using OverflowPolicy = overflow::DropOldest;
    using StopPolicy = Stop;
};

template <typename Executor, typename Stop>
struct delivery_traits<delivery::Coalesced<Executor, Stop>>
{
    static constexpr bool valid = true;
    static constexpr DeliveryKind kind = DeliveryKind::Coalesced;
    static constexpr bool asynchronous = true;
    static constexpr bool isr_compatible = true;
    using Target = Executor;
    using CapacityPolicyType = Capacity<1>;
    using OverflowPolicy = overflow::DropNewest;
    using StopPolicy = Stop;
};

namespace detail
{
struct DefaultOverflowAxis
{};
struct DefaultStopAxis
{};
struct DefaultCapacityAxis
{};
struct StopTimeoutAxis
{};
template <typename Message> struct RequiredSubscriberAxis
{};
} // namespace detail

} // namespace solar::bus

template <typename Policy>
struct solar::subsystem_policy_traits<solar::bus::Tag, solar::bus::DefaultOverflow<Policy>>
{
    static constexpr bool recognized = true;
    using Axis = solar::bus::detail::DefaultOverflowAxis;
};

template <typename Policy>
struct solar::subsystem_policy_traits<solar::bus::Tag, solar::bus::DefaultStop<Policy>>
{
    static constexpr bool recognized = true;
    using Axis = solar::bus::detail::DefaultStopAxis;
};

template <std::size_t Value>
struct solar::subsystem_policy_traits<solar::bus::Tag, solar::bus::DefaultCapacity<Value>>
{
    static constexpr bool recognized = true;
    using Axis = solar::bus::detail::DefaultCapacityAxis;
};

template <solar::DurationValue Value>
struct solar::subsystem_policy_traits<solar::bus::Tag, solar::bus::StopTimeout<Value>>
{
    static constexpr bool recognized = true;
    using Axis = solar::bus::detail::StopTimeoutAxis;
};

template <typename Message>
struct solar::subsystem_policy_traits<solar::bus::Tag, solar::bus::RequireSubscriber<Message>>
{
    static constexpr bool recognized = true;
    using Axis = solar::bus::detail::RequiredSubscriberAxis<Message>;
};
