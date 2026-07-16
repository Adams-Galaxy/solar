#pragma once

#include <array>
#include <cstdio>
#include <span>
#include <string_view>

#include "solar/events/api.hpp"
#include "solar/log/api.hpp"

namespace solar::events::log
{

struct Summary
{
    [[nodiscard]] static Result<std::string_view> render(RecordView record,
                                                         std::span<char> output) noexcept
    {
        const auto size = std::snprintf(output.data(), output.size(),
                                        "event %u occurrence %llu count %u",
                                        static_cast<unsigned>(record.header.event.value),
                                        static_cast<unsigned long long>(record.header.sequence),
                                        static_cast<unsigned>(record.header.occurrence_count));
        if (size < 0 || static_cast<std::size_t>(size) >= output.size()) {
            return fail(Status::MessageTooLarge);
        }
        return std::string_view{output.data(), static_cast<std::size_t>(size)};
    }
};

template <typename EventT, typename SourceT, solar::log::Level LogLevel, typename Renderer = Summary>
struct On
{
    using EventRole = InfrastructureObserver;
    using EventType = EventT;

    [[nodiscard]] static Result<void> process(RecordView record) noexcept
    {
        std::array<char, CONFIG_SOLAR_LOG_MAX_STRING_BYTES> text{};
        auto rendered = Renderer::render(record, text);
        if (!rendered) {
            return fail(rendered.error());
        }
        auto captured = solar::log::detail::emit<
            SourceT, solar::log::domain::Unclassified, LogLevel, solar::log::Operation::TryCapture,
            DefaultApplication, solar::log::Origin::Event>({}, "{}", *rendered);
        return captured ? Result<void>{} : Result<void>{fail(captured.error().status)};
    }
};

template <typename Adapter> struct ProcessorFor;

template <typename EventT, typename SourceT, solar::log::Level LogLevel, typename Renderer>
struct ProcessorFor<On<EventT, SourceT, LogLevel, Renderer>>
{
    using type = Process<EventT, On<EventT, SourceT, LogLevel, Renderer>>;
};

template <typename... AdapterTypes> struct Adapters
{
    using Processors = solar::events::Processors<typename ProcessorFor<AdapterTypes>::type...>;
};

} // namespace solar::events::log

template <typename Component>
struct solar::events::event_log_extensions<Component,
                                            std::void_t<typename Component::EventLogs>>
{
    using type = typename Component::EventLogs::Processors;
};
