#pragma once

#include <zephyr/kernel.h>

#include "solar/core/status.hpp"
#include "solar/kernel/time.hpp"

namespace solar::kernel
{

class Timer
{
public:
    using Callback = void (*)(Timer &);

    Timer(const char *, Tick period_ticks, bool auto_reload, Callback callback)
        : period_ticks_(period_ticks), auto_reload_(auto_reload), callback_(callback)
    {
        k_timer_init(&timer_, &Timer::expiry, nullptr);
        k_timer_user_data_set(&timer_, this);
    }

    Status start(Tick = 0)
    {
        k_timer_start(&timer_, to_timeout(period_ticks_), auto_reload_ ? to_timeout(period_ticks_) : K_NO_WAIT);
        return Status::Ok;
    }

    Status stop(Tick = 0)
    {
        k_timer_stop(&timer_);
        return Status::Ok;
    }

    Status reset(Tick block_time_ticks = 0)
    {
        (void)block_time_ticks;
        return start();
    }

    Status change_period(Tick new_period_ticks, Tick = 0)
    {
        period_ticks_ = new_period_ticks;
        if (is_running())
        {
            return start();
        }
        return Status::Ok;
    }

    bool is_running() const
    {
        return k_timer_remaining_get(const_cast<k_timer *>(&timer_)) > 0;
    }

private:
    static void expiry(k_timer *timer)
    {
        auto *self = static_cast<Timer *>(k_timer_user_data_get(timer));
        if (self != nullptr && self->callback_ != nullptr)
        {
            self->callback_(*self);
        }
    }

    k_timer timer_{};
    Tick period_ticks_ = 0;
    bool auto_reload_ = false;
    Callback callback_ = nullptr;
};

} // namespace solar::kernel
