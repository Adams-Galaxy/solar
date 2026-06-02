#pragma once

namespace solar::rtos
{

enum class SchedulerState
{
    NotStarted,
    Running,
    Suspended,
};

inline void start_scheduler()
{
    // Zephyr starts the scheduler before application main.
}

inline SchedulerState scheduler_state()
{
    return SchedulerState::Running;
}

} // namespace solar::rtos
