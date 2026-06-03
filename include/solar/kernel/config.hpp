#pragma once

#include <cstdint>

#include <zephyr/kernel.h>

#include "solar/core/status.hpp"

namespace solar::kernel
{

constexpr std::uint8_t PriorityStepsPerBand = 6;
constexpr std::uint8_t PriorityBands = 5;
constexpr std::uint8_t PriorityLevels = PriorityStepsPerBand * PriorityBands;

using NativePriority = int;

constexpr NativePriority max_native_priority()
{
    return CONFIG_NUM_PREEMPT_PRIORITIES > 0 ? CONFIG_NUM_PREEMPT_PRIORITIES : 1;
}

inline Status status_from_native(int result, Status zero_status = Status::Ok)
{
    if (result >= 0)
    {
        return zero_status;
    }

    return status_from_errno(result);
}

inline Status status_from_native_wait(int result, Status zero_status = Status::Ok)
{
    if (result >= 0)
    {
        return zero_status;
    }

    if (result == -EAGAIN)
    {
        return Status::Timeout;
    }

    return status_from_errno(result);
}

} // namespace solar::kernel

#ifndef SOLAR_KERNEL_ASSERT
#define SOLAR_KERNEL_ASSERT(expr) __ASSERT_NO_MSG(expr)
#endif
