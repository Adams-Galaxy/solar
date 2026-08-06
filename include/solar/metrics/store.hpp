#pragma once

#include <concepts>
#include <cstdint>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

#include "solar/core/spin_mutex.hpp"
#include "solar/core/status.hpp"
#include "solar/core/type_list.hpp"

namespace solar::metrics
{

template <typename... Metrics> struct Schema
{
    using Entries = TypeList<Metrics...>;
};
template <typename Metric> struct Sample
{
    typename Metric::Value value{};
    std::uint64_t revision{};
};
template <typename Metric> struct MetricSlot
{
    Sample<Metric> sample{};
    std::uint64_t count{};
    typename Metric::Value total{};
    std::optional<typename Metric::Value> minimum{};
    std::optional<typename Metric::Value> maximum{};
};
template <typename Metric> struct AggregateSummary
{
    std::uint64_t count{};
    typename Metric::Value total{};
    typename Metric::Value minimum{};
    typename Metric::Value maximum{};
};
template <typename SchemaT> class MetricStore;

/** Typed metric values only; sampling and export are adapter policy. */
template <typename... Metrics> class MetricStore<Schema<Metrics...>>
{
  public:
    [[nodiscard]] Result<void> initialize()
    {
        SpinGuard lock{mutex_};
        slots_ = {};
        return {};
    }
    template <typename Metric> [[nodiscard]] Result<void> set(typename Metric::Value value)
    {
        require_member<Metric>();
        SpinGuard lock{mutex_};
        auto& sample = std::get<MetricSlot<Metric>>(slots_).sample;
        sample.value = std::move(value);
        ++sample.revision;
        return {};
    }
    template <typename Metric>
    [[nodiscard]] Result<void> observe(typename Metric::Value value)
        requires std::is_arithmetic_v<typename Metric::Value>
    {
        require_member<Metric>();
        SpinGuard lock{mutex_};
        auto& slot = std::get<MetricSlot<Metric>>(slots_);
        slot.sample.value = value;
        ++slot.sample.revision;
        ++slot.count;
        slot.total += value;
        if (!slot.minimum || value < *slot.minimum) {
            slot.minimum = value;
        }
        if (!slot.maximum || value > *slot.maximum) {
            slot.maximum = value;
        }
        return {};
    }
    template <typename Metric> [[nodiscard]] Result<Sample<Metric>> get() const
    {
        require_member<Metric>();
        SpinGuard lock{mutex_};
        return std::get<MetricSlot<Metric>>(slots_).sample;
    }
    template <typename Metric>
    [[nodiscard]] Result<AggregateSummary<Metric>> summary() const
        requires std::is_arithmetic_v<typename Metric::Value>
    {
        require_member<Metric>();
        SpinGuard lock{mutex_};
        const auto& slot = std::get<MetricSlot<Metric>>(slots_);
        if (slot.count == 0) {
            return fail<solar::Error>({.status = Status::Empty});
        }
        return AggregateSummary<Metric>{.count = slot.count,
                                        .total = slot.total,
                                        .minimum = *slot.minimum,
                                        .maximum = *slot.maximum};
    }

  private:
    template <typename Metric> static consteval void require_member()
    {
        static_assert(contains_v<Metric, TypeList<Metrics...>>,
                      "SOLAR_METRICS_NOT_IN_SCHEMA: metric is absent from the closed schema");
    }
    std::tuple<MetricSlot<Metrics>...> slots_{};
    mutable SpinMutex mutex_{};
};

template <typename Application, typename SchemaT> struct StaticMetricStore
{
    inline static MetricStore<SchemaT> storage{};
    [[nodiscard]] static Result<void> initialize()
    {
        return storage.initialize();
    }
    template <typename Metric> [[nodiscard]] static auto set(typename Metric::Value value)
    {
        return storage.template set<Metric>(std::move(value));
    }
    template <typename Metric> [[nodiscard]] static auto get()
    {
        return storage.template get<Metric>();
    }
    template <typename Metric>
    [[nodiscard]] static auto observe(typename Metric::Value value)
    {
        return storage.template observe<Metric>(std::move(value));
    }
    template <typename Metric> [[nodiscard]] static auto summary()
    {
        return storage.template summary<Metric>();
    }
};

} // namespace solar::metrics
