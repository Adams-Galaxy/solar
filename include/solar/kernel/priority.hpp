#pragma once

#include <cstdint>

#include <zephyr/kernel.h>

#include "solar/core/status.hpp"

namespace solar::kernel
{

class Priority
{
  public:
    constexpr Priority() noexcept = default;

    template <std::uint32_t Level> [[nodiscard]] static consteval Priority preemptive()
    {
        static_assert(Level < CONFIG_NUM_PREEMPT_PRIORITIES,
                      "SOLAR_DIAGNOSTIC_INVALID_PREEMPTIVE_PRIORITY: level exceeds Zephyr's "
                      "configured preemptive range");
        return Priority{K_PRIO_PREEMPT(static_cast<int>(Level))};
    }

    template <std::uint32_t Level> [[nodiscard]] static consteval Priority cooperative()
    {
        static_assert(Level < CONFIG_NUM_COOP_PRIORITIES,
                      "SOLAR_DIAGNOSTIC_INVALID_COOPERATIVE_PRIORITY: level exceeds Zephyr's "
                      "configured cooperative range");
        return Priority{K_PRIO_COOP(static_cast<int>(Level))};
    }

    [[nodiscard]] static constexpr Result<Priority> try_preemptive(std::uint32_t level) noexcept
    {
        if (level >= CONFIG_NUM_PREEMPT_PRIORITIES) {
            return fail(Status::Invalid);
        }
        return Priority{K_PRIO_PREEMPT(static_cast<int>(level))};
    }

    [[nodiscard]] static constexpr Result<Priority> try_cooperative(std::uint32_t level) noexcept
    {
        if (level >= CONFIG_NUM_COOP_PRIORITIES) {
            return fail(Status::Invalid);
        }
        return Priority{K_PRIO_COOP(static_cast<int>(level))};
    }

    [[nodiscard]] static constexpr Result<Priority> from_native(int priority) noexcept
    {
        if (priority < -CONFIG_NUM_COOP_PRIORITIES || priority >= CONFIG_NUM_PREEMPT_PRIORITIES) {
            return fail(Status::Invalid);
        }
        return Priority{priority};
    }

    [[nodiscard]] constexpr int native_handle() const noexcept
    {
        return native_;
    }

    [[nodiscard]] constexpr bool is_cooperative() const noexcept
    {
        return native_ < 0;
    }

    [[nodiscard]] constexpr bool is_preemptive() const noexcept
    {
        return native_ >= 0;
    }

    friend constexpr bool operator==(Priority, Priority) noexcept = default;
    friend constexpr auto operator<=>(Priority, Priority) noexcept = default;

  private:
    explicit constexpr Priority(int native) noexcept : native_(native) {}

    int native_{};
};

} // namespace solar::kernel
