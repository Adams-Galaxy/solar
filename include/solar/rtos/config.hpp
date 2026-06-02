#pragma once

#include <cstdint>

#include <zephyr/kernel.h>

namespace solar::rtos
{

constexpr std::uint8_t PriorityStepsPerBand = 6;
constexpr std::uint8_t PriorityBands = 5;
constexpr std::uint8_t PriorityLevels = PriorityStepsPerBand * PriorityBands;

using NativePriority = int;

constexpr NativePriority max_native_priority()
{
    return CONFIG_NUM_PREEMPT_PRIORITIES > 0 ? CONFIG_NUM_PREEMPT_PRIORITIES : 1;
}

} // namespace solar::rtos

#ifndef SOLAR_RTOS_ASSERT
#define SOLAR_RTOS_ASSERT(expr) __ASSERT_NO_MSG(expr)
#endif
