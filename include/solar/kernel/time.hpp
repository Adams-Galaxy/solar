#pragma once

#include <chrono>
#include <cstdint>
#include <limits>
#include <ratio>

#include <zephyr/kernel.h>
#include <zephyr/sys/time_units.h>

#include "solar/core/time.hpp"

namespace solar::kernel
{

using Tick = std::int64_t;

using Duration = solar::Duration;
using Milliseconds = solar::Milliseconds;
using Microseconds = solar::Microseconds;
using Seconds = solar::Seconds;

struct SteadyClock
{
    using rep = Tick;
    using period = std::ratio<1, CONFIG_SYS_CLOCK_TICKS_PER_SEC>;
    using duration = std::chrono::duration<rep, period>;
    using time_point = std::chrono::time_point<SteadyClock>;

    static constexpr bool is_steady = true;

    [[nodiscard]] static time_point now() noexcept
    {
        return time_point{duration{k_uptime_ticks()}};
    }
};

using TickDuration = SteadyClock::duration;
using TimePoint = SteadyClock::time_point;

template <typename Rep, typename Period>
[[nodiscard]] constexpr Tick to_ticks_ceil(std::chrono::duration<Rep, Period> duration) noexcept
{
    if (duration <= std::chrono::duration<Rep, Period>::zero()) {
        return 0;
    }

    const auto nanoseconds = std::chrono::ceil<std::chrono::nanoseconds>(duration).count();
    const auto clamped = static_cast<std::uint64_t>(nanoseconds);
    const auto ticks = k_ns_to_ticks_ceil64(clamped);
    return ticks > static_cast<std::uint64_t>(std::numeric_limits<Tick>::max())
               ? std::numeric_limits<Tick>::max()
               : static_cast<Tick>(ticks);
}

[[nodiscard]] constexpr TickDuration from_ticks(Tick ticks) noexcept
{
    return TickDuration{ticks};
}

[[nodiscard]] inline Tick now_ticks() noexcept
{
    return static_cast<Tick>(k_uptime_ticks());
}

[[nodiscard]] inline TimePoint now() noexcept
{
    return SteadyClock::now();
}

class Timeout
{
  public:
    [[nodiscard]] static constexpr Timeout no_wait() noexcept
    {
        return Timeout{K_NO_WAIT};
    }

    [[nodiscard]] static constexpr Timeout forever() noexcept
    {
        return Timeout{K_FOREVER};
    }

    [[nodiscard]] static constexpr Timeout after_ticks(Tick ticks) noexcept
    {
        if (ticks <= 0) {
            return no_wait();
        }
        constexpr Tick max_ticks = static_cast<Tick>(K_TICK_MAX);
        const auto bounded = ticks > max_ticks ? max_ticks : ticks;
        return Timeout{K_TICKS(static_cast<k_ticks_t>(bounded))};
    }

    template <typename Rep, typename Period>
    [[nodiscard]] static constexpr Timeout
    after(std::chrono::duration<Rep, Period> duration) noexcept
    {
        return after_ticks(to_ticks_ceil(duration));
    }

    [[nodiscard]] static constexpr Timeout from_native(k_timeout_t timeout) noexcept
    {
        return Timeout{timeout};
    }

    [[nodiscard]] constexpr bool is_no_wait() const noexcept
    {
        return K_TIMEOUT_EQ(value_, K_NO_WAIT);
    }

    [[nodiscard]] constexpr bool is_forever() const noexcept
    {
        return K_TIMEOUT_EQ(value_, K_FOREVER);
    }

    [[nodiscard]] constexpr k_timeout_t native_handle() const noexcept
    {
        return value_;
    }

  private:
    explicit constexpr Timeout(k_timeout_t value) noexcept : value_(value) {}

    k_timeout_t value_ = K_NO_WAIT;
};

} // namespace solar::kernel
