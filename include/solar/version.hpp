#pragma once

#include <cstdint>

namespace solar
{

/** Semantic version of the Solar public contract. */
struct Version
{
    std::uint16_t major;
    std::uint16_t minor;
    std::uint16_t patch;

    constexpr bool operator==(const Version &) const = default;
};

/** Version of the headers in this build. */
inline constexpr Version version{0, 1, 0};

} // namespace solar
