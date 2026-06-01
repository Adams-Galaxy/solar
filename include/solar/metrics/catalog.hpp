#pragma once

#include <concepts>
#include <cstdint>
#include <string_view>

#include "solar/core/fixed_string.hpp"
#include "solar/core/type_list.hpp"

namespace solar::metrics
{

enum class Kind : std::uint8_t
{
    Counter,
    Sample,
    Timer,
};

/**
 * @brief Default metric unit tag for values without a domain unit.
 */
struct Unitless
{
    using Name = solar::Name<"">;
};

/**
 * @brief Minimal user-defined unit concept.
 *
 * Units are labels/type tags, not a conversion system. A user unit only needs a
 * stable `Name` so snapshots and Remote can display the value meaning.
 */
template <typename T>
concept Unit = requires {
    typename T::Name;
    { T::Name::c_str() } -> std::convertible_to<const char *>;
};

constexpr std::uint32_t fnv1a32(std::string_view text)
{
    std::uint32_t hash = 2166136261UL;
    for (const char ch : text)
    {
        hash ^= static_cast<std::uint8_t>(ch);
        hash *= 16777619UL;
    }
    return hash;
}

/**
 * @brief Type-level list of metrics owned by a component or system.
 *
 * Entries describe metrics owned/provided by a component. Runtime storage is
 * allocated by the metrics facility from these static descriptors, not by a
 * runtime registry.
 */
template <typename... Entries>
struct List : TypeList<Entries...>
{
    using SolarCatalogKind = List;
};

/**
 * @brief Monotonic accumulated metric.
 *
 * Counters are updated through `Facility::inc`/`add` and snapshot to their
 * current accumulated value.
 */
template <typename NameT, typename ValueT = std::uint64_t, Unit UnitT = Unitless>
struct Counter
{
    using Name = NameT;
    using Value = ValueT;
    using Unit = UnitT;
    static constexpr Kind kind = Kind::Counter;
    static constexpr std::uint32_t id = fnv1a32(NameT::view());
};

/**
 * @brief Observed-value metric whose stored meaning is defined by PolicyT.
 *
 * The policy decides what the primary value means: latest sample, max, moving
 * average, EMA, or a project-specific reducer.
 */
template <typename NameT, typename ValueT, Unit UnitT = Unitless, typename PolicyT = void>
struct Sample
{
    using Name = NameT;
    using Value = ValueT;
    using Unit = UnitT;
    using Policy = PolicyT;
    static constexpr Kind kind = Kind::Sample;
    static constexpr std::uint32_t id = fnv1a32(NameT::view());
};

/**
 * @brief Ergonomic latest-value metric. Internally this is a sample with the
 * default `Last` policy supplied by the runtime storage layer.
 */
template <typename NameT, typename ValueT, Unit UnitT = Unitless>
struct Gauge
{
    using Name = NameT;
    using Value = ValueT;
    using Unit = UnitT;
    using Policy = void;
    static constexpr Kind kind = Kind::Sample;
    static constexpr std::uint32_t id = fnv1a32(NameT::view());
};

/**
 * @brief Duration metric recorded in microseconds with scoped timing helpers.
 *
 * The optional unit tag defaults to unitless so projects can choose their own
 * label, commonly `Name<"us">`.
 */
template <typename NameT, typename PolicyT = void, Unit UnitT = Unitless>
struct Timer
{
    using Name = NameT;
    using Value = std::uint64_t;
    using Unit = UnitT;
    using Policy = PolicyT;
    static constexpr Kind kind = Kind::Timer;
    static constexpr std::uint32_t id = fnv1a32(NameT::view());
};

/**
 * @brief Backward-friendly descriptor spelling; use Gauge/Counter/Sample/Timer
 * for new metrics.
 */
template <typename NameT, typename ValueT = std::uint64_t, Unit UnitT = Unitless>
using Metric = Gauge<NameT, ValueT, UnitT>;

} // namespace solar::metrics
