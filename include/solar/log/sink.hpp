#pragma once

#include <array>
#include <concepts>
#include <cstddef>

#include "solar/SolarConfig_default.h"
#include "solar/core.hpp"
#include "solar/log/filter.hpp"
#include "solar/log/format.hpp"
#include "solar/log/record.hpp"

namespace solar::log
{

/**
 * @brief Type-level list of logger sink objects.
 */
template <typename... SinkTypes>
struct Sinks
{
};

struct SinkStats
{
    /// Records written by this sink.
    std::uint32_t accepted = 0;
    /// Records skipped by the sink filter.
    std::uint32_t filtered = 0;
    /// Records this sink failed to format or write.
    std::uint32_t failed = 0;
};

/**
 * @brief Direct logger sink built from writer, formatter, and filter policies.
 *
 * Writer types provide `write(const char*, std::size_t)`, optionally `init`,
 * `clear`, and `flush`. Format/filter policies remain type-level so the logger
 * can fan out deterministically without runtime registration.
 */
template <typename NameT, typename WriterT, typename FormatT = CompactFormat, typename FilterT = Filter<>>
class Sink
{
public:
    using Name = NameT;
    using Writer = WriterT;
    using Format = FormatT;
    using FilterType = FilterT;

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

        std::array<char, configSOLAR_LOG_FORMAT_BUFFER_BYTES> formatted{};
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

} // namespace solar::log
