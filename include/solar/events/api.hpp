#pragma once

#include <cstring>
#include <functional>

#include "solar/events/protocol.hpp"

namespace solar::events
{

namespace detail
{

template <typename System, typename Entry> [[nodiscard]] consteval DescriptorView make_view()
{
    using EventT = typename Entry::Declaration;
    using Policies = typename System::EventFacility::template Policies<EventT>;
    using Capture = CaptureTraits<typename Policies::Capture>;
    using Retention = RetentionTraits<typename Policies::Retention>;
    return {
        .local_id = Entry::local_id,
        .descriptor = catalog::descriptor_for_view(descriptor_traits<Tag, EventT>::descriptor),
        .owner = Entry::owner_view(),
        .origin = Entry::origin_kind,
        .payload_size = payload_size_v<EventT>,
        .payload_alignment = payload_alignment_v<EventT>,
        .capture = Capture::kind,
        .retention = Retention::kind,
        .critical_reservation = Retention::reserved_slots,
        .payload_free = payload_free_v<EventT>,
        .isr_compatible = Capture::isr_compatible,
    };
}

template <typename System, typename Entries> struct DescriptorTable;

template <typename System, typename... Entries> struct DescriptorTable<System, TypeList<Entries...>>
{
    inline static constexpr std::array<DescriptorView, sizeof...(Entries)> values{
        make_view<System, Entries>()...};
};

template <typename Observer, typename EventT, typename RouteTag, typename List>
struct FindProcessor;

template <typename Observer, typename EventT, typename RouteTag>
struct FindProcessor<Observer, EventT, RouteTag, TypeList<>>
{
    static constexpr bool found = false;
    using type = void;
};

template <typename Observer, typename EventT, typename RouteTag, typename Head, typename... Tail>
struct FindProcessor<Observer, EventT, RouteTag, TypeList<Head, Tail...>>
{
  private:
    using Traits = processor_traits<Head>;
    static constexpr bool matches = std::is_same_v<typename Traits::ObserverType, Observer> &&
                                    std::is_same_v<typename Traits::EventType, EventT> &&
                                    std::is_same_v<typename Traits::RouteTagType, RouteTag>;
    using Remaining = FindProcessor<Observer, EventT, RouteTag, TypeList<Tail...>>;

  public:
    static constexpr bool found = matches || Remaining::found;
    using type = std::conditional_t<matches, Head, typename Remaining::type>;
};

} // namespace detail

template <Event EventT>
[[nodiscard]] Result<Receipt, Error> observe(const typename EventT::Payload& payload,
                                             ObserveOptions options = {}) noexcept
    requires(!payload_free_v<EventT>)
{
    return frontend::Operation<detail::ObserveFrontend<detail::ObserveMode::Normal>, EventT>::call(
        payload, options);
}

template <Event EventT>
[[nodiscard]] Result<Receipt, Error> observe(ObserveOptions options = {}) noexcept
    requires payload_free_v<EventT>
{
    return frontend::Operation<detail::ObserveFrontend<detail::ObserveMode::Normal>, EventT>::call(
        detail::NoPayload{}, options);
}

template <Event EventT>
[[nodiscard]] Result<Receipt, Error> try_observe(const typename EventT::Payload& payload,
                                                 ObserveOptions options = {}) noexcept
    requires(!payload_free_v<EventT>)
{
    return frontend::Operation<detail::ObserveFrontend<detail::ObserveMode::Try>, EventT>::call(
        payload, options);
}

template <Event EventT>
[[nodiscard]] Result<Receipt, Error> try_observe(ObserveOptions options = {}) noexcept
    requires payload_free_v<EventT>
{
    return frontend::Operation<detail::ObserveFrontend<detail::ObserveMode::Try>, EventT>::call(
        detail::NoPayload{}, options);
}

template <Event EventT>
[[nodiscard]] Result<Receipt, Error> try_observe_isr(const typename EventT::Payload& payload,
                                                     ObserveOptions options = {}) noexcept
    requires(!payload_free_v<EventT>)
{
    return frontend::Operation<detail::ObserveFrontend<detail::ObserveMode::Isr>, EventT>::call(
        payload, options);
}

template <Event EventT>
[[nodiscard]] Result<Receipt, Error> try_observe_isr(ObserveOptions options = {}) noexcept
    requires payload_free_v<EventT>
{
    return frontend::Operation<detail::ObserveFrontend<detail::ObserveMode::Isr>, EventT>::call(
        detail::NoPayload{}, options);
}

template <typename Source, Event EventT, typename Application = DefaultApplication>
[[nodiscard]] Result<Receipt, Error> observe_from(const typename EventT::Payload& payload,
                                                  ObserveOptions options = {}) noexcept
    requires(!payload_free_v<EventT>)
{
    return detail::observe_from<Application, Source, EventT, detail::ObserveMode::Normal>(payload,
                                                                                          options);
}

template <typename Source, Event EventT, typename Application = DefaultApplication>
[[nodiscard]] Result<Receipt, Error> observe_from(ObserveOptions options = {}) noexcept
    requires payload_free_v<EventT>
{
    return detail::observe_from<Application, Source, EventT, detail::ObserveMode::Normal>(
        detail::NoPayload{}, options);
}

template <typename Source, Event EventT, typename Application = DefaultApplication>
[[nodiscard]] Result<Receipt, Error> try_observe_from(const typename EventT::Payload& payload,
                                                      ObserveOptions options = {}) noexcept
    requires(!payload_free_v<EventT>)
{
    return detail::observe_from<Application, Source, EventT, detail::ObserveMode::Try>(payload,
                                                                                       options);
}

template <typename Source, Event EventT, typename Application = DefaultApplication>
[[nodiscard]] Result<Receipt, Error> try_observe_from(ObserveOptions options = {}) noexcept
    requires payload_free_v<EventT>
{
    return detail::observe_from<Application, Source, EventT, detail::ObserveMode::Try>(
        detail::NoPayload{}, options);
}

template <typename Source, Event EventT, typename Application = DefaultApplication>
[[nodiscard]] Result<Receipt, Error> try_observe_isr_from(const typename EventT::Payload& payload,
                                                          ObserveOptions options = {}) noexcept
    requires(!payload_free_v<EventT>)
{
    return detail::observe_from<Application, Source, EventT, detail::ObserveMode::Isr>(payload,
                                                                                       options);
}

template <typename Source, Event EventT, typename Application = DefaultApplication>
[[nodiscard]] Result<Receipt, Error> try_observe_isr_from(ObserveOptions options = {}) noexcept
    requires payload_free_v<EventT>
{
    return detail::observe_from<Application, Source, EventT, detail::ObserveMode::Isr>(
        detail::NoPayload{}, options);
}

template <Event EventT> [[nodiscard]] Result<EventRecord, Error> record() noexcept
{
    return frontend::Operation<detail::RecordFrontend, EventT>::call();
}

template <Event EventT, typename Application = DefaultApplication>
[[nodiscard]] Result<ConditionRecord, Error> condition(SourceId source) noexcept
{
    if constexpr (!enabled) {
        return fail(detail::frontend_error(frontend::Error::Disabled, Operation::Query));
    } else {
        using System = bound_system_t<Application>;
        static_assert(System::EventCatalog::template contains<EventT>,
                      "SOLAR_DIAGNOSTIC_EVENT_CONDITION_NOT_REGISTERED: condition query "
                      "references an unregistered event");
        return detail::condition_record<System, EventT>(source);
    }
}

template <typename Observer, Event EventT, typename RouteTag = DefaultProcessorTag,
          typename Application = DefaultApplication>
[[nodiscard]] Result<ProcessorRecord, Error> processor_record() noexcept
{
    if constexpr (!enabled) {
        return fail(detail::frontend_error(frontend::Error::Disabled, Operation::Query));
    } else {
        using System = bound_system_t<Application>;
        using Lookup = detail::FindProcessor<Observer, EventT, RouteTag,
                                             typename System::EventFacility::ProcessorTypes>;
        if constexpr (Lookup::found) {
            return detail::processor_record<System, typename Lookup::type>();
        } else {
            static_assert(!frontend::strict,
                          "SOLAR_DIAGNOSTIC_STRICT_UNREGISTERED_EVENT_PROCESSOR_QUERY: queried "
                          "processor route is absent from the bound catalog");
            return fail(Error{.status = Status::NotFound,
                              .reason = Reason::NotRegistered,
                              .operation = Operation::Query});
        }
    }
}

template <typename Application = DefaultApplication>
[[nodiscard]] constexpr auto descriptors() noexcept
{
    if constexpr (!enabled) {
        return std::span<const DescriptorView>{};
    } else {
        using System = bound_system_t<Application>;
        using Table = detail::DescriptorTable<System, typename System::EventCatalog::EntryTypes>;
        return std::span<const DescriptorView>{Table::values};
    }
}

template <Event EventT, typename Application = DefaultApplication>
[[nodiscard]] Result<std::reference_wrapper<const DescriptorView>, catalog::LookupError>
descriptor() noexcept
{
    if constexpr (!enabled) {
        return fail(catalog::LookupError::Unavailable);
    } else {
        using System = bound_system_t<Application>;
        if constexpr (System::EventCatalog::template contains<EventT>) {
            using Table =
                detail::DescriptorTable<System, typename System::EventCatalog::EntryTypes>;
            return std::cref(
                Table::values[System::EventCatalog::template Entry<EventT>::local_id.index()]);
        } else {
            static_assert(!frontend::strict,
                          "SOLAR_DIAGNOSTIC_STRICT_UNREGISTERED_EVENT_QUERY: queried event is "
                          "absent from the bound catalog");
            return fail(catalog::LookupError::Unavailable);
        }
    }
}

template <Event EventT, typename Application = DefaultApplication>
[[nodiscard]] Result<typename EventT::Payload, Error> decode(RecordView record) noexcept
    requires(!payload_free_v<EventT>)
{
    const auto expected = detail::record_from_binding<EventT, Application>();
    if (!expected) {
        return fail(expected.error());
    }
    if (record.header.event != expected->event ||
        record.payload.size() != sizeof(typename EventT::Payload) ||
        record.header.schema_version != descriptor_traits<Tag, EventT>::descriptor.version) {
        return fail(Error{.status = Status::ProtocolError,
                          .reason = Reason::DecodeMismatch,
                          .operation = Operation::Query,
                          .event = record.header.event});
    }
    typename EventT::Payload payload{};
    std::memcpy(&payload, record.payload.data(), sizeof(payload));
    return payload;
}

template <typename Application = DefaultApplication>
[[nodiscard]] FacilityRecord facility_record() noexcept
{
    if constexpr (!enabled) {
        return {.last_status = Status::NotSupported};
    } else {
        using System = bound_system_t<Application>;
        return detail::facility_record<System>();
    }
}

namespace history
{

template <int = 0, typename Application = DefaultApplication>
[[nodiscard]] HistoryPage read(Cursor cursor, std::span<Record> output) noexcept
{
    if constexpr (!enabled) {
        return {.next = cursor};
    } else {
        using System = bound_system_t<Application>;
        return detail::read_history<System>(cursor, output);
    }
}

template <Event EventT, typename Application = DefaultApplication>
[[nodiscard]] HistoryPage read(Cursor cursor, std::span<Record> output) noexcept
{
    if constexpr (!enabled) {
        return {.next = cursor};
    } else {
        using System = bound_system_t<Application>;
        static_assert(System::EventCatalog::template contains<EventT>,
                      "SOLAR_DIAGNOSTIC_EVENT_HISTORY_NOT_REGISTERED: history query references "
                      "an unregistered event");
        return detail::read_history<System>(cursor, output,
                                            System::EventCatalog::template Entry<EventT>::local_id);
    }
}

template <int = 0, typename Application = DefaultApplication>
[[nodiscard]] Result<Record, Error> latest() noexcept
{
    if constexpr (!enabled) {
        return fail(detail::frontend_error(frontend::Error::Disabled, Operation::Query));
    } else {
        using System = bound_system_t<Application>;
        return detail::latest_history<System>(std::nullopt);
    }
}

template <Event EventT, typename Application = DefaultApplication>
[[nodiscard]] Result<Record, Error> latest() noexcept
{
    if constexpr (!enabled) {
        return fail(detail::frontend_error(frontend::Error::Disabled, Operation::Query));
    } else {
        using System = bound_system_t<Application>;
        static_assert(System::EventCatalog::template contains<EventT>,
                      "SOLAR_DIAGNOSTIC_EVENT_HISTORY_NOT_REGISTERED: latest query references an "
                      "unregistered event");
        return detail::latest_history<System>(
            System::EventCatalog::template Entry<EventT>::local_id);
    }
}

} // namespace history

template <typename Application> struct Of
{
    template <Event EventT>
    [[nodiscard]] static Result<Receipt, Error> observe(const typename EventT::Payload& payload,
                                                        ObserveOptions options = {}) noexcept
        requires(!payload_free_v<EventT>)
    {
        return frontend::Operation<detail::ObserveFrontend<detail::ObserveMode::Normal>, EventT,
                                   Application>::call(payload, options);
    }

    template <Event EventT>
    [[nodiscard]] static Result<Receipt, Error> observe(ObserveOptions options = {}) noexcept
        requires payload_free_v<EventT>
    {
        return frontend::Operation<detail::ObserveFrontend<detail::ObserveMode::Normal>, EventT,
                                   Application>::call(detail::NoPayload{}, options);
    }

    template <Event EventT> [[nodiscard]] static Result<EventRecord, Error> record() noexcept
    {
        return frontend::Operation<detail::RecordFrontend, EventT, Application>::call();
    }

    template <Event EventT>
    [[nodiscard]] static Result<typename EventT::Payload, Error> decode(RecordView record) noexcept
        requires(!payload_free_v<EventT>)
    {
        return events::decode<EventT, Application>(record);
    }

    template <Event EventT>
    [[nodiscard]] static Result<ConditionRecord, Error> condition(SourceId source) noexcept
    {
        if constexpr (!enabled) {
            return fail(detail::frontend_error(frontend::Error::Disabled, Operation::Query));
        } else {
            using System = bound_system_t<Application>;
            static_assert(System::EventCatalog::template contains<EventT>,
                          "SOLAR_DIAGNOSTIC_EVENT_CONDITION_NOT_REGISTERED: condition query "
                          "references an unregistered event");
            return detail::condition_record<System, EventT>(source);
        }
    }

    template <Event EventT>
    [[nodiscard]] static Result<Receipt, Error> try_observe(const typename EventT::Payload& payload,
                                                            ObserveOptions options = {}) noexcept
        requires(!payload_free_v<EventT>)
    {
        return frontend::Operation<detail::ObserveFrontend<detail::ObserveMode::Try>, EventT,
                                   Application>::call(payload, options);
    }

    template <Event EventT>
    [[nodiscard]] static Result<Receipt, Error> try_observe(ObserveOptions options = {}) noexcept
        requires payload_free_v<EventT>
    {
        return frontend::Operation<detail::ObserveFrontend<detail::ObserveMode::Try>, EventT,
                                   Application>::call(detail::NoPayload{}, options);
    }

    template <Event EventT>
    [[nodiscard]] static Result<Receipt, Error>
    try_observe_isr(const typename EventT::Payload& payload, ObserveOptions options = {}) noexcept
        requires(!payload_free_v<EventT>)
    {
        return frontend::Operation<detail::ObserveFrontend<detail::ObserveMode::Isr>, EventT,
                                   Application>::call(payload, options);
    }

    template <Event EventT>
    [[nodiscard]] static Result<Receipt, Error>
    try_observe_isr(ObserveOptions options = {}) noexcept
        requires payload_free_v<EventT>
    {
        return frontend::Operation<detail::ObserveFrontend<detail::ObserveMode::Isr>, EventT,
                                   Application>::call(detail::NoPayload{}, options);
    }

    template <typename Source, Event EventT>
    [[nodiscard]] static Result<Receipt, Error>
    observe_from(const typename EventT::Payload& payload, ObserveOptions options = {}) noexcept
        requires(!payload_free_v<EventT>)
    {
        return detail::observe_from<Application, Source, EventT, detail::ObserveMode::Normal>(
            payload, options);
    }

    template <typename Source, Event EventT>
    [[nodiscard]] static Result<Receipt, Error> observe_from(ObserveOptions options = {}) noexcept
        requires payload_free_v<EventT>
    {
        return detail::observe_from<Application, Source, EventT, detail::ObserveMode::Normal>(
            detail::NoPayload{}, options);
    }

    template <typename Source, Event EventT>
    [[nodiscard]] static Result<Receipt, Error>
    try_observe_from(const typename EventT::Payload& payload, ObserveOptions options = {}) noexcept
        requires(!payload_free_v<EventT>)
    {
        return detail::observe_from<Application, Source, EventT, detail::ObserveMode::Try>(payload,
                                                                                           options);
    }

    template <typename Source, Event EventT>
    [[nodiscard]] static Result<Receipt, Error>
    try_observe_from(ObserveOptions options = {}) noexcept
        requires payload_free_v<EventT>
    {
        return detail::observe_from<Application, Source, EventT, detail::ObserveMode::Try>(
            detail::NoPayload{}, options);
    }

    template <typename Source, Event EventT>
    [[nodiscard]] static Result<Receipt, Error>
    try_observe_isr_from(const typename EventT::Payload& payload,
                         ObserveOptions options = {}) noexcept
        requires(!payload_free_v<EventT>)
    {
        return detail::observe_from<Application, Source, EventT, detail::ObserveMode::Isr>(payload,
                                                                                           options);
    }

    template <typename Source, Event EventT>
    [[nodiscard]] static Result<Receipt, Error>
    try_observe_isr_from(ObserveOptions options = {}) noexcept
        requires payload_free_v<EventT>
    {
        return detail::observe_from<Application, Source, EventT, detail::ObserveMode::Isr>(
            detail::NoPayload{}, options);
    }

    [[nodiscard]] static constexpr auto descriptors() noexcept
    {
        if constexpr (!enabled) {
            return std::span<const DescriptorView>{};
        } else {
            using System = bound_system_t<Application>;
            using Table =
                detail::DescriptorTable<System, typename System::EventCatalog::EntryTypes>;
            return std::span<const DescriptorView>{Table::values};
        }
    }

    template <Event EventT>
    [[nodiscard]] static Result<std::reference_wrapper<const DescriptorView>, catalog::LookupError>
    descriptor() noexcept
    {
        if constexpr (!enabled) {
            return fail(catalog::LookupError::Unavailable);
        } else {
            using System = bound_system_t<Application>;
            if constexpr (System::EventCatalog::template contains<EventT>) {
                using Table =
                    detail::DescriptorTable<System, typename System::EventCatalog::EntryTypes>;
                return std::cref(
                    Table::values[System::EventCatalog::template Entry<EventT>::local_id.index()]);
            } else {
                static_assert(!frontend::strict,
                              "SOLAR_DIAGNOSTIC_STRICT_UNREGISTERED_EVENT_QUERY: queried event is "
                              "absent from the bound catalog");
                return fail(catalog::LookupError::Unavailable);
            }
        }
    }

    template <typename Observer, Event EventT, typename RouteTag = DefaultProcessorTag>
    [[nodiscard]] static Result<ProcessorRecord, Error> processor_record() noexcept
    {
        if constexpr (!enabled) {
            return fail(detail::frontend_error(frontend::Error::Disabled, Operation::Query));
        } else {
            using System = bound_system_t<Application>;
            using Lookup = detail::FindProcessor<Observer, EventT, RouteTag,
                                                 typename System::EventFacility::ProcessorTypes>;
            if constexpr (Lookup::found) {
                return detail::processor_record<System, typename Lookup::type>();
            } else {
                static_assert(!frontend::strict,
                              "SOLAR_DIAGNOSTIC_STRICT_UNREGISTERED_EVENT_PROCESSOR_QUERY: "
                              "queried processor route is absent from the bound catalog");
                return fail(Error{.status = Status::NotFound,
                                  .reason = Reason::NotRegistered,
                                  .operation = Operation::Query});
            }
        }
    }

    [[nodiscard]] static FacilityRecord facility_record() noexcept
    {
        if constexpr (!enabled) {
            return {.last_status = Status::NotSupported};
        } else {
            using System = bound_system_t<Application>;
            return detail::facility_record<System>();
        }
    }

    [[nodiscard]] static HistoryPage read_history(Cursor cursor, std::span<Record> output) noexcept
    {
        if constexpr (!enabled) {
            return {.next = cursor};
        } else {
            using System = bound_system_t<Application>;
            return detail::read_history<System>(cursor, output);
        }
    }

    template <Event EventT>
    [[nodiscard]] static HistoryPage read_history(Cursor cursor, std::span<Record> output) noexcept
    {
        if constexpr (!enabled) {
            return {.next = cursor};
        } else {
            using System = bound_system_t<Application>;
            static_assert(System::EventCatalog::template contains<EventT>,
                          "SOLAR_DIAGNOSTIC_EVENT_HISTORY_NOT_REGISTERED: history query "
                          "references an unregistered event");
            return detail::read_history<System>(
                cursor, output, System::EventCatalog::template Entry<EventT>::local_id);
        }
    }

    [[nodiscard]] static Result<Record, Error> latest() noexcept
    {
        if constexpr (!enabled) {
            return fail(detail::frontend_error(frontend::Error::Disabled, Operation::Query));
        } else {
            using System = bound_system_t<Application>;
            return detail::latest_history<System>(std::nullopt);
        }
    }

    template <Event EventT> [[nodiscard]] static Result<Record, Error> latest() noexcept
    {
        if constexpr (!enabled) {
            return fail(detail::frontend_error(frontend::Error::Disabled, Operation::Query));
        } else {
            using System = bound_system_t<Application>;
            static_assert(System::EventCatalog::template contains<EventT>,
                          "SOLAR_DIAGNOSTIC_EVENT_HISTORY_NOT_REGISTERED: latest query references "
                          "an unregistered event");
            return detail::latest_history<System>(
                System::EventCatalog::template Entry<EventT>::local_id);
        }
    }
};

} // namespace solar::events
