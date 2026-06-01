#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "solar/core.hpp"
#include "solar/metrics/policy.hpp"
#include "solar/metrics/value.hpp"
#include "solar/rtos/critical_section.hpp"
#include "solar/rtos/time.hpp"

namespace solar::metrics
{

namespace detail
{

template <typename MetricT>
using EffectivePolicyT = std::conditional_t<std::is_void_v<typename MetricT::Policy>, Last, typename MetricT::Policy>;

template <typename MetricT>
class CounterStorage
{
public:
    using ValueType = typename MetricT::Value;
    using Output = ValueType;

    void reset()
    {
        value_ = {};
    }

    void add(ValueType amount)
    {
        value_ += amount;
    }

    void set(ValueType value)
    {
        value_ = value;
    }

    Output value() const
    {
        return value_;
    }

private:
    ValueType value_{};
};

template <typename MetricT>
class SampleStorage
{
public:
    using ValueType = typename MetricT::Value;
    using Policy = EffectivePolicyT<MetricT>;
    using PolicyStorage = PolicyStorageT<Policy, ValueType>;
    using Output = decltype(std::declval<PolicyStorage const &>().value());

    static_assert(SamplePolicy<Policy, ValueType>, "Metric sample policy must expose Storage<ValueT> with reset/observe/value");

    void reset()
    {
        policy_.reset();
    }

    void observe(ValueType value)
    {
        policy_.observe(value);
    }

    Output value() const
    {
        return policy_.value();
    }

    PolicyStorage const &policy() const
    {
        return policy_;
    }

private:
    PolicyStorage policy_{};
};

template <typename MetricT>
using StorageForT = std::conditional_t<MetricT::kind == Kind::Counter, CounterStorage<MetricT>, SampleStorage<MetricT>>;

} // namespace detail

/**
 * @brief Passive metric state facility.
 *
 * The facility stores metrics by descriptor type rather than by runtime name.
 * It has no sinks and no worker thread; Remote or a future exporter service can
 * pull snapshots from the static catalog when it wants to publish metrics.
 */
template <typename NameT = solar::Name<"metrics">>
class Facility
{
public:
    using Name = NameT;

    template <typename ContextT>
    /**
     * @brief Reset the owning system's collected metric catalog.
     */
    static Status init(ContextT &)
    {
        reset_catalog<typename ContextT::SystemType::MetricsCatalog>();
        return Status::Ok;
    }

    static Status init()
    {
        return Status::Ok;
    }

    static Status start()
    {
        return Status::Ok;
    }

    template <typename ContextT>
    static Status start(ContextT &)
    {
        return start();
    }

    template <typename MetricT>
    /**
     * @brief Increment a counter metric.
     */
    static void inc(typename MetricT::Value amount = static_cast<typename MetricT::Value>(1))
    {
        static_assert(MetricT::kind == Kind::Counter, "inc() can only be used with Counter metrics");
        rtos::CriticalSection guard;
        storage<MetricT>().add(amount);
    }

    template <typename MetricT>
    static void add(typename MetricT::Value amount)
    {
        inc<MetricT>(amount);
    }

    template <typename MetricT>
    /**
     * @brief Set a metric's primary value.
     *
     * For counters this writes the accumulated value. For samples/gauges this is
     * equivalent to observing a new sample.
     */
    static void set(typename MetricT::Value value)
    {
        rtos::CriticalSection guard;
        if constexpr (MetricT::kind == Kind::Counter)
        {
            storage<MetricT>().set(value);
        }
        else
        {
            storage<MetricT>().observe(value);
        }
    }

    template <typename MetricT>
    /**
     * @brief Observe a sample/gauge/timer value.
     */
    static void observe(typename MetricT::Value value)
    {
        static_assert(MetricT::kind != Kind::Counter, "observe() is for Sample/Gauge/Timer metrics; use inc/add for counters");
        rtos::CriticalSection guard;
        storage<MetricT>().observe(value);
    }

    template <typename MetricT, class Rep, class Period>
    /**
     * @brief Record a timer value from any `std::chrono` duration.
     */
    static void record(std::chrono::duration<Rep, Period> duration)
    {
        static_assert(MetricT::kind == Kind::Timer, "record(duration) can only be used with Timer metrics");
        const auto micros = std::chrono::duration_cast<solar::Microseconds>(duration).count();
        record<MetricT>(static_cast<typename MetricT::Value>(micros));
    }

    template <typename MetricT>
    /**
     * @brief Record a timer value already expressed in microseconds.
     */
    static void record(typename MetricT::Value microseconds)
    {
        static_assert(MetricT::kind == Kind::Timer, "record() can only be used with Timer metrics");
        observe<MetricT>(microseconds);
    }

    template <typename MetricT>
    /**
     * @brief RAII timer that records elapsed time on destruction.
     */
    class ScopedTimer
    {
    public:
        ScopedTimer() : start_(rtos::now()) {}

        ScopedTimer(const ScopedTimer &) = delete;
        ScopedTimer &operator=(const ScopedTimer &) = delete;

        ScopedTimer(ScopedTimer &&other) noexcept : start_(other.start_), active_(other.active_)
        {
            other.active_ = false;
        }

        ScopedTimer &operator=(ScopedTimer &&other) noexcept
        {
            if (this != &other)
            {
                finish();
                start_ = other.start_;
                active_ = other.active_;
                other.active_ = false;
            }
            return *this;
        }

        ~ScopedTimer()
        {
            finish();
        }

        void finish()
        {
            if (!active_)
            {
                return;
            }
            active_ = false;
            const auto elapsed = rtos::now() - start_;
            Facility::template record<MetricT>(elapsed);
        }

    private:
        solar::Milliseconds start_{};
        bool active_ = true;
    };

    template <typename MetricT>
    /**
     * @brief Start a scoped timer for a timer descriptor.
     */
    static ScopedTimer<MetricT> scoped()
    {
        static_assert(MetricT::kind == Kind::Timer, "scoped() can only be used with Timer metrics");
        return ScopedTimer<MetricT>{};
    }

    template <typename MetricT>
    /**
     * @brief Return the primary value produced by a metric's storage policy.
     */
    static auto get() -> Result<typename detail::StorageForT<MetricT>::Output>
    {
        rtos::CriticalSection guard;
        return storage<MetricT>().value();
    }

    template <typename MetricT>
    /**
     * @brief Return a uniform one-value snapshot for a metric descriptor.
     */
    static Snapshot snapshot()
    {
        rtos::CriticalSection guard;
        return make_snapshot<MetricT>();
    }

    template <typename CatalogT>
    /**
     * @brief Reset every metric in a static catalog.
     */
    static void reset_catalog()
    {
        reset_list<CatalogT>();
    }

    template <typename CatalogT>
    /**
     * @brief Copy snapshots from a static catalog into caller-owned storage.
     */
    static std::size_t snapshots(Snapshot *out, std::size_t max)
    {
        return snapshot_list<CatalogT>(out, max);
    }

    template <typename MetricsT, typename GroupT>
    struct Group
    {
        using Metrics = typename GroupT::Metrics;

        template <typename... Args>
        static decltype(auto) observe(Args &&...args)
        {
            return GroupT::template observe<MetricsT>(static_cast<Args &&>(args)...);
        }

        template <typename... Args>
        static decltype(auto) inc(Args &&...args)
        {
            return GroupT::template inc<MetricsT>(static_cast<Args &&>(args)...);
        }

        template <typename... Args>
        static decltype(auto) record(Args &&...args)
        {
            return GroupT::template record<MetricsT>(static_cast<Args &&>(args)...);
        }
    };

private:
    template <typename MetricT>
    static detail::StorageForT<MetricT> &storage()
    {
        static detail::StorageForT<MetricT> instance{};
        return instance;
    }

    template <typename MetricT>
    static Snapshot make_snapshot()
    {
        const auto value = storage<MetricT>().value();
        return Snapshot{
            .id = MetricT::id,
            .name = MetricT::Name::c_str(),
            .kind = MetricT::kind,
            .unit = MetricT::Unit::Name::c_str(),
            .value = make_value(value),
        };
    }

    template <typename ListT>
    static void reset_list()
    {
        reset_list_impl(static_cast<ListT *>(nullptr));
    }

    template <typename... MetricTypes>
    static void reset_list_impl(List<MetricTypes...> *)
    {
        rtos::CriticalSection guard;
        (storage<MetricTypes>().reset(), ...);
    }

    template <typename ListT>
    static std::size_t snapshot_list(Snapshot *out, std::size_t max)
    {
        return snapshot_list_impl(static_cast<ListT *>(nullptr), out, max);
    }

    template <typename... MetricTypes>
    static std::size_t snapshot_list_impl(List<MetricTypes...> *, Snapshot *out, std::size_t max)
    {
        if (out == nullptr)
        {
            return 0;
        }

        std::size_t index = 0;
        ((index < max ? out[index++] = snapshot<MetricTypes>() : Snapshot{}), ...);
        return index;
    }
};

} // namespace solar::metrics
