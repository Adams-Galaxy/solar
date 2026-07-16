#pragma once

#include "solar/events/runtime.hpp"
#include "solar/lifecycle/protocol.hpp"
#include "solar/system/frontend.hpp"

namespace solar::events::detail
{

struct NoPayload
{};

template <typename EventT, bool PayloadFree = payload_free_v<EventT>> struct ObservationArgument
{
    using type = const typename EventT::Payload&;
};

template <typename EventT> struct ObservationArgument<EventT, true>
{
    using type = NoPayload;
};

template <typename EventT>
using observation_argument_t = typename ObservationArgument<EventT>::type;

[[nodiscard]] constexpr Error frontend_error(frontend::Error error, Operation operation) noexcept
{
    switch (error) {
    case frontend::Error::NotReady:
        return {.status = Status::NotReady, .reason = Reason::NotReady, .operation = operation};
    case frontend::Error::Disabled:
        return {.status = Status::NotSupported, .reason = Reason::Disabled, .operation = operation};
    case frontend::Error::NotRegistered:
        return {
            .status = Status::NotFound, .reason = Reason::NotRegistered, .operation = operation};
    }
    return {.status = Status::Error, .reason = Reason::InternalInvariant, .operation = operation};
}

enum class ObserveMode : std::uint8_t
{
    Normal,
    Try,
    Isr,
};

template <ObserveMode Mode> struct ObserveFrontend
{
    using CatalogTag = Tag;
    template <typename EventT>
    using Signature = Result<Receipt, Error>(observation_argument_t<EventT>, ObserveOptions);

    template <typename System, typename EventT>
    [[nodiscard]] static Result<Receipt, Error> invoke(observation_argument_t<EventT> payload,
                                                       ObserveOptions options) noexcept
    {
        constexpr auto owner = System::EventCatalog::template Entry<EventT>::owner_view();
        const auto source = source_from_owner(owner);
        if constexpr (Mode == ObserveMode::Isr) {
            using Policies = typename System::EventFacility::template Policies<EventT>;
            if constexpr (!CaptureTraits<typename Policies::Capture>::isr_compatible) {
                return fail(make_error<System, EventT>(Operation::ObserveIsr, Status::NotSupported,
                                                       Reason::IsrUnsupported, source));
            } else {
                return capture_event<System, EventT, true, Operation::ObserveIsr>(payload, source,
                                                                                  options);
            }
        } else {
            constexpr auto operation =
                Mode == ObserveMode::Try ? Operation::TryObserve : Operation::Observe;
            return capture_event<System, EventT, false, operation>(payload, source, options);
        }
    }

    template <typename System, typename EventT> static consteval void validate()
    {
        using Policies = typename System::EventFacility::template Policies<EventT>;
        if constexpr (Mode == ObserveMode::Isr) {
            static_assert(CaptureTraits<typename Policies::Capture>::isr_compatible,
                          "SOLAR_DIAGNOSTIC_EVENT_ISR_POLICY: event capture policy is not ISR "
                          "compatible");
        }
    }

    template <typename EventT>
    [[nodiscard]] static Result<Receipt, Error> unavailable(frontend::Error error) noexcept
    {
        constexpr auto operation = Mode == ObserveMode::Isr   ? Operation::ObserveIsr
                                   : Mode == ObserveMode::Try ? Operation::TryObserve
                                                              : Operation::Observe;
        return fail(frontend_error(error, operation));
    }
};

struct RecordFrontend
{
    using CatalogTag = Tag;
    template <typename EventT> using Signature = Result<EventRecord, Error>();

    template <typename System, typename EventT>
    [[nodiscard]] static Result<EventRecord, Error> invoke() noexcept
    {
        return event_record<System, EventT>();
    }

    template <typename EventT>
    [[nodiscard]] static Result<EventRecord, Error> unavailable(frontend::Error error) noexcept
    {
        return fail(frontend_error(error, Operation::Query));
    }
};

template <typename EventT, typename Application = DefaultApplication>
[[nodiscard]] Result<EventRecord, Error> record_from_binding() noexcept
{
    using Signature = typename RecordFrontend::template Signature<EventT>;
    return frontend::detail::OperationSlot<RecordFrontend, EventT, Application, Signature>::call();
}

template <typename System, typename Application> void bind_event_frontends() noexcept
{
    if constexpr (enabled) {
        frontend::bind_catalog<System, ObserveFrontend<ObserveMode::Normal>, Application>();
        frontend::bind_catalog<System, ObserveFrontend<ObserveMode::Try>, Application>();
        frontend::bind_catalog<System, ObserveFrontend<ObserveMode::Isr>, Application>();
        frontend::bind_catalog<System, RecordFrontend, Application>();
    } else {
        frontend::bind_disabled<ObserveFrontend<ObserveMode::Normal>, Application>();
        frontend::bind_disabled<ObserveFrontend<ObserveMode::Try>, Application>();
        frontend::bind_disabled<ObserveFrontend<ObserveMode::Isr>, Application>();
        frontend::bind_disabled<RecordFrontend, Application>();
    }
}

template <typename Application, typename Source, typename EventT, ObserveMode Mode>
[[nodiscard]] Result<Receipt, Error> observe_from(observation_argument_t<EventT> payload,
                                                  ObserveOptions options) noexcept
{
    if constexpr (!enabled) {
        constexpr auto operation = Mode == ObserveMode::Isr   ? Operation::ObserveIsr
                                   : Mode == ObserveMode::Try ? Operation::TryObserve
                                                              : Operation::Observe;
        return fail(frontend_error(frontend::Error::Disabled, operation));
    } else {
        using System = bound_system_t<Application>;
        static_assert(System::EventCatalog::template contains<EventT>,
                      "SOLAR_DIAGNOSTIC_EVENT_NOT_REGISTERED: source override references an "
                      "unregistered event");
        static_assert(System::Catalogs::template Of<component::Tag>::template contains<Source>,
                      "SOLAR_DIAGNOSTIC_EVENT_SOURCE_UNREGISTERED: source override must be a "
                      "registered component");
        constexpr auto source = SourceId{
            .kind = SourceKind::Component,
            .component =
                System::Catalogs::template Of<component::Tag>::template Entry<Source>::local_id,
        };
        if constexpr (Mode == ObserveMode::Isr) {
            using Policies = typename System::EventFacility::template Policies<EventT>;
            static_assert(CaptureTraits<typename Policies::Capture>::isr_compatible,
                          "SOLAR_DIAGNOSTIC_EVENT_ISR_POLICY: event capture policy is not ISR "
                          "compatible");
            return capture_event<System, EventT, true, Operation::ObserveIsr>(payload, source,
                                                                              options);
        } else {
            constexpr auto operation =
                Mode == ObserveMode::Try ? Operation::TryObserve : Operation::Observe;
            return capture_event<System, EventT, false, operation>(payload, source, options);
        }
    }
}

} // namespace solar::events::detail

template <> struct solar::lifecycle::ApplicationBindingProtocol<solar::events::Tag>
{
    template <typename System, typename Application> static void bind() noexcept
    {
        solar::events::detail::bind_event_frontends<System, Application>();
    }
};

template <> struct solar::lifecycle::CatalogActivationProtocol<solar::events::Tag>
{
    template <typename System>
    static constexpr bool participates = solar::events::enabled && System::EventCatalog::size != 0;

    template <typename> [[nodiscard]] static solar::Result<void> commit() noexcept
    {
        return {};
    }

    template <typename System>
    [[nodiscard]] static solar::lifecycle::Failure failure(solar::lifecycle::Operation operation,
                                                           solar::Status status) noexcept
    {
        using Facility = typename System::EventFacility;
        return {
            .component = System::Catalogs::template Of<solar::component::Tag>::template Entry<
                Facility>::local_id,
            .category = solar::lifecycle::ComponentCategory::Facility,
            .operation = operation,
            .status = status,
            .primary = true,
        };
    }

    template <typename System> static void activate() noexcept
    {
        if constexpr (solar::events::enabled) {
            System::EventFacility::template activate_runtime<System>();
        }
    }
};
