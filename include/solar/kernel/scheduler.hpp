#pragma once

#include <cstdint>
#include <limits>
#include <utility>

#include <zephyr/kernel.h>

#include "solar/core/status.hpp"
#include "solar/kernel/priority.hpp"
#include "solar/kernel/time.hpp"

namespace solar::kernel
{

[[nodiscard]] inline bool can_yield()
{
    return k_can_yield();
}

/** True when the current context is a preemptible thread. ISR returns false. */
[[nodiscard]] inline bool current_is_preemptible()
{
    return k_is_preempt_thread() != 0;
}

/** Re-evaluate the scheduler without applying yield's equal-priority rule. */
[[nodiscard]] inline Result<void> reschedule()
{
    if (k_is_in_isr()) {
        return fail<Error>({.status = Status::Invalid});
    }
    k_reschedule();
    return {};
}

inline constexpr bool time_slicing_available = IS_ENABLED(CONFIG_TIMESLICING);

/** Configure Zephyr's global preemptive-thread time slicing policy. */
[[nodiscard]] inline Result<void> configure_time_slicing(Milliseconds slice,
                                                         Priority eligible_from)
{
    if (k_is_in_isr()) {
        return fail<Error>({.status = Status::Invalid});
    }
    if (!time_slicing_available) {
        return fail<Error>({.status = Status::NotSupported});
    }
    if (slice.count() <= 0 ||
        static_cast<std::uint64_t>(slice.count()) >
            static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
        return fail<Error>({.status = Status::Invalid});
    }
#if defined(CONFIG_TIMESLICING)
    k_sched_time_slice_set(static_cast<std::int32_t>(slice.count()), eligible_from.native_handle());
    return {};
#else
    (void)eligible_from;
    return fail<Error>({.status = Status::NotSupported});
#endif
}

[[nodiscard]] inline Result<void> disable_time_slicing()
{
    if (k_is_in_isr()) {
        return fail<Error>({.status = Status::Invalid});
    }
    if (!time_slicing_available) {
        return fail<Error>({.status = Status::NotSupported});
    }
#if defined(CONFIG_TIMESLICING)
    k_sched_time_slice_set(0, 0);
    return {};
#else
    return fail<Error>({.status = Status::NotSupported});
#endif
}

class SchedulerLock
{
  public:
    [[nodiscard]] static Result<SchedulerLock> acquire()
    {
        if (k_is_in_isr()) {
            return fail<solar::Error>({.status = solar::Status::Invalid});
        }
        if (!k_can_yield()) {
            return fail<solar::Error>({.status = solar::Status::NotReady});
        }
        k_sched_lock();
        return SchedulerLock{};
    }

    ~SchedulerLock()
    {
        if (owns_) {
            k_sched_unlock();
        }
    }

    SchedulerLock(const SchedulerLock&) = delete;
    SchedulerLock& operator=(const SchedulerLock&) = delete;

    SchedulerLock(SchedulerLock&& other) noexcept : owns_(std::exchange(other.owns_, false)) {}

    SchedulerLock& operator=(SchedulerLock&&) = delete;

  private:
    SchedulerLock() = default;

    bool owns_ = true;
};

} // namespace solar::kernel
