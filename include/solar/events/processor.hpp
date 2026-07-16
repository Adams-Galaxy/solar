#pragma once

#include <concepts>

#include "solar/events/declaration.hpp"

namespace solar::events
{

template <typename Store>
concept PersistentStore = requires(RecordView record) {
    { Store::initialize() } -> std::same_as<Result<void>>;
    { Store::write(record) } -> std::same_as<Result<void>>;
};

template <typename Processor> struct processor_traits
{
    static constexpr bool valid = false;
};

template <Event EventT, typename Observer, typename RouteTag>
struct processor_traits<Process<EventT, Observer, RouteTag>>
{
    using EventType = EventT;
    using ObserverType = Observer;
    using RouteTagType = RouteTag;
    static constexpr bool valid = true;
};

template <typename Observer>
concept RecordProcessorVoid = requires(RecordView record) {
    { Observer::process(record) } -> std::same_as<void>;
};

template <typename Observer>
concept RecordProcessorStatus = requires(RecordView record) {
    { Observer::process(record) } -> std::same_as<Status>;
};

template <typename Observer>
concept RecordProcessorResult = requires(RecordView record) {
    { Observer::process(record) } -> std::same_as<Result<void>>;
};

template <typename Observer>
concept RecordProcessor = RecordProcessorVoid<Observer> || RecordProcessorStatus<Observer> ||
                          RecordProcessorResult<Observer>;

namespace detail
{

template <typename Observer> [[nodiscard]] Result<void> invoke_processor(RecordView record) noexcept
{
    static_assert(RecordProcessor<Observer>,
                  "SOLAR_DIAGNOSTIC_EVENT_PROCESSOR_HANDLER: processor must expose static void, "
                  "Status, or Result<void> process(RecordView)");
    if constexpr (RecordProcessorVoid<Observer>) {
        Observer::process(record);
        return {};
    } else if constexpr (RecordProcessorStatus<Observer>) {
        const auto status = Observer::process(record);
        return status == Status::Ok ? Result<void>{} : Result<void>{fail(status)};
    } else {
        return Observer::process(record);
    }
}

} // namespace detail

} // namespace solar::events
