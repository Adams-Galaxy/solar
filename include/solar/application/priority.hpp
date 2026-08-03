#pragma once

#include <cstdint>

#if defined(__ZEPHYR__)
#include "solar/kernel/priority.hpp"
#endif

namespace solar
{

/** Select a zero-based Zephyr preemptive priority level. */
template <std::uint32_t Level> struct PreemptivePriority
{
    static constexpr std::uint32_t level = Level;

#if defined(__ZEPHYR__)
    [[nodiscard]] static consteval kernel::Priority resolve() noexcept
    {
        return kernel::Priority::preemptive<Level>();
    }
#endif
};

/** Select a zero-based Zephyr cooperative priority level. */
template <std::uint32_t Level> struct CooperativePriority
{
    static constexpr std::uint32_t level = Level;

#if defined(__ZEPHYR__)
    [[nodiscard]] static consteval kernel::Priority resolve() noexcept
    {
        return kernel::Priority::cooperative<Level>();
    }
#endif
};

/** Select an exact signed native Zephyr application-thread priority. */
template <int Value> struct Priority
{
    static constexpr int native_value = Value;

#if defined(__ZEPHYR__)
    [[nodiscard]] static consteval kernel::Priority resolve() noexcept
    {
        return kernel::Priority::native<Value>();
    }
#endif
};

} // namespace solar
