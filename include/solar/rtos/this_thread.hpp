#pragma once

#include "low_level/rtos/this_thread.hpp"
#include "solar/rtos/deadline.hpp"
#include "solar/rtos/priority.hpp"

namespace solar::rtos::ThisThread
{

/**
 * @brief Sleep the current thread for a chrono duration.
 */
template <class Rep, class Period>
inline void sleep_for(std::chrono::duration<Rep, Period> duration)
{
    low_level::rtos::sleep_for(duration);
}

inline void sleep_for(Tick ticks)
{
    low_level::rtos::sleep_for(ticks);
}

inline void sleep_until(Tick deadline_ticks)
{
    low_level::rtos::sleep_until(deadline_ticks);
}

inline DeadlineStatus wait_until(const Deadline &deadline)
{
    sleep_until(deadline.ticks());
    return deadline.status();
}

inline DeadlineStatus wait_until(Tick deadline_ticks, Tick grace_ticks = 0)
{
    return wait_until(Deadline(deadline_ticks, grace_ticks));
}

inline void yield()
{
    low_level::rtos::yield();
}

inline ThreadId get_id()
{
    return low_level::rtos::current_thread_id();
}

inline ThreadId id()
{
    return get_id();
}

inline NativePriority priority()
{
    return low_level::rtos::current_priority();
}

inline void set_priority(NativePriority priority_value)
{
    low_level::rtos::set_current_priority(priority_value);
}

inline void set_priority(Priority priority_value)
{
    set_priority(to_native_priority(priority_value));
}

inline Priority priority_enum()
{
    return from_native_priority(priority());
}

inline void suspend()
{
    low_level::rtos::suspend_current();
}

} // namespace solar::rtos::ThisThread
