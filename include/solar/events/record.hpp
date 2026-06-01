#pragma once

#include <cstdint>

#include "solar/events/catalog.hpp"

namespace solar::events
{

/**
 * @brief Runtime event record stored by event facilities and delivered to sinks.
 *
 * The type-level `Event<...>` descriptor declares ownership and payload shape.
 * This record is the compact runtime envelope used by the first event facility
 * slice. Rich typed payload encoding can build on top of the same descriptor.
 */
struct Record
{
    std::uint64_t timestamp_us = 0;
    std::uint32_t sequence = 0;
    std::uint32_t id = 0;
    Severity severity = Severity::Info;
    const char *name = nullptr;
    const char *source = nullptr;
    std::int32_t value = 0;
    std::uint32_t detail = 0;
};

struct Stats
{
    /// Records accepted into the facility history.
    std::uint32_t emitted = 0;
    /// Oldest history records overwritten by the fixed ring buffer.
    std::uint32_t dropped = 0;
    /// Emissions where at least one sink returned an error.
    std::uint32_t failed = 0;
};

} // namespace solar::events
