#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>

#include "solar/core.hpp"
#include "solar/events/sink.hpp"
#include "solar/rtos/critical_section.hpp"
#include "solar/rtos/time.hpp"

namespace solar::events
{

template <typename NameT = solar::Name<"events">,
          std::size_t HistoryDepth = 64,
          typename SinkListT = Sinks<>>
class Facility;

/**
 * @brief Passive runtime event facility with fixed history and direct sinks.
 *
 * Events are emitted through static methods so any code that can name the
 * system event facility can use it without object plumbing. The facility is
 * still listed as a Solar facility when it should be booted, reset, and exposed
 * in system snapshots.
 */
template <typename NameT, std::size_t HistoryDepth, typename... SinkTypes>
class Facility<NameT, HistoryDepth, Sinks<SinkTypes...>>
{
public:
    using Name = NameT;
    using SinkList = Sinks<SinkTypes...>;

    static_assert(HistoryDepth > 0, "Solar event facilities require a non-zero history depth");

    static constexpr std::size_t SinkCount = sizeof...(SinkTypes);
    static constexpr std::size_t Capacity = HistoryDepth;

    /**
     * @brief Reset history and initialize every sink without a context.
     */
    static Status init()
    {
        reset();
        Status status = Status::Ok;
        std::apply(
            [&](auto &...sinks) {
                ((status == Status::Ok ? status = sinks.init() : status), ...);
            },
            sinks_);
        return status;
    }

    template <typename ContextT>
    /**
     * @brief Reset history and initialize every sink with a Solar context.
     */
    static Status init(ContextT &ctx)
    {
        reset();
        Status status = Status::Ok;
        std::apply(
            [&](auto &...sinks) {
                ((status == Status::Ok ? status = sinks.init(ctx) : status), ...);
            },
            sinks_);
        return status;
    }

    static Status start()
    {
        return Status::Ok;
    }

    template <typename ContextT>
    static Status start(ContextT &)
    {
        return start();
    }

    static Status stop()
    {
        return Status::Ok;
    }

    template <typename ContextT>
    static Status stop(ContextT &)
    {
        return stop();
    }

    static void reset()
    {
        rtos::CriticalSection guard;
        head_ = 0;
        size_ = 0;
        sequence_ = 0;
        stats_ = {};
        std::apply([](auto &...sinks) { (sinks.reset(), ...); }, sinks_);
    }

    template <typename EventT, typename SourceT = solar::Name<"system">>
    /**
     * @brief Emit an event and ignore sink errors.
     */
    static void emit(std::int32_t value = 0, std::uint32_t detail = 0)
    {
        (void)try_emit<EventT, SourceT>(value, detail);
    }

    template <typename EventT, typename SourceT = solar::Name<"system">>
    /**
     * @brief Emit an event and return the merged sink status.
     */
    static Status try_emit(std::int32_t value = 0, std::uint32_t detail = 0)
    {
        const auto timestamp_us = static_cast<std::uint64_t>(rtos::to_milliseconds(rtos::now_ticks()).count()) * 1000ULL;
        rtos::CriticalSection guard;
        Record record{
            .timestamp_us = timestamp_us,
            .sequence = ++sequence_,
            .id = EventT::id,
            .severity = EventT::severity,
            .name = EventT::Name::c_str(),
            .source = SourceT::c_str(),
            .value = value,
            .detail = detail,
        };

        store(record);
        const Status status = publish(record);
        ++stats_.emitted;
        if (status != Status::Ok)
        {
            ++stats_.failed;
        }
        return status;
    }

    static std::size_t count()
    {
        rtos::CriticalSection guard;
        return size_;
    }

    static Stats stats()
    {
        rtos::CriticalSection guard;
        return stats_;
    }

    static std::size_t read(Record *out, std::size_t max_records)
    {
        if (out == nullptr)
        {
            return 0;
        }

        rtos::CriticalSection guard;
        const std::size_t count = size_ < max_records ? size_ : max_records;
        const std::size_t tail = (head_ + HistoryDepth - size_) % HistoryDepth;
        for (std::size_t i = 0; i < count; ++i)
        {
            out[i] = history_[(tail + i) % HistoryDepth];
        }
        return count;
    }

    static Result<Record> latest()
    {
        rtos::CriticalSection guard;
        if (size_ == 0)
        {
            return Status::Empty;
        }
        const std::size_t index = (head_ + HistoryDepth - 1U) % HistoryDepth;
        return history_[index];
    }

    template <typename SinkT>
    /**
     * @brief Access a configured sink by concrete type.
     */
    static SinkT &sink()
    {
        return std::get<SinkT>(sinks_);
    }

private:
    static void store(Record const &record)
    {
        history_[head_] = record;
        head_ = (head_ + 1U) % HistoryDepth;
        if (size_ < HistoryDepth)
        {
            ++size_;
        }
        else
        {
            ++stats_.dropped;
        }
    }

    static Status publish(Record const &record)
    {
        Status status = Status::Ok;
        std::apply(
            [&](auto &...sinks) {
                ((status = merge(status, sinks.publish(record))), ...);
            },
            sinks_);
        return status;
    }

    static Status merge(Status current, Status next)
    {
        return current == Status::Ok ? next : current;
    }

    static inline std::array<Record, HistoryDepth> history_{};
    static inline std::tuple<SinkTypes...> sinks_{};
    static inline std::size_t head_ = 0;
    static inline std::size_t size_ = 0;
    static inline std::uint32_t sequence_ = 0;
    static inline Stats stats_{};
};

} // namespace solar::events
