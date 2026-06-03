#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <tuple>
#include <type_traits>

#include "solar/core.hpp"
#include "solar/log/sink.hpp"
#include "solar/log/source.hpp"
#include "solar/kernel/time.hpp"

namespace solar::log
{

enum class Policy
{
    Direct,
};

/**
 * @brief Aggregate counters for a logger instance.
 */
struct LoggerStats
{
    std::uint32_t emitted = 0;
    std::uint32_t failed = 0;
};

/**
 * @brief Logger implementation that accepts all calls and emits nothing.
 *
 * This is the default system logger so logging can remain available in generic
 * Solar code even when a robot chooses not to configure sinks.
 */
class NullLogger
{
public:
    using Name = solar::Name<"null_log">;

    static Status init()
    {
        return Status::Ok;
    }

    static Status start()
    {
        return Status::Ok;
    }

    static Status stop()
    {
        return Status::Ok;
    }

    static void reset() {}

    static Status emit(Record const &)
    {
        return Status::Ok;
    }

    static LoggerStats stats()
    {
        return {};
    }

    template <typename SourceT = DefaultSource, typename CategoryNameT = NoCategory, typename... Args>
    static void debug(const char *, Args...) {}

    template <typename SourceT = DefaultSource, typename CategoryNameT = NoCategory, typename... Args>
    static void info(const char *, Args...) {}

    template <typename SourceT = DefaultSource, typename CategoryNameT = NoCategory, typename... Args>
    static void warn(const char *, Args...) {}

    template <typename SourceT = DefaultSource, typename CategoryNameT = NoCategory, typename... Args>
    static void error(const char *, Args...) {}

    template <typename SourceT = DefaultSource, typename CategoryNameT = NoCategory, typename... Args>
    static void fatal(const char *, Args...) {}

    template <typename SourceT = DefaultSource, typename CategoryNameT = NoCategory, typename... Args>
    static Status try_debug(const char *, Args...)
    {
        return Status::Ok;
    }

    template <typename SourceT = DefaultSource, typename CategoryNameT = NoCategory, typename... Args>
    static Status try_info(const char *, Args...)
    {
        return Status::Ok;
    }

    template <typename SourceT = DefaultSource, typename CategoryNameT = NoCategory, typename... Args>
    static Status try_warn(const char *, Args...)
    {
        return Status::Ok;
    }

    template <typename SourceT = DefaultSource, typename CategoryNameT = NoCategory, typename... Args>
    static Status try_error(const char *, Args...)
    {
        return Status::Ok;
    }

    template <typename SourceT = DefaultSource, typename CategoryNameT = NoCategory, typename... Args>
    static Status try_fatal(const char *, Args...)
    {
        return Status::Ok;
    }

    template <typename SourceNameT, typename CategoryListT = Categories<>>
    struct Log
    {
        template <typename CategoryNameT = NoCategory, typename... Args>
        static void debug(const char *, Args...) {}

        template <typename CategoryNameT = NoCategory, typename... Args>
        static void info(const char *, Args...) {}

        template <typename CategoryNameT = NoCategory, typename... Args>
        static void warn(const char *, Args...) {}

        template <typename CategoryNameT = NoCategory, typename... Args>
        static void error(const char *, Args...) {}

        template <typename CategoryNameT = NoCategory, typename... Args>
        static void fatal(const char *, Args...) {}
    };
};

template <typename NameT, typename SinksT, Policy DeliveryPolicy = Policy::Direct>
class Logger;

/**
 * @brief Static direct-fanout logger.
 *
 * The logger is usually named in `solar::Runtime<solar::Logging<Logger>>`.
 * Methods are static so app and Solar internals can log through type aliases
 * without carrying a logger object through every call path.
 */
template <typename NameT, typename... SinkTypes>
class Logger<NameT, Sinks<SinkTypes...>, Policy::Direct>
{
public:
    using Name = NameT;
    using SinkList = Sinks<SinkTypes...>;

    static constexpr std::size_t SinkCount = sizeof...(SinkTypes);

    static Status init()
    {
        stats_ = {};
        Status status = Status::Ok;
        std::apply(
            [&](auto &...sinks) {
                ((status == Status::Ok ? status = sinks.init() : status), ...);
            },
            sinks_);
        return status;
    }

    template <typename ContextT>
    static Status init(ContextT &)
    {
        return init();
    }

    static void reset()
    {
        stats_ = {};
        std::apply([](auto &...sinks) { (sinks.reset(), ...); }, sinks_);
    }

    static Status emit(Record record)
    {
        if (record.timestamp_us == 0)
        {
            record.timestamp_us = static_cast<std::uint64_t>(kernel::to_milliseconds(kernel::now_ticks()).count()) * 1000ULL;
        }

        Status status = Status::Ok;
        std::apply(
            [&](auto &...sinks) {
                ((status = merge(status, sinks.publish(record))), ...);
            },
            sinks_);

        if (status == Status::Ok)
        {
            ++stats_.emitted;
        }
        else
        {
            ++stats_.failed;
        }
        return status;
    }

    static Status emit(Level level, const char *source, const char *category, std::uint16_t id, const char *message)
    {
        return emit(Record{
            .level = level,
            .source = source,
            .category = category,
            .id = id,
            .message = message,
        });
    }

    template <typename SourceT = DefaultSource, typename CategoryNameT = NoCategory, typename... Args>
    /**
     * @brief Fire-and-forget formatted debug log.
     */
    static void debug(const char *format, Args... args)
    {
        (void)try_debug<SourceT, CategoryNameT>(format, args...);
    }

    template <typename SourceT = DefaultSource, typename CategoryNameT = NoCategory, typename... Args>
    /**
     * @brief Fire-and-forget formatted info log.
     */
    static void info(const char *format, Args... args)
    {
        (void)try_info<SourceT, CategoryNameT>(format, args...);
    }

    template <typename SourceT = DefaultSource, typename CategoryNameT = NoCategory, typename... Args>
    /**
     * @brief Fire-and-forget formatted warning log.
     */
    static void warn(const char *format, Args... args)
    {
        (void)try_warn<SourceT, CategoryNameT>(format, args...);
    }

    template <typename SourceT = DefaultSource, typename CategoryNameT = NoCategory, typename... Args>
    /**
     * @brief Fire-and-forget formatted error log.
     */
    static void error(const char *format, Args... args)
    {
        (void)try_error<SourceT, CategoryNameT>(format, args...);
    }

    template <typename SourceT = DefaultSource, typename CategoryNameT = NoCategory, typename... Args>
    static void fatal(const char *format, Args... args)
    {
        (void)try_fatal<SourceT, CategoryNameT>(format, args...);
    }

    template <typename SourceT = DefaultSource, typename CategoryNameT = NoCategory, typename... Args>
    static void debug_id(std::uint16_t id, const char *format, Args... args)
    {
        (void)try_debug_id<SourceT, CategoryNameT>(id, format, args...);
    }

    template <typename SourceT = DefaultSource, typename CategoryNameT = NoCategory, typename... Args>
    static void info_id(std::uint16_t id, const char *format, Args... args)
    {
        (void)try_info_id<SourceT, CategoryNameT>(id, format, args...);
    }

    template <typename SourceT = DefaultSource, typename CategoryNameT = NoCategory, typename... Args>
    static void warn_id(std::uint16_t id, const char *format, Args... args)
    {
        (void)try_warn_id<SourceT, CategoryNameT>(id, format, args...);
    }

    template <typename SourceT = DefaultSource, typename CategoryNameT = NoCategory, typename... Args>
    static void error_id(std::uint16_t id, const char *format, Args... args)
    {
        (void)try_error_id<SourceT, CategoryNameT>(id, format, args...);
    }

    template <typename SourceT = DefaultSource, typename CategoryNameT = NoCategory, typename... Args>
    static void fatal_id(std::uint16_t id, const char *format, Args... args)
    {
        (void)try_fatal_id<SourceT, CategoryNameT>(id, format, args...);
    }

    template <typename SourceT = DefaultSource, typename CategoryNameT = NoCategory, typename... Args>
    static Status try_debug(const char *format, Args... args)
    {
        return emit_format<SourceT, CategoryNameT>(Level::Debug, 0, format, args...);
    }

    template <typename SourceT = DefaultSource, typename CategoryNameT = NoCategory, typename... Args>
    static Status try_info(const char *format, Args... args)
    {
        return emit_format<SourceT, CategoryNameT>(Level::Info, 0, format, args...);
    }

    template <typename SourceT = DefaultSource, typename CategoryNameT = NoCategory, typename... Args>
    static Status try_warn(const char *format, Args... args)
    {
        return emit_format<SourceT, CategoryNameT>(Level::Warn, 0, format, args...);
    }

    template <typename SourceT = DefaultSource, typename CategoryNameT = NoCategory, typename... Args>
    static Status try_error(const char *format, Args... args)
    {
        return emit_format<SourceT, CategoryNameT>(Level::Error, 0, format, args...);
    }

    template <typename SourceT = DefaultSource, typename CategoryNameT = NoCategory, typename... Args>
    static Status try_fatal(const char *format, Args... args)
    {
        return emit_format<SourceT, CategoryNameT>(Level::Fatal, 0, format, args...);
    }

    template <typename SourceT = DefaultSource, typename CategoryNameT = NoCategory, typename... Args>
    static Status try_debug_id(std::uint16_t id, const char *format, Args... args)
    {
        return emit_format<SourceT, CategoryNameT>(Level::Debug, id, format, args...);
    }

    template <typename SourceT = DefaultSource, typename CategoryNameT = NoCategory, typename... Args>
    static Status try_info_id(std::uint16_t id, const char *format, Args... args)
    {
        return emit_format<SourceT, CategoryNameT>(Level::Info, id, format, args...);
    }

    template <typename SourceT = DefaultSource, typename CategoryNameT = NoCategory, typename... Args>
    static Status try_warn_id(std::uint16_t id, const char *format, Args... args)
    {
        return emit_format<SourceT, CategoryNameT>(Level::Warn, id, format, args...);
    }

    template <typename SourceT = DefaultSource, typename CategoryNameT = NoCategory, typename... Args>
    static Status try_error_id(std::uint16_t id, const char *format, Args... args)
    {
        return emit_format<SourceT, CategoryNameT>(Level::Error, id, format, args...);
    }

    template <typename SourceT = DefaultSource, typename CategoryNameT = NoCategory, typename... Args>
    static Status try_fatal_id(std::uint16_t id, const char *format, Args... args)
    {
        return emit_format<SourceT, CategoryNameT>(Level::Fatal, id, format, args...);
    }

    static void flush()
    {
        std::apply([](auto &...sinks) { (sinks.flush(), ...); }, sinks_);
    }

    static LoggerStats stats()
    {
        return stats_;
    }

    template <typename SinkT>
    static SinkT &sink()
    {
        return std::get<SinkT>(sinks_);
    }

    template <typename SourceNameT, typename CategoryListT = Categories<>>
    struct Log
    {
        using SourceType = Source<SourceNameT, CategoryListT>;

        template <typename CategoryNameT = NoCategory, typename... Args>
        static void debug(const char *format, Args... args)
        {
            Logger::template debug<SourceType, CategoryNameT>(format, args...);
        }

        template <typename CategoryNameT = NoCategory, typename... Args>
        static void info(const char *format, Args... args)
        {
            Logger::template info<SourceType, CategoryNameT>(format, args...);
        }

        template <typename CategoryNameT = NoCategory, typename... Args>
        static void warn(const char *format, Args... args)
        {
            Logger::template warn<SourceType, CategoryNameT>(format, args...);
        }

        template <typename CategoryNameT = NoCategory, typename... Args>
        static void error(const char *format, Args... args)
        {
            Logger::template error<SourceType, CategoryNameT>(format, args...);
        }

        template <typename CategoryNameT = NoCategory, typename... Args>
        static void fatal(const char *format, Args... args)
        {
            Logger::template fatal<SourceType, CategoryNameT>(format, args...);
        }

        template <typename CategoryNameT = NoCategory, typename... Args>
        static void debug_id(std::uint16_t id, const char *format, Args... args)
        {
            Logger::template debug_id<SourceType, CategoryNameT>(id, format, args...);
        }

        template <typename CategoryNameT = NoCategory, typename... Args>
        static void info_id(std::uint16_t id, const char *format, Args... args)
        {
            Logger::template info_id<SourceType, CategoryNameT>(id, format, args...);
        }

        template <typename CategoryNameT = NoCategory, typename... Args>
        static void warn_id(std::uint16_t id, const char *format, Args... args)
        {
            Logger::template warn_id<SourceType, CategoryNameT>(id, format, args...);
        }

        template <typename CategoryNameT = NoCategory, typename... Args>
        static void error_id(std::uint16_t id, const char *format, Args... args)
        {
            Logger::template error_id<SourceType, CategoryNameT>(id, format, args...);
        }

        template <typename CategoryNameT = NoCategory, typename... Args>
        static void fatal_id(std::uint16_t id, const char *format, Args... args)
        {
            Logger::template fatal_id<SourceType, CategoryNameT>(id, format, args...);
        }

        template <typename CategoryNameT = NoCategory, typename... Args>
        static Status try_debug(const char *format, Args... args)
        {
            return Logger::template try_debug<SourceType, CategoryNameT>(format, args...);
        }

        template <typename CategoryNameT = NoCategory, typename... Args>
        static Status try_info(const char *format, Args... args)
        {
            return Logger::template try_info<SourceType, CategoryNameT>(format, args...);
        }

        template <typename CategoryNameT = NoCategory, typename... Args>
        static Status try_warn(const char *format, Args... args)
        {
            return Logger::template try_warn<SourceType, CategoryNameT>(format, args...);
        }

        template <typename CategoryNameT = NoCategory, typename... Args>
        static Status try_error(const char *format, Args... args)
        {
            return Logger::template try_error<SourceType, CategoryNameT>(format, args...);
        }

        template <typename CategoryNameT = NoCategory, typename... Args>
        static Status try_fatal(const char *format, Args... args)
        {
            return Logger::template try_fatal<SourceType, CategoryNameT>(format, args...);
        }

        template <typename CategoryNameT = NoCategory, typename... Args>
        static Status try_debug_id(std::uint16_t id, const char *format, Args... args)
        {
            return Logger::template try_debug_id<SourceType, CategoryNameT>(id, format, args...);
        }

        template <typename CategoryNameT = NoCategory, typename... Args>
        static Status try_info_id(std::uint16_t id, const char *format, Args... args)
        {
            return Logger::template try_info_id<SourceType, CategoryNameT>(id, format, args...);
        }

        template <typename CategoryNameT = NoCategory, typename... Args>
        static Status try_warn_id(std::uint16_t id, const char *format, Args... args)
        {
            return Logger::template try_warn_id<SourceType, CategoryNameT>(id, format, args...);
        }

        template <typename CategoryNameT = NoCategory, typename... Args>
        static Status try_error_id(std::uint16_t id, const char *format, Args... args)
        {
            return Logger::template try_error_id<SourceType, CategoryNameT>(id, format, args...);
        }

        template <typename CategoryNameT = NoCategory, typename... Args>
        static Status try_fatal_id(std::uint16_t id, const char *format, Args... args)
        {
            return Logger::template try_fatal_id<SourceType, CategoryNameT>(id, format, args...);
        }
    };

private:
    static Status merge(Status current, Status next)
    {
        return current == Status::Ok ? next : current;
    }

    template <typename SourceT, typename CategoryNameT>
    static Status emit_for(Level level, const char *message, std::uint16_t id)
    {
        return emit(level, SourceT::name(), CategoryNameT::c_str(), id, message);
    }

    template <typename SourceT, typename CategoryNameT, typename... Args>
    static Status emit_format(Level level, std::uint16_t id, const char *format, Args... args)
    {
        std::array<char, configSOLAR_LOG_RECORD_BYTES> message{};
        if (format == nullptr)
        {
            message[0] = '\0';
        }
        else if constexpr (sizeof...(Args) == 0)
        {
            std::snprintf(message.data(), message.size(), "%s", format);
        }
        else
        {
            std::snprintf(message.data(), message.size(), format, args...);
        }
        return emit_for<SourceT, CategoryNameT>(level, message.data(), id);
    }

    static inline std::tuple<SinkTypes...> sinks_{};
    static inline LoggerStats stats_{};
};

} // namespace solar::log
