#pragma once

#include <cstdint>

#include <zephyr/kernel.h>

#include "solar/core/status.hpp"
#include "solar/kernel/deadline.hpp"

namespace solar::kernel
{

class Semaphore
{
public:
    explicit Semaphore(std::uint32_t initial_count = 0, std::uint32_t max_count = 1)
    {
        k_sem_init(&sem_, initial_count, max_count);
    }

    Status give()
    {
        k_sem_give(&sem_);
        return Status::Ok;
    }

    Status take(Tick timeout_ticks = WaitForever)
    {
        return k_sem_take(&sem_, to_timeout(timeout_ticks)) == 0 ? Status::Ok : Status::Timeout;
    }

    template <class Rep, class Period>
    Status take(std::chrono::duration<Rep, Period> timeout)
    {
        return take(to_ticks(timeout));
    }

    Status take(const Deadline &deadline)
    {
        return take(deadline.remaining_ticks());
    }

    Status try_take()
    {
        return take(0);
    }

    Status give_from_isr(bool &higher_priority_woken)
    {
        higher_priority_woken = false;
        return give();
    }

private:
    k_sem sem_{};
};

class BinarySemaphore : public Semaphore
{
public:
    explicit BinarySemaphore(bool initially_available = false) : Semaphore(initially_available ? 1U : 0U, 1U) {}
};

} // namespace solar::kernel
