#pragma once

#include <cstdint>

#include <zephyr/kernel.h>

#include "solar/core/status.hpp"
#include "solar/kernel/priority_level.hpp"

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

    /** Map portable scheduling intent across Zephyr's configured preemptive range. */
    template <PriorityLevel Level> [[nodiscard]] static consteval Priority semantic()
    {
        static_assert(CONFIG_NUM_PREEMPT_PRIORITIES > 0,
                      "SOLAR_DIAGNOSTIC_NO_PREEMPTIVE_PRIORITIES: semantic priorities require "
                      "a configured preemptive priority");
        constexpr std::uint32_t maximum_rank = static_cast<std::uint32_t>(PriorityLevel::Realtime);
        constexpr std::uint32_t rank = static_cast<std::uint32_t>(Level);
        constexpr std::uint32_t lowest_native = CONFIG_NUM_PREEMPT_PRIORITIES - 1U;
        constexpr std::uint32_t native =
            ((maximum_rank - rank) * lowest_native + maximum_rank / 2U) / maximum_rank;
        return Priority{K_PRIO_PREEMPT(static_cast<int>(native))};
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
            return fail<solar::Error>({.status = solar::Status::Invalid});
        }
        return Priority{K_PRIO_PREEMPT(static_cast<int>(level))};
    }

    [[nodiscard]] static constexpr Result<Priority> try_cooperative(std::uint32_t level) noexcept
    {
        if (level >= CONFIG_NUM_COOP_PRIORITIES) {
            return fail<solar::Error>({.status = solar::Status::Invalid});
        }
        return Priority{K_PRIO_COOP(static_cast<int>(level))};
    }

    [[nodiscard]] static constexpr Result<Priority> from_native(int priority) noexcept
    {
        if (priority < -CONFIG_NUM_COOP_PRIORITIES || priority >= CONFIG_NUM_PREEMPT_PRIORITIES) {
            return fail<solar::Error>({.status = solar::Status::Invalid});
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
