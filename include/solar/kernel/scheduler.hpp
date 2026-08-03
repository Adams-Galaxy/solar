#pragma once

#include <utility>

#include <zephyr/kernel.h>

#include "solar/core/status.hpp"

namespace solar::kernel
{

[[nodiscard]] inline bool can_yield() noexcept
{
    return k_can_yield();
}

/** True when the current context is a preemptible thread. ISR returns false. */
[[nodiscard]] inline bool current_is_preemptible() noexcept
{
    return k_is_preempt_thread() != 0;
}

/** Re-evaluate the scheduler without applying yield's equal-priority rule. */
[[nodiscard]] inline Result<void> reschedule() noexcept
{
    if (k_is_in_isr()) {
        return fail<Error>({.status = Status::Invalid});
    }
    k_reschedule();
    return {};
}

class SchedulerLock
{
  public:
    [[nodiscard]] static Result<SchedulerLock> acquire() noexcept
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
