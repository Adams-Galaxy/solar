#pragma once

#include <compare>

#include <zephyr/sys/clock.h>

#include "solar/kernel/time.hpp"

namespace solar::kernel
{

class Deadline
{
  public:
    Deadline() noexcept : value_(sys_timepoint_calc(K_NO_WAIT)) {}

    [[nodiscard]] static Deadline after(Timeout timeout) noexcept
    {
        return Deadline{sys_timepoint_calc(timeout.native_handle())};
    }

    template <typename Rep, typename Period>
    [[nodiscard]] static Deadline after(std::chrono::duration<Rep, Period> duration) noexcept
    {
        return after(Timeout::after(duration));
    }

    [[nodiscard]] static Deadline forever() noexcept
    {
        return after(Timeout::forever());
    }

    [[nodiscard]] static Deadline from_native(k_timepoint_t timepoint) noexcept
    {
        return Deadline{timepoint};
    }

    [[nodiscard]] Timeout remaining() const noexcept
    {
        return Timeout::from_native(sys_timepoint_timeout(value_));
    }

    [[nodiscard]] bool expired() const noexcept
    {
        return sys_timepoint_expired(value_);
    }

    [[nodiscard]] k_timepoint_t native_handle() const noexcept
    {
        return value_;
    }

    friend std::strong_ordering operator<=>(const Deadline& left, const Deadline& right) noexcept
    {
        const int comparison = sys_timepoint_cmp(left.value_, right.value_);
        if (comparison < 0) {
            return std::strong_ordering::less;
        }
        if (comparison > 0) {
            return std::strong_ordering::greater;
        }
        return std::strong_ordering::equal;
    }

    friend bool operator==(const Deadline& left, const Deadline& right) noexcept
    {
        return sys_timepoint_cmp(left.value_, right.value_) == 0;
    }

  private:
    explicit Deadline(k_timepoint_t value) noexcept : value_(value) {}

    k_timepoint_t value_{};
};

} // namespace solar::kernel
