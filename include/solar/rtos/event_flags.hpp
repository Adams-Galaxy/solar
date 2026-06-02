#pragma once

#include <cstdint>

#include <zephyr/kernel.h>

#include "solar/rtos/time.hpp"

namespace solar::rtos
{

using EventBits = std::uint32_t;

class EventFlags
{
public:
    EventFlags()
    {
        k_event_init(&event_);
    }

    EventBits set(EventBits bits)
    {
        k_event_post(&event_, bits);
        return k_event_test(&event_, 0xFFFFFFFFu);
    }

    EventBits clear(EventBits bits)
    {
        k_event_clear(&event_, bits);
        return k_event_test(&event_, 0xFFFFFFFFu);
    }

    EventBits wait(EventBits bits, bool clear_on_exit = true, bool wait_all = false, Tick timeout_ticks = WaitForever)
    {
        return wait_all ? k_event_wait_all(&event_, bits, clear_on_exit, to_timeout(timeout_ticks))
                        : k_event_wait(&event_, bits, clear_on_exit, to_timeout(timeout_ticks));
    }

    EventBits wait_any(EventBits bits, Tick timeout_ticks = WaitForever, bool clear_on_exit = true)
    {
        return wait(bits, clear_on_exit, false, timeout_ticks);
    }

    EventBits wait_all(EventBits bits, Tick timeout_ticks = WaitForever, bool clear_on_exit = true)
    {
        return wait(bits, clear_on_exit, true, timeout_ticks);
    }

private:
    k_event event_{};
};

} // namespace solar::rtos
