#pragma once

#include "solar/events/record.hpp"

namespace solar::events
{

/**
 * @brief Severity threshold used by event sinks.
 */
template <Severity MinimumSeverity = Severity::Debug>
struct Filter
{
    static constexpr Severity minimum_severity = MinimumSeverity;

    static constexpr bool accepts(Record const &record)
    {
        return at_least(record.severity, MinimumSeverity);
    }
};

} // namespace solar::events
