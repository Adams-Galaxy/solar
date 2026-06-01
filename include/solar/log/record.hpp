#pragma once

#include <cstdint>

#include "solar/log/level.hpp"

namespace solar::log
{

/**
 * @brief Runtime log record passed through filters and formatters.
 */
struct Record
{
    std::uint64_t timestamp_us = 0;
    Level level = Level::Info;
    const char *source = nullptr;
    const char *category = nullptr;
    std::uint16_t id = 0;
    const char *message = nullptr;
};

/**
 * @brief Small compile-time/runtime FNV helper for stable log callsite IDs.
 */
constexpr std::uint16_t fnv16(const char *text, std::uint16_t hash = 0x811C)
{
    return (text == nullptr || *text == '\0')
               ? hash
               : fnv16(text + 1, static_cast<std::uint16_t>((hash ^ static_cast<std::uint8_t>(*text)) * 0x0101));
}

} // namespace solar::log

#define SOLAR_LOG_ID() (::solar::log::fnv16(__PRETTY_FUNCTION__))
