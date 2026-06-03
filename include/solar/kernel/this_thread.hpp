#pragma once

#include <zephyr/kernel.h>

#include "solar/kernel/deadline.hpp"
#include "solar/kernel/priority.hpp"

namespace solar::kernel::ThisThread
{

/**
 * @brief Sleep the current thread for a chrono duration.
 */
template <class Rep, class Period>
inline void sleep_for(std::chrono::duration<Rep, Period> duration)
{
    k_sleep(to_timeout(to_ticks(duration)));
}

inline void sleep_for(Tick ticks)
{
    k_sleep(to_timeout(ticks));
}

inline void sleep_for(Timeout timeout)
{
    k_sleep(timeout.native());
}

inline void sleep_until(Tick deadline_ticks)
{
    const Tick now = now_ticks();
    if (deadline_ticks > now)
    {
        sleep_for(deadline_ticks - now);
    }
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
    k_yield();
}

inline ThreadId get_id()
{
    return k_current_get();
}

inline ThreadId id()
{
    return get_id();
}

inline NativePriority priority()
{
    return k_thread_priority_get(k_current_get());
}

inline void set_priority(NativePriority priority_value)
{
    k_thread_priority_set(k_current_get(), priority_value);
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
    k_thread_suspend(k_current_get());
}

} // namespace solar::kernel::ThisThread
