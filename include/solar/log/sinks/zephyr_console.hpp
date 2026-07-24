#pragma once

#include <string_view>

#include <zephyr/sys/printk.h>

#include "solar/core/status.hpp"
#include "solar/log/declaration.hpp"

namespace solar::log
{

struct ZephyrConsole
{
    static constexpr SinkDescriptor descriptor{
        .name = "solar.log.zephyr-console",
        .description = "Rendered Solar logs on the active Zephyr console",
    };

    [[nodiscard]] static Result<void> consume(RecordView record,
                                               std::string_view rendered) noexcept
    {
        constexpr Timestamp microseconds_per_second = 1'000'000;
        const auto seconds = record.header.timestamp / microseconds_per_second;
        const auto microseconds = record.header.timestamp % microseconds_per_second;

        printk("[solar %lld.%06lld] %-7s %.*s\n", static_cast<long long>(seconds),
               static_cast<long long>(microseconds), to_string(record.header.level),
               static_cast<int>(rendered.size()), rendered.data());
        return {};
    }
};

} // namespace solar::log
