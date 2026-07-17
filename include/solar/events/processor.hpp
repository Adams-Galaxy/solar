#pragma once

#include <concepts>
#include <utility>

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
concept RecordProcessorResult = requires(RecordView record) { Observer::process(record); } &&
                                VoidResult<decltype(Observer::process(std::declval<RecordView>()))>;

template <typename Observer>
concept RecordProcessor = RecordProcessorVoid<Observer> || RecordProcessorResult<Observer>;

namespace detail
{

template <typename Observer> [[nodiscard]] Result<void> invoke_processor(RecordView record) noexcept
{
    static_assert(RecordProcessor<Observer>,
                  "SOLAR_DIAGNOSTIC_EVENT_PROCESSOR_HANDLER: processor must expose static void "
                  "or Result<void, ErrorType> process(RecordView)");
    if constexpr (RecordProcessorVoid<Observer>) {
        Observer::process(record);
        return {};
    } else {
        auto result = Observer::process(record);
        return result ? Result<void>{}
                      : Result<void>{fail<solar::Error>({.status = status_of(result.error())})};
    }
}

} // namespace detail

} // namespace solar::events
