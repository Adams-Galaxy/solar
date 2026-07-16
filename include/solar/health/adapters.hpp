#pragma once

#include "solar/events/types.hpp"
#include "solar/health/api.hpp"
#include "solar/log/types.hpp"
#include "solar/metrics/types.hpp"

namespace solar::health::adapters
{

template <typename Component, typename Application = DefaultApplication>
[[nodiscard]] Result<void, Error> event(const events::EventRecord& record) noexcept
{
    Assessment assessment = nominal();
    if (record.last_status != Status::Ok && record.last_status != Status::NotReady) {
        assessment = faulted(record.last_status);
    } else if (record.overflow_latched || record.known_lost > 0 || record.processor_failures > 0 ||
               record.ingress_rejected > 0) {
        assessment = degraded();
        assessment.detail = static_cast<std::uint32_t>(record.known_lost + record.ingress_rejected);
    }
    return detail::commit_adapter<bound_system_t<Application>, Component>(
        assessment, SourceKind::Events, record.event.value);
}

template <typename Component, typename Application = DefaultApplication>
[[nodiscard]] Result<void, Error> metric(const metrics::MetricRecord& record) noexcept
{
    Assessment assessment = record.initialized ? nominal() : Assessment{};
    if (record.last_status != Status::Ok && record.last_status != Status::NotReady) {
        assessment = faulted(record.last_status);
    } else if (record.degraded || record.overflows > 0 || record.saturations > 0 ||
               record.invalid_numeric > 0) {
        assessment = degraded();
        assessment.detail = static_cast<std::uint32_t>(record.overflows + record.saturations +
                                                       record.invalid_numeric);
    }
    return detail::commit_adapter<bound_system_t<Application>, Component>(
        assessment, SourceKind::Metrics, record.metric.value);
}

template <typename Component, typename Application = DefaultApplication>
[[nodiscard]] Result<void, Error> logging(const log::FacilityRecord& record) noexcept
{
    Assessment assessment = record.ready ? nominal() : Assessment{};
    if (record.panic ||
        (record.last_status != Status::Ok && record.last_status != Status::NotReady)) {
        assessment = faulted(record.last_status == Status::Ok ? Status::Error : record.last_status);
    } else if (record.dropped > 0 || record.truncated > 0 || record.sink_failures > 0 ||
               record.preceding_loss) {
        assessment = degraded();
        assessment.detail = static_cast<std::uint32_t>(record.dropped + record.sink_failures);
    }
    return detail::commit_adapter<bound_system_t<Application>, Component>(assessment,
                                                                          SourceKind::Logging);
}

template <typename Component, typename Endpoint, typename Application = DefaultApplication>
[[nodiscard]] Result<void, Error> hardware() noexcept
{
    static_assert(requires {
        { Endpoint::ready() } -> std::same_as<bool>;
    });
    const auto assessment = Endpoint::ready() ? nominal() : faulted(Status::NotReady);
    return detail::commit_adapter<bound_system_t<Application>, Component>(assessment,
                                                                          SourceKind::Hardware);
}

} // namespace solar::health::adapters
