#pragma once

#include <atomic>
#include <cstddef>
#include <type_traits>

#include "solar/metrics/catalog.hpp"
#include "solar/metrics/contribution.hpp"

#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_METRICS)
#include <zephyr/sys/util_macro.h>

#include "solar/kernel/spinlock.hpp"
#include "solar/metrics/storage.hpp"
#endif

namespace solar::metrics
{

#if defined(CONFIG_SOLAR) && !defined(CONFIG_SOLAR_METRICS)
inline constexpr bool enabled = false;
#else
inline constexpr bool enabled = true;
#endif

template <typename Architecture> struct Facility;

namespace detail
{

template <typename List> struct DeclarationsOf;

template <typename... Entries> struct DeclarationsOf<TypeList<Entries...>>
{
    using type = TypeList<typename Entries::Declaration...>;
};

template <typename List> using declarations_of_t = typename DeclarationsOf<List>::type;

} // namespace detail

#if !defined(__ZEPHYR__) || !defined(CONFIG_SOLAR_METRICS)

template <typename MetricDeclarations, typename Components, typename Configuration>
struct Architecture
{
    using Metrics = MetricDeclarations;
    using ComponentTypes = Components;
    using ConfigurationPolicies = Configuration;
    using BootstrapDependencies = TypeList<>;
    static constexpr bool demanded =
        list_size_v<Metrics> != 0 || list_size_v<ConfigurationPolicies> != 0;
};

template <typename ArchitectureT> struct Facility
{
    using Architecture = ArchitectureT;
    using MetricTypes = typename Architecture::Metrics;
    using Dependencies = solar::Dependencies<>;

    static constexpr component::Descriptor descriptor{
        .name = "solar.metrics",
        .description = "Typed passive metrics",
    };

    template <typename> static void activate_runtime() noexcept {}
};

#else

namespace detail
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

template <typename Wrapper, typename Fallback, bool Missing = std::is_same_v<Wrapper, NoPolicy>>
struct UnwrapPolicy
{
    using type = typename Wrapper::PolicyType;
};

template <typename Wrapper, typename Fallback> struct UnwrapPolicy<Wrapper, Fallback, true>
{
    using type = Fallback;
};

template <typename Axis, typename Configuration, typename Fallback>
using configured_policy_t =
    typename UnwrapPolicy<policy_for_axis_t<Axis, Configuration>, Fallback>::type;

#if defined(CONFIG_SOLAR_METRICS_DEFAULT_CONCURRENCY_ATOMIC)
using KconfigConcurrency = concurrency::Atomic;
#elif defined(CONFIG_SOLAR_METRICS_DEFAULT_CONCURRENCY_SPINLOCKED)
using KconfigConcurrency = concurrency::SpinLocked;
#elif defined(CONFIG_SOLAR_METRICS_DEFAULT_CONCURRENCY_MUTEX)
using KconfigConcurrency = concurrency::MutexProtected;
#else
using KconfigConcurrency = concurrency::Automatic;
#endif

#if defined(CONFIG_SOLAR_METRICS_DEFAULT_OVERFLOW_REJECT)
using KconfigOverflow = overflow::Reject;
#elif defined(CONFIG_SOLAR_METRICS_DEFAULT_OVERFLOW_WRAP)
using KconfigOverflow = overflow::Wrap;
#else
using KconfigOverflow = overflow::Saturate;
#endif

#if defined(CONFIG_SOLAR_METRICS_TIMESTAMPS)
using KconfigTimestamps = timestamps::Enabled;
#else
using KconfigTimestamps = timestamps::Disabled;
#endif

template <typename MetricT, typename Configuration> struct MetricPolicies
{
    using Concurrency =
        resolve_policy_t<typename DeclaredConcurrency<MetricT>::type,
                         configured_policy_t<ConcurrencyAxis, Configuration, KconfigConcurrency>,
                         KconfigConcurrency>;
    using Overflow =
        resolve_policy_t<typename DeclaredOverflow<MetricT>::type,
                         configured_policy_t<OverflowAxis, Configuration, KconfigOverflow>,
                         KconfigOverflow>;
    using Numeric =
        resolve_policy_t<typename DeclaredNumeric<MetricT>::type,
                         configured_policy_t<NumericAxis, Configuration, numeric::RejectNonFinite>,
                         numeric::RejectNonFinite>;
    using Reset = typename DeclaredReset<MetricT>::type;
    using Timestamps =
        resolve_policy_t<typename DeclaredTimestamps<MetricT>::type,
                         configured_policy_t<TimestampAxis, Configuration, KconfigTimestamps>,
                         KconfigTimestamps>;
};

template <typename MetricT, typename Policies> consteval bool validate_metric()
{
    static_assert(Metric<MetricT>,
                  "SOLAR_DIAGNOSTIC_INVALID_METRIC: metric requires numeric Value, valid "
                  "Instrument, Unit, and Descriptor");
    using Value = typename MetricT::Value;
    using Instrument = InstrumentTraits<typename MetricT::Instrument>;
    static_assert(sizeof(Value) <= CONFIG_SOLAR_METRICS_MAX_VALUE_BYTES,
                  "SOLAR_DIAGNOSTIC_METRIC_VALUE_SIZE: metric value exceeds configured ceiling");
    static_assert(!Instrument::counter ||
                      (std::unsigned_integral<Value> && !std::same_as<Value, bool>),
                  "SOLAR_DIAGNOSTIC_METRIC_COUNTER_VALUE: counters require unsigned integral "
                  "values");
    static_assert(ConcurrencyTraits<typename Policies::Concurrency>::valid,
                  "SOLAR_DIAGNOSTIC_METRIC_CONCURRENCY: invalid concurrency policy");
    static_assert(OverflowTraits<typename Policies::Overflow>::valid,
                  "SOLAR_DIAGNOSTIC_METRIC_OVERFLOW_POLICY: invalid overflow policy");
    static_assert(NumericTraits<typename Policies::Numeric>::valid,
                  "SOLAR_DIAGNOSTIC_METRIC_NUMERIC_POLICY: invalid numeric policy");
    static_assert(ResetTraits<typename Policies::Reset>::valid,
                  "SOLAR_DIAGNOSTIC_METRIC_RESET_POLICY: invalid reset policy");
    static_assert(TimestampTraits<typename Policies::Timestamps>::valid,
                  "SOLAR_DIAGNOSTIC_METRIC_TIMESTAMP_POLICY: invalid timestamp policy");
    static_assert(!std::is_same_v<typename Policies::Concurrency, concurrency::Atomic> ||
                      ((Instrument::counter || Instrument::gauge) &&
                       std::atomic<Value>::is_always_lock_free && atomic_metadata_lock_free),
                  "SOLAR_DIAGNOSTIC_METRIC_ATOMIC_NOT_LOCK_FREE: explicit atomic metrics require "
                  "a lock-free scalar counter or gauge and metadata scheme on this target");
    static_assert(!std::is_same_v<typename Policies::Concurrency, concurrency::Atomic> ||
                      IS_ENABLED(CONFIG_SOLAR_METRICS_ATOMIC_BACKEND),
                  "SOLAR_DIAGNOSTIC_METRIC_ATOMIC_DISABLED: atomic metrics backend is disabled");
    static_assert(!std::is_same_v<typename Policies::Concurrency, concurrency::SpinLocked> ||
                      IS_ENABLED(CONFIG_SOLAR_METRICS_SPINLOCK_BACKEND),
                  "SOLAR_DIAGNOSTIC_METRIC_SPINLOCK_DISABLED: spinlocked metrics backend is "
                  "disabled");
    static_assert(!std::is_same_v<typename Policies::Concurrency, concurrency::MutexProtected> ||
                      IS_ENABLED(CONFIG_SOLAR_METRICS_MUTEX_BACKEND),
                  "SOLAR_DIAGNOSTIC_METRIC_MUTEX_DISABLED: mutex metrics backend is disabled");
    static_assert(!std::is_same_v<typename Policies::Reset, RuntimeResettable> ||
                      IS_ENABLED(CONFIG_SOLAR_METRICS_RUNTIME_RESET),
                  "SOLAR_DIAGNOSTIC_METRIC_RESET_DISABLED: runtime-resettable metric requires "
                  "CONFIG_SOLAR_METRICS_RUNTIME_RESET");
    static_assert(!std::is_same_v<typename Policies::Timestamps, timestamps::Enabled> ||
                      IS_ENABLED(CONFIG_SOLAR_METRICS_TIMESTAMPS),
                  "SOLAR_DIAGNOSTIC_METRIC_TIMESTAMPS_DISABLED: typed timestamps require "
                  "CONFIG_SOLAR_METRICS_TIMESTAMPS");
    if constexpr (Instrument::distribution) {
        using Reducer = typename Instrument::Reducer;
        static_assert(ReducerTraits<Reducer>::valid,
                      "SOLAR_DIAGNOSTIC_METRIC_REDUCER: distribution requires a supported "
                      "bounded reducer");
        if constexpr (requires { ReducerTraits<Reducer>::capacity; }) {
            static_assert(ReducerTraits<Reducer>::capacity <= CONFIG_SOLAR_METRICS_MAX_WINDOW_SIZE,
                          "SOLAR_DIAGNOSTIC_METRIC_WINDOW_CEILING: window reducer exceeds Kconfig "
                          "capacity");
        }
        if constexpr (requires { ReducerTraits<Reducer>::boundaries; }) {
            static_assert(ReducerTraits<Reducer>::boundaries <=
                              CONFIG_SOLAR_METRICS_MAX_HISTOGRAM_BOUNDARIES,
                          "SOLAR_DIAGNOSTIC_METRIC_HISTOGRAM_CEILING: histogram exceeds Kconfig "
                          "boundary capacity");
        }
    }
    if constexpr (Instrument::timer) {
        static_assert(std::same_as<typename MetricT::Unit::Dimension, dimension::Time>,
                      "SOLAR_DIAGNOSTIC_METRIC_TIMER_UNIT: timer unit must use the time dimension");
    }
    return true;
}

} // namespace detail

template <typename MetricDeclarations, typename Components, typename Configuration>
struct Architecture
{
    using Metrics = MetricDeclarations;
    using ComponentTypes = Components;
    using ConfigurationPolicies = Configuration;
    using BootstrapDependencies = TypeList<>;

    template <typename MetricT> using Policies = detail::MetricPolicies<MetricT, Configuration>;

    static_assert([]<typename... MetricTypes>(TypeList<MetricTypes...>) {
        return (detail::validate_metric<MetricTypes, Policies<MetricTypes>>() && ...);
    }(Metrics{}));

    static constexpr bool demanded =
        list_size_v<Metrics> != 0 || list_size_v<ConfigurationPolicies> != 0;
};

template <typename ArchitectureT> struct Facility
{
    using Architecture = ArchitectureT;
    using MetricTypes = typename Architecture::Metrics;
    using Dependencies = solar::Dependencies<>;

    template <typename MetricT> using Policies = typename Architecture::template Policies<MetricT>;

    template <typename MetricT> inline static detail::MetricSlot<MetricT, Policies<MetricT>> slot{};

    static constexpr component::Descriptor descriptor{
        .name = "solar.metrics",
        .description = "Typed passive metrics",
    };

    inline static std::atomic_bool ready{};
    inline static std::atomic_bool accepting{};
    inline static FacilityRecord lifecycle_record{};
    inline static kernel::SpinLock record_lock{};

    [[nodiscard]] static Result<void> init() noexcept
    {
        std::size_t index{};
        for_each_type<MetricTypes>([&]<typename MetricT> {
            slot<MetricT>.initialize(LocalId{static_cast<LocalId::Representation>(index++)});
        });
        {
            auto guard = record_lock.acquire();
            lifecycle_record = {.last_status = Status::Ok, .ready = true, .accepting = true};
        }
        ready.store(true, std::memory_order_release);
        accepting.store(true, std::memory_order_release);
        return {};
    }

    [[nodiscard]] static Result<void> start() noexcept
    {
        accepting.store(true, std::memory_order_release);
        auto guard = record_lock.acquire();
        lifecycle_record.last_status = Status::Ok;
        lifecycle_record.ready = true;
        lifecycle_record.accepting = true;
        return {};
    }

    [[nodiscard]] static Result<void> stop() noexcept
    {
        accepting.store(false, std::memory_order_release);
        auto guard = record_lock.acquire();
        lifecycle_record.accepting = false;
        return {};
    }

    [[nodiscard]] static Result<void> deinit() noexcept
    {
        accepting.store(false, std::memory_order_release);
        ready.store(false, std::memory_order_release);
        auto guard = record_lock.acquire();
        lifecycle_record.ready = false;
        lifecycle_record.last_status = Status::NotReady;
        return {};
    }

    static void account_success(bool reset = false) noexcept
    {
        auto guard = record_lock.acquire();
        ++lifecycle_record.updates;
        if (reset) {
            ++lifecycle_record.resets;
        }
    }

    static void account_failure(const Error& error) noexcept
    {
        auto guard = record_lock.acquire();
        ++lifecycle_record.rejected;
        if (error.reason == Reason::WouldBlock) {
            ++lifecycle_record.contention;
        }
        lifecycle_record.last_status = error.status;
    }

    [[nodiscard]] static FacilityRecord record() noexcept
    {
        auto guard = record_lock.acquire();
        auto result = lifecycle_record;
        result.ready = ready.load(std::memory_order_relaxed);
        result.accepting = accepting.load(std::memory_order_relaxed);
        return result;
    }

    template <typename> static void activate_runtime() noexcept {}
};

#endif

} // namespace solar::metrics

template <typename Architecture>
struct solar::builtin_traits<solar::metrics::Facility<Architecture>>
{
    static constexpr bool enabled = solar::metrics::enabled;
    static constexpr bool always_present = false;
    using Requirements = solar::TypeList<>;

    template <typename> static constexpr bool demanded = Architecture::demanded;
};

#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_METRICS)
template <typename Component, typename Architecture, typename AllComponents>
struct solar::generated_component_dependency<Component, solar::metrics::Facility<Architecture>,
                                             AllComponents>
    : std::bool_constant<!std::is_same_v<Component, solar::metrics::Facility<Architecture>> &&
                         solar::contains_v<Component, typename Architecture::ComponentTypes>>
{};
#endif
