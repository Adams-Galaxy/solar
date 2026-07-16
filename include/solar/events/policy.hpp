#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "solar/core/time.hpp"
#include "solar/events/types.hpp"
#include "solar/execution/types.hpp"
#include "solar/system/sections.hpp"

namespace solar::events
{

namespace capture
{
struct EveryOccurrence
{};

template <std::size_t N> struct SampleEvery
{
    static_assert(N > 0, "SOLAR_DIAGNOSTIC_EVENT_SAMPLE_ZERO: sampling period must be positive");
    static constexpr std::size_t period = N;
};

template <typename Interval> struct RateLimited
{
    static_assert(
        requires { Interval::value; },
        "SOLAR_DIAGNOSTIC_EVENT_RATE_INTERVAL: rate limit requires a typed interval");
    using IntervalType = Interval;
};

template <typename Window, typename Key = void, std::size_t KeyCapacity = 1> struct AggregateCount
{
    static_assert(
        requires { Window::value; },
        "SOLAR_DIAGNOSTIC_EVENT_AGGREGATE_WINDOW: aggregate requires a typed window");
    static_assert(KeyCapacity > 0,
                  "SOLAR_DIAGNOSTIC_EVENT_AGGREGATE_CAPACITY: key capacity must be positive");
    using WindowType = Window;
    using KeyType = Key;
    static constexpr std::size_t key_capacity = KeyCapacity;
};
} // namespace capture

namespace retention
{
struct Transient
{};

struct Buffered
{};

namespace exhaustion
{
struct LatchAndReject
{};
struct Panic
{};
} // namespace exhaustion

template <std::size_t ReservedSlots, typename Exhaustion = exhaustion::LatchAndReject>
struct Critical
{
    static_assert(ReservedSlots > 0,
                  "SOLAR_DIAGNOSTIC_EVENT_CRITICAL_RESERVATION: reservation must be positive");
    static constexpr std::size_t reserved_slots = ReservedSlots;
    using ExhaustionPolicy = Exhaustion;
};

template <typename Store> struct Persistent
{
    using StoreType = Store;
};
} // namespace retention

namespace interval
{
template <std::int64_t Value> struct Milliseconds
{
    static_assert(Value > 0,
                  "SOLAR_DIAGNOSTIC_EVENT_INTERVAL_NONPOSITIVE: interval must be positive");
    static constexpr DurationValue value{std::chrono::milliseconds{Value}};
};

template <std::int64_t Value> struct Seconds
{
    static_assert(Value > 0,
                  "SOLAR_DIAGNOSTIC_EVENT_INTERVAL_NONPOSITIVE: interval must be positive");
    static constexpr DurationValue value{std::chrono::seconds{Value}};
};
} // namespace interval

template <typename Policy> struct DefaultCapture
{
    using PolicyType = Policy;
};

template <typename Policy> struct DefaultRetention
{
    using PolicyType = Policy;
};

template <typename Executor> struct ProcessorExecutor
{
    using ExecutorType = Executor;
};

namespace stop
{
struct Drain
{};
struct CancelPending
{};
} // namespace stop

template <typename Policy> struct ProcessorStop
{
    using PolicyType = Policy;
};

namespace detail
{
struct DefaultCaptureAxis
{};
struct DefaultRetentionAxis
{};
struct ProcessorExecutorAxis
{};
struct ProcessorStopAxis
{};

template <typename Event, typename = void> struct DeclaredCapture
{
    using type = NoPolicy;
};

template <typename Event> struct DeclaredCapture<Event, std::void_t<typename Event::Capture>>
{
    using type = typename Event::Capture;
};

template <typename Event, typename = void> struct DeclaredRetention
{
    using type = NoPolicy;
};

template <typename Event> struct DeclaredRetention<Event, std::void_t<typename Event::Retention>>
{
    using type = typename Event::Retention;
};

template <typename Policy> struct CaptureTraits
{
    static constexpr bool valid = false;
};

template <> struct CaptureTraits<capture::EveryOccurrence>
{
    static constexpr bool valid = true;
    static constexpr CaptureKind kind = CaptureKind::EveryOccurrence;
    static constexpr bool isr_compatible = true;
    static constexpr bool aggregate = false;
    static constexpr bool keyed = false;
    static constexpr std::size_t key_capacity = 0;
};

template <std::size_t N> struct CaptureTraits<capture::SampleEvery<N>>
{
    static constexpr bool valid = true;
    static constexpr CaptureKind kind = CaptureKind::SampleEvery;
    static constexpr bool isr_compatible = true;
    static constexpr bool aggregate = false;
    static constexpr bool keyed = false;
    static constexpr std::size_t key_capacity = 0;
    static constexpr std::size_t period = N;
};

template <typename Interval> struct CaptureTraits<capture::RateLimited<Interval>>
{
    static constexpr bool valid = true;
    static constexpr CaptureKind kind = CaptureKind::RateLimited;
    static constexpr bool isr_compatible = false;
    static constexpr bool aggregate = false;
    static constexpr bool keyed = false;
    static constexpr std::size_t key_capacity = 0;
    static constexpr DurationValue interval = Interval::value;
};

template <typename Window, typename Key, std::size_t Capacity>
struct CaptureTraits<capture::AggregateCount<Window, Key, Capacity>>
{
    static constexpr bool valid = true;
    static constexpr CaptureKind kind = CaptureKind::AggregateCount;
    static constexpr bool isr_compatible = std::is_void_v<Key>;
    static constexpr bool aggregate = true;
    static constexpr bool keyed = !std::is_void_v<Key>;
    static constexpr DurationValue window = Window::value;
    using KeyType = Key;
    static constexpr std::size_t key_capacity = Capacity;
};

template <typename Extractor, typename EventT>
concept AggregationKeyExtractor =
    !payload_free_v<EventT> && requires(const PayloadOf<EventT>& payload) {
        typename Extractor::Value;
        requires std::is_trivially_copyable_v<typename Extractor::Value>;
        requires std::is_trivially_destructible_v<typename Extractor::Value>;
        requires std::equality_comparable<typename Extractor::Value>;
        { Extractor::get(payload) } -> std::same_as<typename Extractor::Value>;
    };

template <typename Policy> struct RetentionTraits
{
    static constexpr bool valid = false;
};

template <> struct RetentionTraits<retention::Transient>
{
    static constexpr bool valid = true;
    static constexpr RetentionKind kind = RetentionKind::Transient;
    static constexpr bool critical = false;
    static constexpr bool persistent = false;
    static constexpr std::size_t reserved_slots = 0;
    static constexpr bool panic_on_exhaustion = false;
};

template <> struct RetentionTraits<retention::Buffered>
{
    static constexpr bool valid = true;
    static constexpr RetentionKind kind = RetentionKind::Buffered;
    static constexpr bool critical = false;
    static constexpr bool persistent = false;
    static constexpr std::size_t reserved_slots = 0;
    static constexpr bool panic_on_exhaustion = false;
};

template <std::size_t Slots, typename Exhaustion>
struct RetentionTraits<retention::Critical<Slots, Exhaustion>>
{
    static constexpr bool valid = true;
    static constexpr RetentionKind kind = RetentionKind::Critical;
    static constexpr bool critical = true;
    static constexpr bool persistent = false;
    static constexpr std::size_t reserved_slots = Slots;
    static constexpr bool panic_on_exhaustion =
        std::is_same_v<Exhaustion, retention::exhaustion::Panic>;
    using ExhaustionPolicy = Exhaustion;
};

template <typename Store> struct RetentionTraits<retention::Persistent<Store>>
{
    static constexpr bool valid = true;
    static constexpr RetentionKind kind = RetentionKind::Persistent;
    static constexpr bool critical = false;
    static constexpr bool persistent = true;
    static constexpr std::size_t reserved_slots = 0;
    static constexpr bool panic_on_exhaustion = false;
    using StoreType = Store;
};

} // namespace detail

} // namespace solar::events

template <typename Policy>
struct solar::subsystem_policy_traits<solar::events::Tag, solar::events::DefaultCapture<Policy>>
{
    static constexpr bool recognized = solar::events::detail::CaptureTraits<Policy>::valid;
    using Axis = solar::events::detail::DefaultCaptureAxis;
};

template <typename Policy>
struct solar::subsystem_policy_traits<solar::events::Tag, solar::events::DefaultRetention<Policy>>
{
    static constexpr bool recognized = solar::events::detail::RetentionTraits<Policy>::valid;
    using Axis = solar::events::detail::DefaultRetentionAxis;
};

template <typename Executor>
struct solar::subsystem_policy_traits<solar::events::Tag,
                                      solar::events::ProcessorExecutor<Executor>>
{
    static constexpr bool recognized = true;
    using Axis = solar::events::detail::ProcessorExecutorAxis;
};

template <typename Policy>
struct solar::subsystem_policy_traits<solar::events::Tag, solar::events::ProcessorStop<Policy>>
{
    static constexpr bool recognized = std::is_same_v<Policy, solar::events::stop::Drain> ||
                                       std::is_same_v<Policy, solar::events::stop::CancelPending>;
    using Axis = solar::events::detail::ProcessorStopAxis;
};
