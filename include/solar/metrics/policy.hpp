#pragma once

#include <type_traits>

#include "solar/metrics/reducer.hpp"
#include "solar/system/sections.hpp"

namespace solar::metrics
{

namespace concurrency
{
struct Automatic
{};
struct Atomic
{};
struct SpinLocked
{};
struct MutexProtected
{};
} // namespace concurrency

namespace overflow
{
struct Saturate
{};
struct Reject
{};
struct Wrap
{};
} // namespace overflow

namespace numeric
{
struct RejectNonFinite
{};
struct PreserveNonFinite
{};
} // namespace numeric

struct RuntimeResettable
{};
struct BootResetOnly
{};

namespace timestamps
{
struct Enabled
{};
struct Disabled
{};
} // namespace timestamps

template <typename Policy> struct DefaultConcurrency
{
    using PolicyType = Policy;
};

template <typename Policy> struct DefaultOverflow
{
    using PolicyType = Policy;
};

template <typename Policy> struct DefaultNumeric
{
    using PolicyType = Policy;
};

template <typename Policy> struct DefaultTimestamps
{
    using PolicyType = Policy;
};

namespace detail
{
struct ConcurrencyAxis;
struct OverflowAxis;
struct NumericAxis;
struct TimestampAxis;

template <typename Metric, typename = void> struct DeclaredConcurrency
{
    using type = NoPolicy;
};

template <typename Metric>
struct DeclaredConcurrency<Metric, std::void_t<typename Metric::Concurrency>>
{
    using type = typename Metric::Concurrency;
};

template <typename Metric, typename = void> struct DeclaredOverflow
{
    using type = NoPolicy;
};

template <typename Metric> struct DeclaredOverflow<Metric, std::void_t<typename Metric::Overflow>>
{
    using type = typename Metric::Overflow;
};

template <typename Metric, typename = void> struct DeclaredNumeric
{
    using type = NoPolicy;
};

template <typename Metric> struct DeclaredNumeric<Metric, std::void_t<typename Metric::Numeric>>
{
    using type = typename Metric::Numeric;
};

template <typename Metric, typename = void> struct DeclaredReset
{
    using type = BootResetOnly;
};

template <typename Metric> struct DeclaredReset<Metric, std::void_t<typename Metric::Reset>>
{
    using type = typename Metric::Reset;
};

template <typename Metric, typename = void> struct DeclaredTimestamps
{
    using type = NoPolicy;
};

template <typename Metric>
struct DeclaredTimestamps<Metric, std::void_t<typename Metric::Timestamps>>
{
    using type = typename Metric::Timestamps;
};

template <typename Policy> struct ConcurrencyTraits
{
    static constexpr bool valid = false;
};

template <> struct ConcurrencyTraits<concurrency::Automatic>
{
    static constexpr bool valid = true;
};

template <> struct ConcurrencyTraits<concurrency::Atomic>
{
    static constexpr bool valid = true;
    static constexpr ConcurrencyKind kind = ConcurrencyKind::Atomic;
};

template <> struct ConcurrencyTraits<concurrency::SpinLocked>
{
    static constexpr bool valid = true;
    static constexpr ConcurrencyKind kind = ConcurrencyKind::SpinLocked;
};

template <> struct ConcurrencyTraits<concurrency::MutexProtected>
{
    static constexpr bool valid = true;
    static constexpr ConcurrencyKind kind = ConcurrencyKind::MutexProtected;
};

template <typename Policy> struct OverflowTraits
{
    static constexpr bool valid = false;
};

template <> struct OverflowTraits<overflow::Saturate>
{
    static constexpr bool valid = true;
    static constexpr OverflowKind kind = OverflowKind::Saturate;
};

template <> struct OverflowTraits<overflow::Reject>
{
    static constexpr bool valid = true;
    static constexpr OverflowKind kind = OverflowKind::Reject;
};

template <> struct OverflowTraits<overflow::Wrap>
{
    static constexpr bool valid = true;
    static constexpr OverflowKind kind = OverflowKind::Wrap;
};

template <typename Policy> struct NumericTraits
{
    static constexpr bool valid = false;
};

template <> struct NumericTraits<numeric::RejectNonFinite>
{
    static constexpr bool valid = true;
};

template <> struct NumericTraits<numeric::PreserveNonFinite>
{
    static constexpr bool valid = true;
};

template <typename Policy> struct ResetTraits
{
    static constexpr bool valid = false;
};

template <> struct ResetTraits<RuntimeResettable>
{
    static constexpr bool valid = true;
};

template <> struct ResetTraits<BootResetOnly>
{
    static constexpr bool valid = true;
};

template <typename Policy> struct TimestampTraits
{
    static constexpr bool valid = false;
};

template <> struct TimestampTraits<timestamps::Enabled>
{
    static constexpr bool valid = true;
};

template <> struct TimestampTraits<timestamps::Disabled>
{
    static constexpr bool valid = true;
};

} // namespace detail

} // namespace solar::metrics

template <typename Policy>
struct solar::subsystem_policy_traits<solar::metrics::Tag,
                                      solar::metrics::DefaultConcurrency<Policy>>
{
    static constexpr bool recognized = solar::metrics::detail::ConcurrencyTraits<Policy>::valid;
    using Axis = solar::metrics::detail::ConcurrencyAxis;
};

template <typename Policy>
struct solar::subsystem_policy_traits<solar::metrics::Tag, solar::metrics::DefaultOverflow<Policy>>
{
    static constexpr bool recognized = solar::metrics::detail::OverflowTraits<Policy>::valid;
    using Axis = solar::metrics::detail::OverflowAxis;
};

template <typename Policy>
struct solar::subsystem_policy_traits<solar::metrics::Tag, solar::metrics::DefaultNumeric<Policy>>
{
    static constexpr bool recognized = solar::metrics::detail::NumericTraits<Policy>::valid;
    using Axis = solar::metrics::detail::NumericAxis;
};

template <typename Policy>
struct solar::subsystem_policy_traits<solar::metrics::Tag,
                                      solar::metrics::DefaultTimestamps<Policy>>
{
    static constexpr bool recognized = solar::metrics::detail::TimestampTraits<Policy>::valid;
    using Axis = solar::metrics::detail::TimestampAxis;
};
