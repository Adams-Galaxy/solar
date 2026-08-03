#pragma once

#include <array>
#include <chrono>
#include <concepts>
#include <cstring>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include "solar/catalog/catalog.hpp"
#include "solar/core/spin_mutex.hpp"
#include "solar/core/type_list.hpp"
#include "solar/log/declaration.hpp"
#include "solar/log/format.hpp"
#include "solar/log/types.hpp"

#if defined(__ZEPHYR__)
#include "solar/kernel/time.hpp"
#endif

namespace solar::log
{
namespace static_detail
{

template <typename Tag, typename Entries> struct CatalogFrom;

template <typename Tag, typename... Entries> struct CatalogFrom<Tag, TypeList<Entries...>>
{
  private:
    template <std::size_t... Indices>
    static auto make(std::index_sequence<Indices...>)
        -> Catalog<Tag, CatalogEntry<Tag, Entries, ApplicationOwner, origin::Direct, Indices>...>;

  public:
    using type = decltype(make(std::index_sequence_for<Entries...>{}));
};

template <typename Tag, typename Entries>
using catalog_from_t = typename CatalogFrom<Tag, Entries>::type;

template <typename Sink> [[nodiscard]] Result<void> initialize_sink() noexcept
{
    if constexpr (requires { Sink::init(); }) {
        auto result = Sink::init();
        return result ? Result<void>{}
                      : Result<void>{fail<solar::Error>({.status = status_of(result.error())})};
    }
    return {};
}

template <typename Sink> [[nodiscard]] Result<void> deinitialize_sink() noexcept
{
    if constexpr (requires { Sink::deinit(); }) {
        auto result = Sink::deinit();
        return result ? Result<void>{}
                      : Result<void>{fail<solar::Error>({.status = status_of(result.error())})};
    }
    return {};
}

template <typename Context, typename Sink>
[[nodiscard]] Result<void> consume_sink(RecordView record, std::string_view rendered) noexcept
{
    if constexpr (requires { Sink::template consume<Context>(record, rendered); }) {
        auto result = Sink::template consume<Context>(record, rendered);
        return result ? Result<void>{}
                      : Result<void>{fail<solar::Error>({.status = status_of(result.error())})};
    } else if constexpr (requires { Sink::consume(record, rendered); }) {
        auto result = Sink::consume(record, rendered);
        return result ? Result<void>{}
                      : Result<void>{fail<solar::Error>({.status = status_of(result.error())})};
    } else if constexpr (requires { Sink::write(rendered); }) {
        auto result = Sink::write(rendered);
        return result ? Result<void>{}
                      : Result<void>{fail<solar::Error>({.status = status_of(result.error())})};
    } else {
        static_assert(solar::detail::dependent_false_v<Sink>,
                      "SOLAR_LOG_INVALID_SINK: sink requires consume or write");
    }
}

[[nodiscard]] inline Timestamp now_microseconds() noexcept
{
#if defined(__ZEPHYR__)
    return std::chrono::duration_cast<std::chrono::microseconds>(kernel::now().time_since_epoch())
        .count();
#else
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
#endif
}

} // namespace static_detail

/**
 * Allocation-free logger with explicit application ownership.
 *
 * Records stay encoded in a bounded ring and are rendered only when delivered
 * to a sink or replayed. Source and domain catalogs come directly from the
 * declared type lists; no System binding or runtime callback registration is
 * involved.
 */
template <typename Application, typename Sources, typename Domains, typename Sinks,
          std::size_t HistoryCapacity>
struct StaticLogger;

template <typename Application, typename... SourceTypes, typename... DomainTypes,
          typename... SinkTypes, std::size_t HistoryCapacity>
struct StaticLogger<Application, TypeList<SourceTypes...>, TypeList<DomainTypes...>,
                    TypeList<SinkTypes...>, HistoryCapacity>
{
    static_assert(HistoryCapacity > 0);
    static_assert(unique_types_v<TypeList<SourceTypes...>>);
    static_assert(unique_types_v<TypeList<DomainTypes...>>);
    static constexpr std::string_view name = "solar.logging";
    using Dependencies = TypeList<>;
    using LogSourceCatalog = static_detail::catalog_from_t<SourceTag, TypeList<SourceTypes...>>;
    using LogDomainCatalog = static_detail::catalog_from_t<DomainTag, TypeList<DomainTypes...>>;

    [[nodiscard]] static Result<void> initialize() noexcept
    {
        SpinGuard guard{lock_};
        head_ = size_ = lost_ = 0;
        status_ = {.history_capacity = HistoryCapacity,
                   .next_sequence = 1,
                   .last_status = Status::Ok,
                   .ready = true,
                   // Initialization is deliberately observable. When the logger is
                   // ordered before the remaining modules, their bring-up records
                   // are retained even though the System has not reached start().
                   .accepting = true};
        Result<void> result{};
        ((result ? result = static_detail::initialize_sink<SinkTypes>() : result), ...);
        return result;
    }

    [[nodiscard]] static Result<void> start() noexcept
    {
        SpinGuard guard{lock_};
        if (!status_.ready) {
            return fail<solar::Error>({.status = Status::NotReady});
        }
        status_.accepting = true;
        return {};
    }

    [[nodiscard]] static Result<void> stop() noexcept
    {
        {
            SpinGuard guard{lock_};
            status_.accepting = false;
        }
        return flush();
    }

    [[nodiscard]] static Result<void> deinitialize() noexcept
    {
        Result<void> result{};
        ((result ? result = static_detail::deinitialize_sink<SinkTypes>() : result), ...);
        SpinGuard guard{lock_};
        status_.ready = false;
        return result;
    }

    template <typename Source, typename Domain, Level LogLevel, typename... Arguments>
    [[nodiscard]] static Result<Receipt, Error>
    emit(CaptureOptions options, FormatString<std::type_identity_t<Arguments>...> format,
         Arguments&&... arguments) noexcept
    {
        static_assert(LogSourceCatalog::template contains<Source>,
                      "SOLAR_LOG_SOURCE_NOT_DECLARED: source is absent from this logger");
        static_assert(LogDomainCatalog::template contains<Domain>,
                      "SOLAR_LOG_DOMAIN_NOT_DECLARED: domain is absent from this logger");
        CaptureRequest request{.level = LogLevel,
                               .context = ContextKind::Thread,
                               .origin = Origin::Solar,
                               .encoding = Encoding::SolarArguments,
                               .correlation = options.correlation};
        auto encoded = detail::encode_native(request, format.data(), format.size(),
                                             std::forward<Arguments>(arguments)...);
        if (!encoded) {
            return fail<Error>(encoded.error());
        }
        return capture<Source, Domain>(request);
    }

#define SOLAR_STATIC_LOG_LEVEL(NAME, LEVEL)                                                        \
    template <typename Source, typename Domain = domain::Unclassified, typename... Arguments>      \
    [[nodiscard]] static Result<Receipt, Error> NAME(                                              \
        FormatString<std::type_identity_t<Arguments>...> format,                                   \
        Arguments&&... arguments) noexcept                                                         \
    {                                                                                              \
        return emit<Source, Domain, Level::LEVEL>({}, format,                                      \
                                                  std::forward<Arguments>(arguments)...);          \
    }

    SOLAR_STATIC_LOG_LEVEL(trace, Trace)
    SOLAR_STATIC_LOG_LEVEL(debug, Debug)
    SOLAR_STATIC_LOG_LEVEL(info, Info)
    SOLAR_STATIC_LOG_LEVEL(notice, Notice)
    SOLAR_STATIC_LOG_LEVEL(warn, Warning)
    SOLAR_STATIC_LOG_LEVEL(error, Error)
#undef SOLAR_STATIC_LOG_LEVEL

    [[nodiscard]] static FacilityRecord record() noexcept
    {
        SpinGuard guard{lock_};
        return status_;
    }

    [[nodiscard]] static HistoryPage history(Cursor cursor, std::span<Record> output) noexcept
    {
        SpinGuard guard{lock_};
        HistoryPage page{.next = cursor};
        if (size_ == 0) {
            return page;
        }
        const auto oldest = records_[head_].header.sequence;
        if (cursor.next_sequence < oldest) {
            page.stale = true;
            page.evicted_before = oldest - cursor.next_sequence;
            page.next.next_sequence = oldest;
        }
        for (std::size_t index{}; index < size_; ++index) {
            const auto& record = records_[(head_ + index) % HistoryCapacity];
            if (record.header.sequence < page.next.next_sequence) {
                continue;
            }
            ++page.available;
            if (page.written < output.size()) {
                output[page.written++] = record;
                page.next.next_sequence = record.header.sequence + 1;
            }
        }
        return page;
    }

    template <typename Sink>
    [[nodiscard]] static Result<HistoryPage, Error> replay(Cursor cursor,
                                                           std::span<Record> scratch) noexcept
    {
        const auto page = history(cursor, scratch);
        for (std::size_t index{}; index < page.written; ++index) {
            if (auto result = deliver<Sink>(scratch[index]); !result) {
                return fail<Error>({.status = status_of(result.error()),
                                    .reason = Reason::SinkFailure,
                                    .operation = Operation::Sink,
                                    .source = scratch[index].header.source,
                                    .level = scratch[index].header.level});
            }
        }
        return page;
    }

    [[nodiscard]] static Result<void> flush() noexcept
    {
        (flush_one<SinkTypes>(), ...);
        return {};
    }

  private:
    template <typename Source, typename Domain>
    [[nodiscard]] static Result<Receipt, Error> capture(const CaptureRequest& request) noexcept
    {
        Record stored{};
        {
            SpinGuard guard{lock_};
            if (!status_.ready || !status_.accepting) {
                return fail<Error>({.status = Status::NotReady,
                                    .reason = Reason::CaptureClosed,
                                    .operation = Operation::Capture,
                                    .level = request.level});
            }
            ++status_.attempted;
            stored.header = {
                .sequence = status_.next_sequence++,
                .timestamp = static_detail::now_microseconds(),
                .source = LogSourceCatalog::template Entry<Source>::local_id,
                .domain = LogDomainCatalog::template Entry<Domain>::local_id,
                .level = request.level,
                .context = request.context,
                .correlation = request.correlation,
                .callsite = request.callsite,
                .origin = request.origin,
                .encoding = request.encoding,
                .payload_size = request.payload_size,
                .flags = request.flags,
            };
            std::memcpy(stored.payload.data(), request.payload.data(), request.payload_size);
            if (size_ == HistoryCapacity) {
                records_[head_] = stored;
                head_ = (head_ + 1) % HistoryCapacity;
                ++lost_;
                ++status_.history_evicted;
            } else {
                records_[(head_ + size_) % HistoryCapacity] = stored;
                ++size_;
            }
            ++status_.captured;
            status_.history_used = size_;
        }

        Result<void> delivered{};
        ((delivered ? delivered = deliver<SinkTypes>(stored) : delivered), ...);
        if (!delivered) {
            SpinGuard guard{lock_};
            ++status_.sink_failures;
            status_.last_status = status_of(delivered.error());
        }
        return Receipt{.disposition = Disposition::Captured,
                       .sequence = stored.header.sequence,
                       .timestamp = stored.header.timestamp,
                       .encoded_size = static_cast<std::uint16_t>(sizeof(RecordHeader) +
                                                                  stored.header.payload_size),
                       .flags = stored.header.flags};
    }

    template <typename Sink>
    [[nodiscard]] static Result<void> deliver(const Record& stored) noexcept
    {
        std::array<char,
#if defined(CONFIG_SOLAR_LOG_RENDER_BUFFER_BYTES)
                   CONFIG_SOLAR_LOG_RENDER_BUFFER_BYTES
#else
                   256
#endif
                   >
            rendered{};
        // StaticLogger currently produces SolarArguments records exclusively,
        // so it can use the dependency-light native renderer directly.
        auto result = detail::render_native(stored.view(), rendered);
        if (!result) {
            return fail<solar::Error>({.status = status_of(result.error())});
        }
        return static_detail::consume_sink<StaticLogger, Sink>(
            stored.view(), std::string_view{rendered.data(), *result});
    }

    template <typename Sink> static void flush_one() noexcept
    {
        if constexpr (requires { Sink::flush(); }) {
            Sink::flush();
        }
    }

    inline static std::array<Record, HistoryCapacity> records_{};
    inline static std::size_t head_{};
    inline static std::size_t size_{};
    inline static std::uint64_t lost_{};
    inline static FacilityRecord status_{};
    inline static SpinMutex lock_{};
};

} // namespace solar::log
