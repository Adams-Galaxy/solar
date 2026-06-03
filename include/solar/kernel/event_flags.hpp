#pragma once

#include <cstdint>

#include <zephyr/kernel.h>

#include "solar/kernel/time.hpp"

namespace solar::kernel
{

using EventBits = std::uint32_t;

class Event
{
public:
    Event()
    {
        k_event_init(&event_);
    }

    EventBits set(EventBits bits)
    {
        k_event_post(&event_, bits);
        return k_event_test(&event_, 0xFFFFFFFFu);
    }

    EventBits set_isr(EventBits bits)
    {
        return set(bits);
    }

    EventBits clear(EventBits bits)
    {
        k_event_clear(&event_, bits);
        return k_event_test(&event_, 0xFFFFFFFFu);
    }

    EventBits wait(EventBits bits,
                   bool clear_on_exit = true,
                   bool wait_all = false,
                   Timeout timeout = Timeout::forever())
    {
        return wait_all ? k_event_wait_all(&event_, bits, clear_on_exit, timeout.native())
                        : k_event_wait(&event_, bits, clear_on_exit, timeout.native());
    }

    EventBits wait(EventBits bits, bool clear_on_exit, bool wait_all, Tick timeout_ticks)
    {
        return wait(bits, clear_on_exit, wait_all, Timeout::after_ticks(timeout_ticks));
    }

    EventBits wait_any(EventBits bits, Timeout timeout = Timeout::forever(), bool clear_on_exit = true)
    {
        return wait(bits, clear_on_exit, false, timeout);
    }

    EventBits wait_any(EventBits bits, Tick timeout_ticks, bool clear_on_exit = true)
    {
        return wait_any(bits, Timeout::after_ticks(timeout_ticks), clear_on_exit);
    }

    EventBits wait_all(EventBits bits, Timeout timeout = Timeout::forever(), bool clear_on_exit = true)
    {
        return wait(bits, clear_on_exit, true, timeout);
    }

    EventBits wait_all(EventBits bits, Tick timeout_ticks, bool clear_on_exit = true)
    {
        return wait_all(bits, Timeout::after_ticks(timeout_ticks), clear_on_exit);
    }

    k_event *native_handle()
    {
        return &event_;
    }

private:
    k_event event_{};
};

using EventFlags = Event;

} // namespace solar::kernel
