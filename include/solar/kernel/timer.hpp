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

    Timer(const char *, Timeout period, bool auto_reload, Callback callback)
        : period_(period), auto_reload_(auto_reload), callback_(callback)
    {
        k_timer_init(&timer_, &Timer::expiry, nullptr);
        k_timer_user_data_set(&timer_, this);
    }

    Timer(const char *name, Tick period_ticks, bool auto_reload, Callback callback)
        : Timer(name, Timeout::after_ticks(period_ticks), auto_reload, callback) {}

    Status start(Timeout = Timeout::no_wait())
    {
        k_timer_start(&timer_, period_.native(), auto_reload_ ? period_.native() : K_NO_WAIT);
        return Status::Ok;
    }

    Status stop(Timeout = Timeout::no_wait())
    {
        k_timer_stop(&timer_);
        return Status::Ok;
    }

    Status reset(Timeout block_time = Timeout::no_wait())
    {
        (void)block_time;
        return start();
    }

    Status change_period(Timeout new_period, Timeout = Timeout::no_wait())
    {
        period_ = new_period;
        if (is_running())
        {
            return start();
        }
        return Status::Ok;
    }

    Status change_period(Tick new_period_ticks, Tick = 0)
    {
        return change_period(Timeout::after_ticks(new_period_ticks));
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
    Timeout period_ = Timeout::no_wait();
    bool auto_reload_ = false;
    Callback callback_ = nullptr;
};

} // namespace solar::kernel
