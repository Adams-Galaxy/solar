#pragma once

#include <cstdint>

#include "low_level/rtos/config.hpp"

namespace solar::rtos
{

constexpr std::uint8_t PriorityStepsPerBand = 6;
constexpr std::uint8_t PriorityBands = 5;
constexpr std::uint8_t PriorityLevels = PriorityStepsPerBand * PriorityBands;

using NativePriority = low_level::rtos::NativePriority;

constexpr NativePriority max_native_priority()
{
    return low_level::rtos::max_native_priority();
}

} // namespace solar::rtos

#ifndef SOLAR_RTOS_ASSERT
#define SOLAR_RTOS_ASSERT(expr) LOW_LEVEL_RTOS_ASSERT(expr)
#endif
