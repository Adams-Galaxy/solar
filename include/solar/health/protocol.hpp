#pragma once

#include "solar/health/runtime.hpp"
#include "solar/lifecycle/protocol.hpp"
#include "solar/system/frontend.hpp"

namespace solar::health::detail
{

[[nodiscard]] constexpr Error frontend_error(frontend::Error error, Operation operation) noexcept
{
    switch (error) {
    case frontend::Error::NotReady:
        return make_error(Status::NotReady, Reason::NotReady, operation);
    case frontend::Error::Disabled:
        return make_error(Status::NotSupported, Reason::Disabled, operation);
    case frontend::Error::NotRegistered:
        return make_error(Status::NotFound, Reason::NotRegistered, operation);
    }
    return make_error(Status::Error, Reason::InternalInvariant, operation);
}

template <typename System, typename Component, MonitorKind Kind>
inline constexpr bool has_monitor_kind_v = [] {
    bool found{};
    for_each_type<typename System::HealthMonitorCatalog::EntryTypes>([&]<typename Entry> {
        using Monitor = typename Entry::Declaration;
        if constexpr (std::is_same_v<typename Monitor::Subject, Component> &&
                      monitor_kind<typename Monitor::DeclarationType>() == Kind) {
            found = true;
        }
    });
    return found;
}();

struct ReportFrontend
{
    using CatalogTag = component::Tag;
    template <typename> using Signature = Result<Receipt, Error>(Assessment);

    template <typename System, typename Component>
    [[nodiscard]] static Result<Receipt, Error> invoke(Assessment assessment) noexcept
    {
        return detail::report<System, Component>(assessment, EvidenceQuality::Reported);
    }

    template <typename>
    [[nodiscard]] static Result<Receipt, Error> unavailable(frontend::Error error) noexcept
    {
        return fail<Error>(frontend_error(error, Operation::Report));
    }
};

struct ProgressFrontend
{
    using CatalogTag = component::Tag;
    template <typename> using Signature = Result<ProgressReceipt, Error>();

    template <typename System, typename Component>
    [[nodiscard]] static Result<ProgressReceipt, Error> invoke() noexcept
    {
        if constexpr (!has_monitor_kind_v<System, Component, MonitorKind::Progress>) {
            constexpr auto subject =
                System::Catalogs::template Of<component::Tag>::template Entry<Component>::local_id;
            return fail<Error>(
                make_error(Status::NotFound, Reason::NotRegistered, Operation::Progress, subject));
        } else {
            return detail::progress<System, Component>();
        }
    }

    template <typename System, typename Component> static consteval void validate()
    {
        static_assert(has_monitor_kind_v<System, Component, MonitorKind::Progress>,
                      "SOLAR_DIAGNOSTIC_HEALTH_PROGRESS_NOT_DECLARED: progress<Component>() "
                      "requires a Progress monitor in Component::Health::Checks");
    }

    template <typename>
    [[nodiscard]] static Result<ProgressReceipt, Error> unavailable(frontend::Error error) noexcept
    {
        return fail<Error>(frontend_error(error, Operation::Progress));
    }
};

struct IsrReportFrontend
{
    using CatalogTag = component::Tag;
    template <typename> using Signature = Result<void, Error>(CompactObservation);

    template <typename System, typename Component>
    [[nodiscard]] static Result<void, Error> invoke(CompactObservation observation) noexcept
    {
        return detail::report_isr<System, Component>(observation);
    }

    template <typename>
    [[nodiscard]] static Result<void, Error> unavailable(frontend::Error error) noexcept
    {
        return fail<Error>(frontend_error(error, Operation::ReportIsr));
    }
};

struct RecordFrontend
{
    using CatalogTag = component::Tag;
    template <typename> using Signature = Result<SubjectRecord, Error>();

    template <typename System, typename Component>
    [[nodiscard]] static Result<SubjectRecord, Error> invoke() noexcept
    {
        return detail::subject_record<System, Component>();
    }

    template <typename>
    [[nodiscard]] static Result<SubjectRecord, Error> unavailable(frontend::Error error) noexcept
    {
        return fail<Error>(frontend_error(error, Operation::Query));
    }
};

struct AssessFrontend
{
    using CatalogTag = component::Tag;
    template <typename> using Signature = Result<Receipt, Error>();

    template <typename System, typename Component>
    [[nodiscard]] static Result<Receipt, Error> invoke() noexcept
    {
        return detail::assess_component<System, Component>();
    }

    template <typename>
    [[nodiscard]] static Result<Receipt, Error> unavailable(frontend::Error error) noexcept
    {
        return fail<Error>(frontend_error(error, Operation::Assess));
    }
};

template <typename System, typename Application> void bind_health_frontends() noexcept
{
    if constexpr (enabled) {
        frontend::bind_catalog<System, ReportFrontend, Application>();
        frontend::bind_catalog<System, ProgressFrontend, Application>();
        frontend::bind_catalog<System, IsrReportFrontend, Application>();
        frontend::bind_catalog<System, RecordFrontend, Application>();
        frontend::bind_catalog<System, AssessFrontend, Application>();
    } else {
        frontend::bind_disabled<ReportFrontend, Application>();
        frontend::bind_disabled<ProgressFrontend, Application>();
        frontend::bind_disabled<IsrReportFrontend, Application>();
        frontend::bind_disabled<RecordFrontend, Application>();
        frontend::bind_disabled<AssessFrontend, Application>();
    }
}

} // namespace solar::health::detail

template <> struct solar::lifecycle::ApplicationBindingProtocol<solar::component::Tag>
{
    template <typename System, typename Application> static void bind() noexcept
    {
        solar::health::detail::bind_health_frontends<System, Application>();
    }
};
