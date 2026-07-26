#pragma once

#include <string_view>

#include <zephyr/sys/printk.h>

#include "solar/core/status.hpp"
#include "solar/log/declaration.hpp"

namespace solar::log
{

namespace detail
{

[[nodiscard]] constexpr std::string_view origin_name(Origin origin) noexcept
{
    switch (origin) {
    case Origin::Solar:
        return "solar";
    case Origin::Zephyr:
        return "zephyr";
    case Origin::Event:
        return "event";
    case Origin::RedirectedPrint:
        return "print";
    case Origin::Infrastructure:
        return "infrastructure";
    case Origin::Emergency:
        return "emergency";
    }
    return "unknown";
}

template <typename System> [[nodiscard]] std::string_view source_name(RecordView record) noexcept
{
    const auto source = System::LogSourceCatalog::find(record.header.source);
    if (source && !source->get().descriptor.name.empty()) {
        return source->get().descriptor.name;
    }
    return origin_name(record.header.origin);
}

} // namespace detail

struct DefaultZephyrConsoleRenderer
{
    template <typename System>
    [[nodiscard]] static Result<void> render(RecordView record, std::string_view rendered) noexcept
    {
        constexpr Timestamp microseconds_per_second = 1'000'000;
        constexpr Timestamp microseconds_per_millisecond = 1'000;
        const auto seconds = record.header.timestamp / microseconds_per_second;
        const auto milliseconds =
            (record.header.timestamp % microseconds_per_second) / microseconds_per_millisecond;
        const auto source = detail::source_name<System>(record);

        printk("[solar %lld.%03lld] [%.*s] %-7s %.*s\n", static_cast<long long>(seconds),
               static_cast<long long>(milliseconds), static_cast<int>(source.size()), source.data(),
               to_string(record.header.level), static_cast<int>(rendered.size()), rendered.data());
        return {};
    }
};

template <typename Renderer = DefaultZephyrConsoleRenderer> struct ZephyrConsole
{
    static constexpr SinkDescriptor descriptor{
        .name = "solar.log.zephyr-console",
        .description = "Rendered Solar logs on the active Zephyr console",
    };

    template <typename System>
    [[nodiscard]] static Result<void> consume(RecordView record, std::string_view rendered) noexcept
    {
        if constexpr (requires { Renderer::template render<System>(record, rendered); }) {
            return Renderer::template render<System>(record, rendered);
        } else {
            return Renderer::render(record, rendered);
        }
    }
};

} // namespace solar::log
