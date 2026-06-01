#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>

#include "solar/SolarConfig_default.h"
#include "solar/core.hpp"
#include "solar/events/filter.hpp"
#include "solar/events/format.hpp"

namespace solar::events
{

/**
 * @brief Type-level event sink list for an event facility.
 */
template <typename... SinkTypes>
struct Sinks
{
};

struct SinkStats
{
    /// Records formatted and written by this sink.
    std::uint32_t accepted = 0;
    /// Records skipped by this sink's filter.
    std::uint32_t filtered = 0;
    /// Records this sink failed to format or write.
    std::uint32_t failed = 0;
};

/**
 * @brief Direct event sink using a writer, formatter, and filter.
 *
 * The writer only needs `write(const char*, std::size_t)`. This deliberately
 * matches the logging writer surface so serial, ring-buffer, persistent, and
 * future Remote-backed sinks can share machinery.
 */
template <typename NameT, typename WriterT, typename FormatT = CompactFormat, typename FilterT = Filter<>>
class Sink
{
public:
    using Name = NameT;
    using Writer = WriterT;
    using Format = FormatT;
    using FilterType = FilterT;

    /**
     * @brief Initialize the sink writer without a Solar context.
     */
    Status init()
    {
        stats_ = {};
        if constexpr (requires(WriterT writer) { writer.init(); })
        {
            return writer_.init();
        }
        else
        {
            return Status::Ok;
        }
    }

    template <typename ContextT>
    /**
     * @brief Initialize the sink writer with a Solar context when supported.
     */
    Status init(ContextT &ctx)
    {
        stats_ = {};
        if constexpr (requires(WriterT writer, ContextT &context) { writer.init(context); })
        {
            return writer_.init(ctx);
        }
        else if constexpr (requires(WriterT writer) { writer.init(); })
        {
            return writer_.init();
        }
        else
        {
            return Status::Ok;
        }
    }

    void reset()
    {
        stats_ = {};
        if constexpr (requires(WriterT writer) { writer.clear(); })
        {
            writer_.clear();
        }
    }

    Status publish(Record const &record)
    {
        if (!FilterT::accepts(record))
        {
            ++stats_.filtered;
            return Status::Ok;
        }

        std::array<char, configSOLAR_EVENT_FORMAT_BUFFER_BYTES> formatted{};
        const std::size_t len = FormatT::format(record, formatted.data(), formatted.size());
        if (len == 0)
        {
            ++stats_.failed;
            return Status::Error;
        }

        const Status status = writer_.write(formatted.data(), len);
        if (status == Status::Ok)
        {
            ++stats_.accepted;
        }
        else
        {
            ++stats_.failed;
        }
        return status;
    }

    void flush()
    {
        if constexpr (requires(WriterT writer) { writer.flush(); })
        {
            writer_.flush();
        }
    }

    WriterT &writer()
    {
        return writer_;
    }

    WriterT const &writer() const
    {
        return writer_;
    }

    SinkStats stats() const
    {
        return stats_;
    }

private:
    WriterT writer_{};
    SinkStats stats_{};
};

} // namespace solar::events
