#pragma once

#include <span>

#include "solar/health/protocol.hpp"

namespace solar::health
{

[[nodiscard]] constexpr Assessment nominal() noexcept
{
    return {.condition = Condition::Nominal,
            .readiness = Readiness::Ready,
            .safety = Safety::Acceptable,
            .freshness = Freshness::Current};
}

template <typename ErrorT> [[nodiscard]] constexpr Assessment degraded(const ErrorT& error) noexcept
{
    return {.condition = Condition::Degraded,
            .freshness = Freshness::Current,
            .last_error = detail::normalize_error(error, Operation::Report)};
}

[[nodiscard]] constexpr Assessment degraded() noexcept
{
    return {.condition = Condition::Degraded, .freshness = Freshness::Current};
}

template <typename ErrorT>
[[nodiscard]] constexpr Assessment faulted(const ErrorT& error,
                                           Safety safety = Safety::AtRisk) noexcept
{
    return {.condition = Condition::Faulted,
            .readiness = Readiness::NotReady,
            .safety = safety,
            .freshness = Freshness::Current,
            .last_error = detail::normalize_error(error, Operation::Report)};
}

[[nodiscard]] constexpr Assessment faulted(Safety safety = Safety::AtRisk) noexcept
{
    return {.condition = Condition::Faulted,
            .readiness = Readiness::NotReady,
            .safety = safety,
            .freshness = Freshness::Current};
}

template <typename ErrorT>
[[nodiscard]] constexpr Assessment recovering(const ErrorT& error) noexcept
{
    return {.condition = Condition::Degraded,
            .readiness = Readiness::NotReady,
            .safety = Safety::AtRisk,
            .freshness = Freshness::Current,
            .last_error = detail::normalize_error(error, Operation::Report),
            .recovering = true};
}

template <typename Component, typename Application = DefaultApplication>
[[nodiscard]] Result<Receipt, Error> report(Assessment assessment) noexcept
{
    return frontend::Operation<detail::ReportFrontend, Component, Application>::call(assessment);
}

template <typename Component, typename Application = DefaultApplication>
[[nodiscard]] Result<ProgressReceipt, Error> progress() noexcept
{
    return frontend::Operation<detail::ProgressFrontend, Component, Application>::call();
}

template <typename Component, typename Application = DefaultApplication>
[[nodiscard]] Result<void, Error> try_report_isr_from(CompactObservation observation) noexcept
{
    return frontend::Operation<detail::IsrReportFrontend, Component, Application>::call(
        observation);
}

template <typename Component, typename Application = DefaultApplication>
[[nodiscard]] Result<SubjectRecord, Error> record() noexcept
{
    return frontend::Operation<detail::RecordFrontend, Component, Application>::call();
}

template <typename Component, typename Application = DefaultApplication>
[[nodiscard]] Result<Receipt, Error> assess() noexcept
{
    return frontend::Operation<detail::AssessFrontend, Component, Application>::call();
}

template <typename Component, typename Declaration, typename Application = DefaultApplication>
[[nodiscard]] Result<void, Error> check() noexcept
{
    using System = bound_system_t<Application>;
    using Monitor = OwnedMonitor<Component, Declaration>;
    static_assert(System::HealthMonitorCatalog::template contains<Monitor>,
                  "SOLAR_DIAGNOSTIC_HEALTH_CHECK_NOT_REGISTERED: named check is absent from the "
                  "component Health declaration");
    return detail::run_check<System, Monitor>();
}

template <typename Component, typename Declaration, typename Application = DefaultApplication>
[[nodiscard]] Result<void, Error> observe(Observation observation) noexcept
{
    using System = bound_system_t<Application>;
    using Monitor = OwnedMonitor<Component, Declaration>;
    static_assert(System::HealthMonitorCatalog::template contains<Monitor>,
                  "SOLAR_DIAGNOSTIC_HEALTH_MONITOR_NOT_REGISTERED: observed monitor is absent "
                  "from the component Health declaration");
    return detail::commit_monitor<System, Monitor>(observation);
}

template <typename Application = DefaultApplication>
[[nodiscard]] Result<void, Error> refresh() noexcept
{
    return detail::refresh<bound_system_t<Application>>();
}

template <typename Application = DefaultApplication>
[[nodiscard]] Result<SystemRecord, Error> state() noexcept
{
    return detail::system_record<bound_system_t<Application>>();
}

template <typename Application = DefaultApplication> [[nodiscard]] auto subjects() noexcept
{
    using System = bound_system_t<Application>;
    return System::Catalogs::template Of<component::Tag>::descriptors();
}

template <typename Application = DefaultApplication> [[nodiscard]] auto conditions() noexcept
{
    return detail::subject_records<bound_system_t<Application>>();
}

template <typename Component, typename Application = DefaultApplication>
[[nodiscard]] auto monitors() noexcept
{
    return detail::monitor_records<bound_system_t<Application>, Component>();
}

template <typename Application = DefaultApplication> struct Of
{
    template <typename Component>
    [[nodiscard]] static Result<Receipt, Error> report(Assessment assessment) noexcept
    {
        return health::report<Component, Application>(assessment);
    }

    template <typename Component>
    [[nodiscard]] static Result<ProgressReceipt, Error> progress() noexcept
    {
        return health::progress<Component, Application>();
    }

    template <typename Component>
    [[nodiscard]] static Result<SubjectRecord, Error> record() noexcept
    {
        return health::record<Component, Application>();
    }

    template <typename Component> [[nodiscard]] static Result<Receipt, Error> assess() noexcept
    {
        return health::assess<Component, Application>();
    }

    [[nodiscard]] static Result<void, Error> refresh() noexcept
    {
        return health::refresh<Application>();
    }

    [[nodiscard]] static Result<SystemRecord, Error> state() noexcept
    {
        return health::state<Application>();
    }
};

namespace history
{

template <typename Application = DefaultApplication>
[[nodiscard]] Result<HistoryPage, Error> read(HistoryCursor cursor,
                                              std::span<TransitionRecord> destination) noexcept
{
    return detail::read_history<bound_system_t<Application>>(cursor, destination);
}

} // namespace history

} // namespace solar::health
