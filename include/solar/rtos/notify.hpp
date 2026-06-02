#pragma once

#include <cstdint>

#include "solar/rtos/deadline.hpp"
#include "solar/rtos/notify_action.hpp"
#include "solar/rtos/thread.hpp"

namespace solar::rtos
{

inline Status notify(ThreadId target, std::uint32_t value, NotifyAction action)
{
    (void)target;
    (void)value;
    (void)action;
    return Status::NotReady;
}

inline Status notify(const Thread &target, std::uint32_t value, NotifyAction action)
{
    return solar::rtos::notify(target.native_handle(), value, action);
}

inline Status notify_from_isr(ThreadId target, std::uint32_t value, NotifyAction action, bool &higher_priority_woken)
{
    higher_priority_woken = false;
    return notify(target, value, action);
}

inline Status wait(std::uint32_t &out_value, Tick timeout_ticks = WaitForever)
{
    (void)out_value;
    (void)timeout_ticks;
    return Status::NotReady;
}

inline Status wait(std::uint32_t &out_value, const Deadline &deadline)
{
    return wait(out_value, deadline.remaining_ticks());
}

inline Status give(ThreadId target)
{
    (void)target;
    return Status::NotReady;
}

inline Status give(const Thread &target)
{
    return solar::rtos::give(target.native_handle());
}

inline Status take(std::uint32_t &out_value, Tick timeout_ticks = WaitForever)
{
    (void)out_value;
    (void)timeout_ticks;
    return Status::NotReady;
}

inline Status take(std::uint32_t &out_value, const Deadline &deadline)
{
    return take(out_value, deadline.remaining_ticks());
}

} // namespace solar::rtos
