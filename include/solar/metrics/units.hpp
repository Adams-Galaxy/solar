#pragma once

#include <concepts>
#include <ratio>

#include "solar/core/fixed_string.hpp"
#include "solar/metrics/types.hpp"

namespace solar::metrics
{

namespace dimension
{
struct Dimensionless;
struct Count;
struct Information;
struct Time;
struct Frequency;
struct Voltage;
struct Current;
struct Angle;
} // namespace dimension

template <typename UnitT>
concept Unit = requires {
    typename UnitT::Dimension;
    typename UnitT::Ratio;
    requires UnitT::Ratio::num > 0;
    requires UnitT::Ratio::den > 0;
    { UnitT::descriptor } -> std::convertible_to<UnitDescriptor>;
};

namespace units
{

template <typename DimensionT, typename RatioT, FixedString Symbol, FixedString Name> struct Basic
{
    using Dimension = DimensionT;
    using Ratio = RatioT;
    static constexpr UnitDescriptor descriptor{.symbol = Symbol.view(), .name = Name.view()};
};

using Unitless = Basic<dimension::Dimensionless, std::ratio<1>, "", "unitless">;
using Count = Basic<dimension::Count, std::ratio<1>, "count", "count">;
using Items = Basic<dimension::Count, std::ratio<1>, "items", "items">;
using Frames = Basic<dimension::Count, std::ratio<1>, "frames", "frames">;
using Packets = Basic<dimension::Count, std::ratio<1>, "packets", "packets">;
using Bits = Basic<dimension::Information, std::ratio<1>, "bit", "bits">;
using Bytes = Basic<dimension::Information, std::ratio<8>, "B", "bytes">;
using Nanoseconds = Basic<dimension::Time, std::nano, "ns", "nanoseconds">;
using Microseconds = Basic<dimension::Time, std::micro, "us", "microseconds">;
using Milliseconds = Basic<dimension::Time, std::milli, "ms", "milliseconds">;
using Seconds = Basic<dimension::Time, std::ratio<1>, "s", "seconds">;
using Hertz = Basic<dimension::Frequency, std::ratio<1>, "Hz", "hertz">;
using Percent = Basic<dimension::Dimensionless, std::ratio<1, 100>, "%", "percent">;
using Ratio = Basic<dimension::Dimensionless, std::ratio<1>, "ratio", "ratio">;
using Volts = Basic<dimension::Voltage, std::ratio<1>, "V", "volts">;
using Amperes = Basic<dimension::Current, std::ratio<1>, "A", "amperes">;
using Degrees = Basic<dimension::Angle, std::ratio<1>, "deg", "degrees">;
using Radians = Basic<dimension::Angle, std::ratio<1>, "rad", "radians">;

} // namespace units

template <Unit To, Unit From, typename Value>
    requires std::same_as<typename To::Dimension, typename From::Dimension>
[[nodiscard]] constexpr long double convert_unit(Value value) noexcept
{
    using Scale = std::ratio_divide<typename From::Ratio, typename To::Ratio>;
    return static_cast<long double>(value) * static_cast<long double>(Scale::num) /
           static_cast<long double>(Scale::den);
}

} // namespace solar::metrics
