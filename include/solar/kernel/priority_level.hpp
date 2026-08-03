#pragma once

#include <cstdint>

namespace solar::kernel
{

/** Portable intent-based priority used by application and service policies. */
enum class PriorityLevel : std::uint8_t
{
    Background,
    Low,
    Normal,
    High,
    Realtime,
};

} // namespace solar::kernel
