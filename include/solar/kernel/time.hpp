#pragma once

#include <chrono>
#include <cstdint>

#include <zephyr/kernel.h>

#include "solar/core/time.hpp"

namespace solar::kernel
{

using Tick = std::int64_t;
inline constexpr Tick WaitForever = -1;

using Duration = solar::Duration;
using Milliseconds = solar::Milliseconds;
using Microseconds = solar::Microseconds;
using Seconds = solar::Seconds;

constexpr Tick to_ticks(Tick ticks);

template <class Rep, class Period>
constexpr Tick to_ticks(std::chrono::duration<Rep, Period> duration);

inline k_timeout_t to_timeout(Tick ticks);

class Timeout
{
public:
    Timeout() = default;

    static Timeout no_wait()
    {
        return Timeout{0};
    }

    static Timeout forever()
    {
        return Timeout{WaitForever};
    }

    static Timeout after_ticks(Tick ticks)
    {
        return Timeout{ticks};
    }

    static Timeout after_ms(Tick milliseconds)
    {
        return Timeout{milliseconds};
    }

    template <class Rep, class Period>
    static Timeout after(std::chrono::duration<Rep, Period> duration)
    {
        return Timeout{to_ticks(duration)};
    }

    Tick ticks() const
    {
        return ticks_;
    }

    bool is_forever() const
    {
        return ticks_ == WaitForever;
    }

    k_timeout_t native() const
    {
        return to_timeout(ticks_);
    }

private:
    explicit constexpr Timeout(Tick ticks) : ticks_(ticks) {}

    Tick ticks_ = 0;
};

/**
 * @brief Convert a chrono duration or native tick count into kernel ticks.
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

inline k_timeout_t to_timeout(Timeout timeout)
{
    return timeout.native();
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

class TimePoint
{
public:
    TimePoint() = default;
    explicit constexpr TimePoint(Tick ticks) : ticks_(ticks) {}

    static TimePoint now()
    {
        return TimePoint{now_ticks()};
    }

    Tick ticks() const
    {
        return ticks_;
    }

private:
    Tick ticks_ = 0;
};

} // namespace solar::kernel
