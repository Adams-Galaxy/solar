#pragma once

#include "low_level/rtos/scheduler.hpp"

namespace solar::rtos
{

using SchedulerState = low_level::rtos::SchedulerState;

inline void start_scheduler()
{
    low_level::rtos::start_scheduler();
}

inline SchedulerState scheduler_state()
{
    return low_level::rtos::scheduler_state();
}

} // namespace solar::rtos

