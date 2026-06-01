#pragma once

#include <cstdint>
#include <string_view>

#include "solar/core/fixed_string.hpp"
#include "solar/core/type_list.hpp"

namespace solar::events
{

/**
 * @brief Severity carried by typed event descriptors.
 */
enum class Severity : std::uint8_t
{
    Debug,
    Info,
    Warning,
    Error,
    Critical,
};

constexpr bool at_least(Severity value, Severity minimum)
{
    return static_cast<std::uint8_t>(value) >= static_cast<std::uint8_t>(minimum);
}

constexpr const char *to_string(Severity severity)
{
    switch (severity)
    {
    case Severity::Debug:
        return "DEBUG";
    case Severity::Info:
        return "INFO";
    case Severity::Warning:
        return "WARN";
    case Severity::Error:
        return "ERROR";
    case Severity::Critical:
        return "CRITICAL";
    }
    return "UNKNOWN";
}

constexpr std::uint32_t fnv1a32(std::string_view text)
{
    std::uint32_t hash = 2166136261UL;
    for (const char ch : text)
    {
        hash ^= static_cast<std::uint8_t>(ch);
        hash *= 16777619UL;
    }
    return hash;
}

/**
 * @brief Type-level list of events owned by a component or system.
 *
 * Event history storage and sinks are intentionally not part of this descriptor;
 * the catalog only declares which event types can exist.
 */
template <typename... Entries>
struct List : TypeList<Entries...>
{
    using SolarCatalogKind = List;
};

/**
 * @brief Minimal event descriptor for static contribution catalogs.
 *
 * Components contribute events they own/provide. Consumers should not redeclare
 * another component's event just because they might observe it.
 *
 * @tparam NameT Stable event name.
 * @tparam PayloadT Future typed payload shape. The current runtime record uses
 * compact numeric fields while keeping this type hook for richer encoding.
 * @tparam SeverityValue Default severity carried by emitted records.
 */
template <typename NameT, typename PayloadT = void, Severity SeverityValue = Severity::Info>
struct Event
{
    using Name = NameT;
    using Payload = PayloadT;
    static constexpr std::uint32_t id = fnv1a32(NameT::view());
    static constexpr Severity severity = SeverityValue;
};

} // namespace solar::events
