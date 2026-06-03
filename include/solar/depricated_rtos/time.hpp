#pragma once

#include <chrono>

#include <zephyr/kernel.h>

#include "solar/core/time.hpp"

namespace solar::kernel
{

using Tick = std::int64_t;
inline constexpr Tick WaitForever = -1;

using Milliseconds = solar::Milliseconds;
using Microseconds = solar::Microseconds;
using Seconds = solar::Seconds;

/**
 * @brief Convert a chrono duration or native tick count into Kernel ticks.
 */
constexpr Tick to_ticks(Tick ticks)
{
    return ticks;
}

template <class Rep, class Period>
constexpr Tick to_ticks(std::chrono::duration<Rep, Period> duration)
{
    return static_cast<Tick>(std::chrono::duration_cast<Milliseconds>(duration).count());
}

constexpr Milliseconds to_milliseconds(Tick ticks)
{
    return Milliseconds{ticks < 0 ? 0 : ticks};
}

inline k_timeout_t to_timeout(Tick ticks)
{
    if (ticks == WaitForever)
    {
        return K_FOREVER;
    }
    if (ticks <= 0)
    {
        return K_NO_WAIT;
    }
    return K_MSEC(ticks);
}

inline Tick now_ticks()
{
    return static_cast<Tick>(k_uptime_get());
}

inline Milliseconds now()
{
    return to_milliseconds(now_ticks());
}

inline Tick ticks_since(Tick start)
{
    return now_ticks() - start;
}

} // namespace solar::kernel
