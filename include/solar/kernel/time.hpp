#pragma once

#include <chrono>
#include <concepts>
#include <cstdint>
#include <limits>
#include <ratio>

#include <zephyr/kernel.h>
#include <zephyr/sys/time_units.h>

#include "solar/core/status.hpp"
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

/** Signed scheduler-deadline offset measured in hardware cycles. */
class CycleDuration
{
  public:
    [[nodiscard]] static constexpr Result<CycleDuration> from_cycles(std::uint64_t cycles) noexcept
    {
        if (cycles > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
            return fail<Error>({.status = Status::Invalid});
        }
        return CycleDuration{static_cast<std::int32_t>(cycles)};
    }

    [[nodiscard]] constexpr std::int32_t count() const noexcept
    {
        return cycles_;
    }

  private:
    explicit constexpr CycleDuration(std::int32_t cycles) noexcept : cycles_(cycles) {}
    std::int32_t cycles_{};
};

/** Absolute 32-bit hardware-cycle timestamp used by Zephyr deadline scheduling. */
class CycleTimePoint
{
  public:
    [[nodiscard]] static constexpr CycleTimePoint from_cycles(std::uint32_t cycles) noexcept
    {
        return CycleTimePoint{cycles};
    }

    [[nodiscard]] static CycleTimePoint now() noexcept
    {
        return from_cycles(k_cycle_get_32());
    }

    [[nodiscard]] constexpr std::uint32_t count() const noexcept
    {
        return cycles_;
    }

  private:
    explicit constexpr CycleTimePoint(std::uint32_t cycles) noexcept : cycles_(cycles) {}
    std::uint32_t cycles_{};
};

template <typename Rep, typename Period>
[[nodiscard]] constexpr Tick to_ticks_ceil(std::chrono::duration<Rep, Period> duration) noexcept
{
    if (duration <= std::chrono::duration<Rep, Period>::zero()) {
        return 0;
    }

    constexpr Tick maximum = std::numeric_limits<Tick>::max();

    if constexpr (std::integral<Rep>) {
        using Wide = unsigned __int128;
        const auto count = static_cast<Wide>(duration.count());
        const auto denominator = static_cast<Wide>(Period::den);
        const auto factor =
            static_cast<Wide>(Period::num) * static_cast<Wide>(CONFIG_SYS_CLOCK_TICKS_PER_SEC);
        const auto saturation_limit = static_cast<Wide>(maximum) * denominator;
        if (factor != 0 && count > saturation_limit / factor) {
            return maximum;
        }
        const auto numerator = count * factor;
        const auto ticks = (numerator + denominator - 1U) / denominator;
        return ticks > static_cast<Wide>(maximum) ? maximum : static_cast<Tick>(ticks);
    } else {
        const long double ticks = static_cast<long double>(duration.count()) *
                                  static_cast<long double>(Period::num) *
                                  static_cast<long double>(CONFIG_SYS_CLOCK_TICKS_PER_SEC) /
                                  static_cast<long double>(Period::den);
        if (ticks >= static_cast<long double>(maximum)) {
            return maximum;
        }
        const auto truncated = static_cast<Tick>(ticks);
        return static_cast<long double>(truncated) == ticks ? truncated : truncated + 1;
    }
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
