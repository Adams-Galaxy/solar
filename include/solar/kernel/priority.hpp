#pragma once

#include <cstdint>

#include "solar/kernel/config.hpp"

namespace solar::kernel
{

/**
 * @brief Portable priority ladder mapped onto Zephyr priorities.
 *
 * Solar keeps a stable semantic priority scale even when Zephyr exposes a
 * different number of native priority levels.
 */
enum class Priority : std::uint8_t
{
    Idle = 0,
    Idle1,
    Idle2,
    Idle3,
    Idle4,
    Idle5,
    Low,
    Low1,
    Low2,
    Low3,
    Low4,
    Low5,
    Normal,
    Normal1,
    Normal2,
    Normal3,
    Normal4,
    Normal5,
    High,
    High1,
    High2,
    High3,
    High4,
    High5,
    Realtime,
    Realtime1,
    Realtime2,
    Realtime3,
    Realtime4,
    Realtime5,
};

constexpr std::uint8_t priority_index(Priority priority)
{
    return static_cast<std::uint8_t>(priority);
}

constexpr NativePriority to_native_priority(Priority priority)
{
    const NativePriority native_levels = max_native_priority();
    if (native_levels <= 1)
    {
        return 0;
    }

    constexpr std::uint8_t max_index = PriorityLevels - 1;
    const std::uint32_t index = priority_index(priority);
    const std::uint32_t scaled = (index * (native_levels - 1) + (max_index / 2)) / max_index;
    return static_cast<NativePriority>((native_levels - 1) - scaled);
}

constexpr Priority from_native_priority(NativePriority native_priority)
{
    const NativePriority native_levels = max_native_priority();
    if (native_levels <= 1)
    {
        return Priority::Idle;
    }

    constexpr std::uint8_t max_index = PriorityLevels - 1;
    const std::uint32_t clamped = native_priority >= native_levels ? native_levels - 1 : native_priority;
    const std::uint32_t reversed = (native_levels - 1) - clamped;
    const std::uint32_t scaled = (reversed * max_index + ((native_levels - 1) / 2)) / (native_levels - 1);
    return static_cast<Priority>(scaled);
}

} // namespace solar::kernel
