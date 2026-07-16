#pragma once

#include <cstddef>
#include <cstdint>

#include "solar/core/time.hpp"
#include "solar/core/type_list.hpp"

namespace solar::execution
{

template <std::size_t Bytes> struct StackSize
{
    static_assert(Bytes > 0,
                  "SOLAR_DIAGNOSTIC_EXECUTION_ZERO_STACK: execution stack must be non-zero");
    static constexpr std::size_t value = Bytes;
};

template <std::uint32_t Level> struct Priority
{
    static constexpr std::uint32_t value = Level;
};

template <DurationValue Value> struct StopTimeout
{
    static_assert(
        Value.nanoseconds >= 0,
        "SOLAR_DIAGNOSTIC_EXECUTION_NEGATIVE_STOP_TIMEOUT: stop timeout cannot be negative");
    static constexpr DurationValue value = Value;
};

template <DurationValue Value> struct WorkTimeout
{
    static_assert(
        Value.nanoseconds >= 0,
        "SOLAR_DIAGNOSTIC_EXECUTION_NEGATIVE_WORK_TIMEOUT: work timeout cannot be negative");
    static constexpr DurationValue value = Value;
};

template <bool Enabled> struct AbortOnTimeout
{
    static constexpr bool value = Enabled;
};

struct YieldBetweenItems
{};

struct NoYieldBetweenItems
{};

struct DefaultTarget
{};

struct SystemWorkQueue
{};

struct NativeCoalescing
{};

template <std::size_t Capacity> struct Counted
{
    static_assert(
        Capacity > 0,
        "SOLAR_DIAGNOSTIC_EXECUTION_ZERO_COUNTED_CAPACITY: Counted capacity must be non-zero");
    static constexpr std::size_t capacity = Capacity;
};

template <typename... Components> struct DependsOn
{
    using ComponentsList = TypeList<Components...>;
};

namespace stop
{
struct Drain
{};

struct CancelPending
{};
} // namespace stop

namespace failure
{
struct RecordAndContinue
{};

struct Suspend
{};
} // namespace failure

namespace periodic
{
struct FixedRate
{};

struct FixedDelay
{};
} // namespace periodic

namespace overrun
{
struct Skip
{};
} // namespace overrun

struct StartAfterPeriod
{};

struct StartImmediately
{};

template <DurationValue Value> struct Deadline
{
    static_assert(Value.positive(),
                  "SOLAR_DIAGNOSTIC_EXECUTION_NONPOSITIVE_DEADLINE: deadline must be positive");
    static constexpr DurationValue value = Value;
};

namespace poll
{
struct OneShot
{};

struct AutoRearm
{};
} // namespace poll

template <typename... Policies> struct Service
{
    using PoliciesList = TypeList<Policies...>;
};

} // namespace solar::execution
