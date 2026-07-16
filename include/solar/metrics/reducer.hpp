#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "solar/metrics/units.hpp"

namespace solar::metrics
{

struct Counter
{};

struct Gauge
{};

struct Last
{};

struct Minimum
{};

struct Maximum
{};

struct Summary
{};

template <std::size_t Size> struct WindowMean
{
    static_assert(Size > 0,
                  "SOLAR_DIAGNOSTIC_METRIC_WINDOW_ZERO: window capacity must be positive");
    static constexpr std::size_t size = Size;
};

template <std::size_t Numerator, std::size_t Denominator> struct Ema
{
    static_assert(Denominator > 0,
                  "SOLAR_DIAGNOSTIC_METRIC_EMA_DENOMINATOR: EMA denominator must be positive");
    static_assert(Numerator <= Denominator,
                  "SOLAR_DIAGNOSTIC_METRIC_EMA_RATIO: EMA ratio must be in [0, 1]");
    static constexpr std::size_t numerator = Numerator;
    static constexpr std::size_t denominator = Denominator;
};

template <auto... Boundaries> struct Histogram
{
    static constexpr std::array boundaries{Boundaries...};
    static constexpr std::size_t bucket_count = sizeof...(Boundaries) + 1;
    static constexpr bool ordered = [] {
        for (std::size_t index = 1; index < boundaries.size(); ++index) {
            if (!(boundaries[index - 1] < boundaries[index])) {
                return false;
            }
        }
        return true;
    }();

    static_assert(ordered, "SOLAR_DIAGNOSTIC_METRIC_HISTOGRAM_ORDER: histogram boundaries must be "
                           "strictly increasing");
};

template <typename Reducer> struct Distribution
{
    using ReducerType = Reducer;
};

template <typename Reducer = Summary> struct Timer
{
    using ReducerType = Reducer;
};

namespace view
{
struct Value;
struct Count;
struct Sum;
struct Minimum;
struct Maximum;
struct Mean;
struct Last;
template <std::size_t Index> struct Bucket;
} // namespace view

namespace detail
{

template <typename Value>
using accumulator_t =
    std::conditional_t<std::is_floating_point_v<Value>, long double,
                       std::conditional_t<std::is_signed_v<Value>, std::int64_t, std::uint64_t>>;

template <typename Instrument> struct InstrumentTraits
{
    static constexpr bool valid = false;
};

template <> struct InstrumentTraits<Counter>
{
    static constexpr bool valid = true;
    static constexpr InstrumentKind kind = InstrumentKind::Counter;
    static constexpr bool counter = true;
    static constexpr bool gauge = false;
    static constexpr bool distribution = false;
    static constexpr bool timer = false;
    using Reducer = void;
};

template <> struct InstrumentTraits<Gauge>
{
    static constexpr bool valid = true;
    static constexpr InstrumentKind kind = InstrumentKind::Gauge;
    static constexpr bool counter = false;
    static constexpr bool gauge = true;
    static constexpr bool distribution = false;
    static constexpr bool timer = false;
    using Reducer = void;
};

template <typename ReducerT> struct InstrumentTraits<Distribution<ReducerT>>
{
    static constexpr bool valid = true;
    static constexpr InstrumentKind kind = InstrumentKind::Distribution;
    static constexpr bool counter = false;
    static constexpr bool gauge = false;
    static constexpr bool distribution = true;
    static constexpr bool timer = false;
    using Reducer = ReducerT;
};

template <typename ReducerT> struct InstrumentTraits<Timer<ReducerT>>
{
    static constexpr bool valid = true;
    static constexpr InstrumentKind kind = InstrumentKind::Timer;
    static constexpr bool counter = false;
    static constexpr bool gauge = false;
    static constexpr bool distribution = true;
    static constexpr bool timer = true;
    using Reducer = ReducerT;
};

template <typename Reducer> struct ReducerTraits
{
    static constexpr bool valid = false;
};

template <> struct ReducerTraits<Last>
{
    static constexpr bool valid = true;
    static constexpr bool bounded_isr = true;
    static constexpr std::size_t views = 2;
};

template <> struct ReducerTraits<Minimum> : ReducerTraits<Last>
{};

template <> struct ReducerTraits<Maximum> : ReducerTraits<Last>
{};

template <> struct ReducerTraits<Summary>
{
    static constexpr bool valid = true;
    static constexpr bool bounded_isr = true;
    static constexpr std::size_t views = 5;
};

template <std::size_t Size> struct ReducerTraits<WindowMean<Size>>
{
    static constexpr bool valid = true;
    static constexpr bool bounded_isr = Size <= 16;
    static constexpr std::size_t views = 3;
    static constexpr std::size_t capacity = Size;
};

template <std::size_t Numerator, std::size_t Denominator>
struct ReducerTraits<Ema<Numerator, Denominator>>
{
    static constexpr bool valid = true;
    static constexpr bool bounded_isr = false;
    static constexpr std::size_t views = 2;
};

template <auto... Boundaries> struct ReducerTraits<Histogram<Boundaries...>>
{
    static constexpr bool valid = Histogram<Boundaries...>::ordered;
    static constexpr bool bounded_isr = sizeof...(Boundaries) <= 8;
    static constexpr std::size_t views = sizeof...(Boundaries) + 3;
    static constexpr std::size_t boundaries = sizeof...(Boundaries);
};

template <typename Value, typename Reducer> struct ReducerState;

template <typename Value> struct ReducerState<Value, Last>
{
    Value value{};
    std::uint64_t count{};
    bool initialized{};
};

template <typename Value> struct ReducerState<Value, Minimum> : ReducerState<Value, Last>
{};

template <typename Value> struct ReducerState<Value, Maximum> : ReducerState<Value, Last>
{};

template <typename Value> struct ReducerState<Value, Summary>
{
    using Sum = accumulator_t<Value>;
    std::uint64_t count{};
    Sum sum{};
    Value minimum{};
    Value maximum{};
    bool initialized{};
};

template <typename Value, std::size_t Size> struct ReducerState<Value, WindowMean<Size>>
{
    std::array<Value, Size> values{};
    accumulator_t<Value> sum{};
    Value latest{};
    std::size_t next{};
    std::size_t count{};
};

template <typename Value, std::size_t Numerator, std::size_t Denominator>
struct ReducerState<Value, Ema<Numerator, Denominator>>
{
    long double mean{};
    Value latest{};
    std::uint64_t count{};
    bool initialized{};
};

template <typename Value, auto... Boundaries> struct ReducerState<Value, Histogram<Boundaries...>>
{
    using Sum = accumulator_t<Value>;
    std::array<std::uint64_t, sizeof...(Boundaries) + 1> buckets{};
    std::uint64_t count{};
    Sum sum{};
};

template <typename Value, typename Reducer> struct ReducerReading;

template <typename Value> struct ReducerReading<Value, Last>
{
    using type = PointReading<Value>;
};

template <typename Value> struct ReducerReading<Value, Minimum> : ReducerReading<Value, Last>
{};

template <typename Value> struct ReducerReading<Value, Maximum> : ReducerReading<Value, Last>
{};

template <typename Value> struct ReducerReading<Value, Summary>
{
    using type = SummaryReading<Value, accumulator_t<Value>>;
};

template <typename Value, std::size_t Size> struct ReducerReading<Value, WindowMean<Size>>
{
    using type = MeanReading<Value>;
};

template <typename Value, std::size_t Numerator, std::size_t Denominator>
struct ReducerReading<Value, Ema<Numerator, Denominator>>
{
    using type = MeanReading<Value>;
};

template <typename Value, auto... Boundaries> struct ReducerReading<Value, Histogram<Boundaries...>>
{
    using type = HistogramReading<Value, accumulator_t<Value>, sizeof...(Boundaries) + 1>;
};

} // namespace detail

} // namespace solar::metrics
