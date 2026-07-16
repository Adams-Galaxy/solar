#pragma once

#include <cstdint>
#include <string_view>

#include "solar/core/fixed_string.hpp"

namespace solar::hardware
{

enum class EndpointKind : std::uint8_t
{
    Unknown,
    Device,
    Gpio,
    Spi,
    I2c,
    Uart,
    Adc,
    Pwm,
    Counter,
    Watchdog,
};

enum class SelectorKind : std::uint8_t
{
    Explicit,
    Alias,
    Chosen,
    NodeLabel,
};

enum class Capability : std::uint16_t
{
    Metadata = 1U << 0U,
    Ready = 1U << 1U,
    NativeHandle = 1U << 2U,
    Read = 1U << 3U,
    Write = 1U << 4U,
    Interrupt = 1U << 5U,
    Async = 1U << 6U,
};

using CapabilitySet = std::uint16_t;

[[nodiscard]] constexpr CapabilitySet capability(Capability value) noexcept
{
    return static_cast<CapabilitySet>(value);
}

[[nodiscard]] constexpr CapabilitySet operator|(Capability left, Capability right) noexcept
{
    return capability(left) | capability(right);
}

[[nodiscard]] constexpr CapabilitySet operator|(CapabilitySet left, Capability right) noexcept
{
    return left | capability(right);
}

template <std::size_t SelectorN, std::size_t PathN, std::size_t CompatibleN>
struct Identity
{
    SelectorKind selector_kind{SelectorKind::Explicit};
    FixedString<SelectorN> selector;
    FixedString<PathN> path;
    FixedString<CompatibleN> compatible;
    std::uint32_t stable_id{};
    EndpointKind endpoint_kind{EndpointKind::Unknown};
    CapabilitySet capabilities{capability(Capability::Metadata)};
    bool okay{};

    constexpr bool operator==(const Identity&) const = default;
};

template <std::size_t SelectorN, std::size_t PathN, std::size_t CompatibleN>
[[nodiscard]] consteval auto
identity(SelectorKind selector_kind, const char (&selector)[SelectorN],
         const char (&path)[PathN], const char (&compatible)[CompatibleN],
         std::uint32_t stable_id, EndpointKind endpoint_kind,
         CapabilitySet capabilities, bool okay)
{
    return Identity<SelectorN, PathN, CompatibleN>{
        .selector_kind = selector_kind,
        .selector = FixedString{selector},
        .path = FixedString{path},
        .compatible = FixedString{compatible},
        .stable_id = stable_id,
        .endpoint_kind = endpoint_kind,
        .capabilities = capabilities,
        .okay = okay,
    };
}

struct InventoryEntry
{
    SelectorKind selector_kind{};
    std::string_view selector;
    std::string_view path;
    std::string_view compatible;
    std::uint32_t stable_id{};
    EndpointKind endpoint_kind{};
    CapabilitySet capabilities{};
    bool okay{};
};

} // namespace solar::hardware
