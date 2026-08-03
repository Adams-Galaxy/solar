#pragma once

#include <cstdint>
#include <type_traits>

#include <zephyr/kernel.h>

#include "solar/core/status.hpp"

namespace solar::kernel
{

enum class PriorityClass : std::uint8_t
{
    Cooperative,
    Preemptive,
};

/** An exact Zephyr application-thread priority. */
class Priority
{
  public:
    template <std::uint32_t Level> [[nodiscard]] static consteval Priority preemptive() noexcept
    {
        static_assert(Level < CONFIG_NUM_PREEMPT_PRIORITIES,
                      "SOLAR_DIAGNOSTIC_INVALID_PREEMPTIVE_PRIORITY: level exceeds Zephyr's "
                      "configured preemptive range");
        return Priority{K_PRIO_PREEMPT(static_cast<int>(Level))};
    }

    template <std::uint32_t Level> [[nodiscard]] static consteval Priority cooperative() noexcept
    {
        static_assert(Level < CONFIG_NUM_COOP_PRIORITIES,
                      "SOLAR_DIAGNOSTIC_INVALID_COOPERATIVE_PRIORITY: level exceeds Zephyr's "
                      "configured cooperative range");
        return Priority{K_PRIO_COOP(static_cast<int>(Level))};
    }

    /** Construct an exact signed Zephyr application-thread priority. */
    template <int Value> [[nodiscard]] static consteval Priority native() noexcept
    {
        static_assert(Value >= K_HIGHEST_APPLICATION_THREAD_PRIO &&
                          Value <= K_LOWEST_APPLICATION_THREAD_PRIO,
                      "SOLAR_DIAGNOSTIC_INVALID_NATIVE_PRIORITY: value is outside Zephyr's "
                      "configured application-thread range");
        return Priority{Value};
    }

#if CONFIG_NUM_METAIRQ_PRIORITIES > 0
    /** Construct a cooperative priority in Zephyr's synchronous Meta-IRQ subset. */
    template <std::uint32_t Level> [[nodiscard]] static consteval Priority meta_irq() noexcept
    {
        static_assert(Level < CONFIG_NUM_METAIRQ_PRIORITIES,
                      "SOLAR_DIAGNOSTIC_INVALID_METAIRQ_PRIORITY: level exceeds Zephyr's "
                      "configured Meta-IRQ range");
        return Priority{K_PRIO_COOP(static_cast<int>(Level))};
    }
#endif

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
        if (priority < K_HIGHEST_APPLICATION_THREAD_PRIO ||
            priority > K_LOWEST_APPLICATION_THREAD_PRIO) {
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

    [[nodiscard]] constexpr PriorityClass category() const noexcept
    {
        return is_cooperative() ? PriorityClass::Cooperative : PriorityClass::Preemptive;
    }

    /** Return the zero-based level within the cooperative or preemptive class. */
    [[nodiscard]] constexpr std::uint32_t level() const noexcept
    {
        return is_cooperative()
                   ? static_cast<std::uint32_t>(native_ - K_HIGHEST_APPLICATION_THREAD_PRIO)
                   : static_cast<std::uint32_t>(native_);
    }

    [[nodiscard]] constexpr bool is_meta_irq() const noexcept
    {
#if CONFIG_NUM_METAIRQ_PRIORITIES > 0
        return is_cooperative() && level() < CONFIG_NUM_METAIRQ_PRIORITIES;
#else
        return false;
#endif
    }

    /** Zephyr schedules this priority ahead of `other`. */
    [[nodiscard]] constexpr bool higher_than(Priority other) const noexcept
    {
        return native_ < other.native_;
    }

    /** Zephyr schedules this priority behind `other`. */
    [[nodiscard]] constexpr bool lower_than(Priority other) const noexcept
    {
        return native_ > other.native_;
    }

    friend constexpr bool operator==(Priority, Priority) noexcept = default;

  private:
    explicit constexpr Priority(int native) noexcept : native_(native) {}

    int native_;
};

static_assert(!std::is_default_constructible_v<Priority>);

} // namespace solar::kernel
