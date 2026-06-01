#pragma once

#include <cstdint>

#include "low_level/rtos/notify.hpp"
#include "solar/rtos/deadline.hpp"
#include "solar/rtos/notify_action.hpp"
#include "solar/rtos/thread.hpp"

namespace solar::rtos
{

inline Status notify(ThreadId target, std::uint32_t value, NotifyAction action)
{
    return low_level::rtos::notify(target, value, action);
}

inline Status notify(const Thread &target, std::uint32_t value, NotifyAction action)
{
    return solar::rtos::notify(target.native_handle(), value, action);
}

inline Status notify_from_isr(ThreadId target, std::uint32_t value, NotifyAction action, bool &higher_priority_woken)
{
    return low_level::rtos::notify_from_isr(target, value, action, higher_priority_woken);
}

inline Status wait(std::uint32_t &out_value, Tick timeout_ticks = WaitForever)
{
    return low_level::rtos::wait(out_value, timeout_ticks);
}

inline Status wait(std::uint32_t &out_value, const Deadline &deadline)
{
    return wait(out_value, deadline.remaining_ticks());
}

inline Status give(ThreadId target)
{
    return low_level::rtos::give(target);
}

inline Status give(const Thread &target)
{
    return solar::rtos::give(target.native_handle());
}

inline Status take(std::uint32_t &out_value, Tick timeout_ticks = WaitForever)
{
    return low_level::rtos::take(out_value, timeout_ticks);
}

inline Status take(std::uint32_t &out_value, const Deadline &deadline)
{
    return take(out_value, deadline.remaining_ticks());
}

} // namespace solar::rtos
