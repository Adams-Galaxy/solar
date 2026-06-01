#pragma once

#include <cstdint>

namespace solar::log
{

enum class Level : std::uint8_t
{
    Debug,
    Info,
    Warn,
    Error,
    Fatal,
};

constexpr const char *to_string(Level level)
{
    switch (level)
    {
    case Level::Debug:
        return "DEBUG";
    case Level::Info:
        return "INFO";
    case Level::Warn:
        return "WARN";
    case Level::Error:
        return "ERROR";
    case Level::Fatal:
        return "FATAL";
    }
    return "UNKNOWN";
}

constexpr bool at_least(Level actual, Level minimum)
{
    return static_cast<std::uint8_t>(actual) >= static_cast<std::uint8_t>(minimum);
}

} // namespace solar::log
