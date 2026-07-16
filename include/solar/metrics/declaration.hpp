#pragma once

#include <concepts>
#include <type_traits>

#include "solar/metrics/policy.hpp"

namespace solar::metrics
{

template <typename Metric>
concept MetricDeclaration = requires {
    typename Metric::Value;
    typename Metric::Instrument;
    typename Metric::Unit;
    { Metric::descriptor } -> std::convertible_to<Descriptor>;
};

template <typename MetricT>
concept Metric = MetricDeclaration<MetricT> && Unit<typename MetricT::Unit> &&
                 detail::InstrumentTraits<typename MetricT::Instrument>::valid &&
                 (std::is_arithmetic_v<typename MetricT::Value> ||
                  std::is_same_v<typename MetricT::Value, bool>) &&
                 std::is_trivially_copyable_v<typename MetricT::Value> &&
                 std::is_trivially_destructible_v<typename MetricT::Value>;

namespace detail
{

template <typename Metric, typename Instrument = typename Metric::Instrument> struct ReadingFor;

template <typename Metric> struct ReadingFor<Metric, Counter>
{
    using type = CounterReading<typename Metric::Value>;
};

template <typename Metric> struct ReadingFor<Metric, Gauge>
{
    using type = GaugeReading<typename Metric::Value>;
};

template <typename Metric, typename Reducer> struct ReadingFor<Metric, Distribution<Reducer>>
{
    using type = typename ReducerReading<typename Metric::Value, Reducer>::type;
};

template <typename Metric, typename Reducer>
struct ReadingFor<Metric, Timer<Reducer>> : ReadingFor<Metric, Distribution<Reducer>>
{};

} // namespace detail

template <Metric MetricT> using Reading = typename detail::ReadingFor<MetricT>::type;

} // namespace solar::metrics
