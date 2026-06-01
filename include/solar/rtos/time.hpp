#pragma once

#include <chrono>

#include "solar/core/time.hpp"
#include "low_level/rtos/time.hpp"

namespace solar::rtos
{

using Tick = low_level::rtos::Tick;
inline constexpr Tick WaitForever = low_level::rtos::WaitForever;

using Milliseconds = solar::Milliseconds;
using Microseconds = solar::Microseconds;
using Seconds = solar::Seconds;

/**
 * @brief Convert a chrono duration or native tick count into RTOS ticks.
 */
constexpr Tick to_ticks(Tick ticks)
{
    return low_level::rtos::to_ticks(ticks);
}

template <class Rep, class Period>
constexpr Tick to_ticks(std::chrono::duration<Rep, Period> duration)
{
    return low_level::rtos::to_ticks(duration);
}

constexpr Milliseconds to_milliseconds(Tick ticks)
{
    return low_level::rtos::to_milliseconds(ticks);
}

inline Tick now_ticks()
{
    return low_level::rtos::now_ticks();
}

inline Milliseconds now()
{
    return to_milliseconds(now_ticks());
}

inline Tick ticks_since(Tick start)
{
    return now_ticks() - start;
}

} // namespace solar::rtos
