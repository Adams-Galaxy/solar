#pragma once

#include <algorithm>
#include <array>
#include <cstring>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include "solar/log/protocol.hpp"

namespace solar::log
{

namespace detail
{

template <typename SourceT, typename DomainT, Level LogLevel,
          typename Application = DefaultApplication>
inline constexpr bool enabled_for = [] {
    if constexpr (!available || !Source<SourceT> || !Domain<DomainT>) {
        return false;
    } else if constexpr (frontend::strict) {
        using System = bound_system_t<Application>;
        return at_least(LogLevel, System::template log_compile_level<SourceT, DomainT>);
    } else {
        return at_least(LogLevel, kconfig_compile_level);
    }
}();

template <typename SourceT, typename DomainT, Level LogLevel, Operation CaptureOperation,
          typename Application, Origin LogOrigin = Origin::Solar, typename... Arguments>
[[nodiscard]] Result<Receipt, Error> emit(CaptureOptions options,
                                          FormatString<std::type_identity_t<Arguments>...> format,
                                          Arguments&&... arguments) noexcept
{
    static_assert(Source<SourceT>,
                  "SOLAR_DIAGNOSTIC_LOG_SOURCE: source requires a valid log descriptor");
    static_assert(Domain<DomainT>,
                  "SOLAR_DIAGNOSTIC_LOG_DOMAIN: domain requires a valid log descriptor");
    if constexpr (frontend::strict && available) {
        using System = bound_system_t<Application>;
        static_assert(System::LogSourceCatalog::template contains<SourceT>,
                      "SOLAR_DIAGNOSTIC_LOG_SOURCE_UNREGISTERED: source is absent from the bound "
                      "System");
        static_assert(System::LogDomainCatalog::template contains<DomainT>,
                      "SOLAR_DIAGNOSTIC_LOG_DOMAIN_UNREGISTERED: domain is absent from the bound "
                      "System");
    }
    if constexpr (!available) {
        return fail<Error>({.status = solar::Status::NotSupported,
                            .reason = Reason::Disabled,
                            .operation = CaptureOperation,
                            .level = LogLevel});
    } else if constexpr (!enabled_for<SourceT, DomainT, LogLevel, Application>) {
        return Receipt{.disposition = Disposition::CompileTimeFiltered};
    } else {
        CaptureRequest request{
            .level = LogLevel,
            .context =
                CaptureOperation == Operation::IsrCapture ? ContextKind::Isr : ContextKind::Thread,
            .origin = LogOrigin,
            .encoding = Encoding::SolarArguments,
            .correlation = options.correlation,
            .domain_token = &type_token<DomainT>,
        };
        auto encoded = encode_native(request, format.data(), format.size(),
                                     std::forward<Arguments>(arguments)...);
        if (!encoded) {
            return fail<Error>(encoded.error());
        }
        using Frontend = CaptureFrontend<CaptureOperation>;
        return frontend::Operation<Frontend, SourceT, Application>::call(request);
    }
}

} // namespace detail

template <typename SourceT, Level LogLevel>
inline constexpr bool enabled = detail::enabled_for<SourceT, domain::Unclassified, LogLevel>;

#define SOLAR_DETAIL_LOG_LEVEL_API(NAME, LEVEL)                                                    \
    template <typename SourceT, typename DomainT = domain::Unclassified,                           \
              typename Application = DefaultApplication, typename... Arguments>                    \
    Result<Receipt, Error> NAME(FormatString<std::type_identity_t<Arguments>...> format,           \
                                Arguments&&... arguments) noexcept                                 \
    {                                                                                              \
        return detail::emit<SourceT, DomainT, Level::LEVEL, Operation::Capture, Application>(      \
            {}, format, std::forward<Arguments>(arguments)...);                                    \
    }                                                                                              \
                                                                                                   \
    template <typename SourceT, typename DomainT = domain::Unclassified,                           \
              typename Application = DefaultApplication, typename... Arguments>                    \
    Result<Receipt, Error> NAME(CaptureOptions options,                                            \
                                FormatString<std::type_identity_t<Arguments>...> format,           \
                                Arguments&&... arguments) noexcept                                 \
    {                                                                                              \
        return detail::emit<SourceT, DomainT, Level::LEVEL, Operation::Capture, Application>(      \
            options, format, std::forward<Arguments>(arguments)...);                               \
    }                                                                                              \
                                                                                                   \
    template <typename SourceT, typename DomainT = domain::Unclassified,                           \
              typename Application = DefaultApplication, typename... Arguments>                    \
    Result<Receipt, Error> try_##NAME(FormatString<std::type_identity_t<Arguments>...> format,     \
                                      Arguments&&... arguments) noexcept                           \
    {                                                                                              \
        return detail::emit<SourceT, DomainT, Level::LEVEL, Operation::TryCapture, Application>(   \
            {}, format, std::forward<Arguments>(arguments)...);                                    \
    }                                                                                              \
                                                                                                   \
    template <typename SourceT, typename DomainT = domain::Unclassified,                           \
              typename Application = DefaultApplication, typename... Arguments>                    \
    Result<Receipt, Error> try_##NAME##_isr(                                                       \
        FormatString<std::type_identity_t<Arguments>...> format,                                   \
        Arguments&&... arguments) noexcept                                                         \
    {                                                                                              \
        return detail::emit<SourceT, DomainT, Level::LEVEL, Operation::IsrCapture, Application>(   \
            {}, format, std::forward<Arguments>(arguments)...);                                    \
    }

SOLAR_DETAIL_LOG_LEVEL_API(trace, Trace)
SOLAR_DETAIL_LOG_LEVEL_API(debug, Debug)
SOLAR_DETAIL_LOG_LEVEL_API(info, Info)
SOLAR_DETAIL_LOG_LEVEL_API(notice, Notice)
SOLAR_DETAIL_LOG_LEVEL_API(warn, Warning)
SOLAR_DETAIL_LOG_LEVEL_API(error, Error)

#undef SOLAR_DETAIL_LOG_LEVEL_API

template <typename SourceT, typename DomainT = domain::Unclassified,
          typename Application = DefaultApplication, typename... Arguments>
[[noreturn]] void fatal(FormatString<std::type_identity_t<Arguments>...> format,
                        Arguments&&... arguments) noexcept
{
    auto captured = detail::emit<SourceT, DomainT, Level::Fatal, Operation::Capture, Application>(
        {}, format, std::forward<Arguments>(arguments)...);
    (void)captured;
#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_LOG)
    using System = bound_system_t<Application>;
    System::LogFacility::panic_mode.store(true, std::memory_order_release);
    {
        auto guard = System::LogFacility::lock.acquire();
        System::LogFacility::record.panic = true;
    }
    (void)System::LogFacility::flush();
    k_panic();
    CODE_UNREACHABLE;
#else
    __builtin_trap();
#endif
}

template <typename SourceT, typename DomainT = domain::Unclassified,
          typename Application = DefaultApplication>
Result<Receipt, Error> text(Level level, std::string_view value,
                            CaptureOptions options = {}) noexcept
{
    static_assert(Source<SourceT>,
                  "SOLAR_DIAGNOSTIC_LOG_SOURCE: source requires a valid log descriptor");
    static_assert(Domain<DomainT>,
                  "SOLAR_DIAGNOSTIC_LOG_DOMAIN: domain requires a valid log descriptor");
    if constexpr (!available) {
        return fail<Error>({.status = solar::Status::NotSupported,
                            .reason = Reason::Disabled,
                            .operation = Operation::Capture,
                            .level = level});
    } else {
        if constexpr (frontend::strict) {
            using System = bound_system_t<Application>;
            static_assert(System::LogSourceCatalog::template contains<SourceT>,
                          "SOLAR_DIAGNOSTIC_LOG_SOURCE_UNREGISTERED: source is absent from the "
                          "bound System");
            static_assert(System::LogDomainCatalog::template contains<DomainT>,
                          "SOLAR_DIAGNOSTIC_LOG_DOMAIN_UNREGISTERED: domain is absent from the "
                          "bound System");
        }
        if (!at_least(level, detail::kconfig_compile_level)) {
            return Receipt{.disposition = Disposition::CompileTimeFiltered};
        }
        CaptureRequest request{
            .level = level,
            .origin = Origin::Solar,
            .encoding = Encoding::Text,
            .correlation = options.correlation,
            .domain_token = &type_token<DomainT>,
        };
        const auto copied = std::min<std::size_t>(
            value.size(),
            std::min<std::size_t>(detail::max_copied_string_bytes, request.payload.size()));
        std::memcpy(request.payload.data(), value.data(), copied);
        request.payload_size = static_cast<std::uint16_t>(copied);
        if (copied != value.size()) {
            request.flags |= flag(RecordFlag::Truncated);
        }
        using Frontend = detail::CaptureFrontend<Operation::Capture>;
        return frontend::Operation<Frontend, SourceT, Application>::call(request);
    }
}

template <typename SourceT, typename DomainT = domain::Unclassified,
          typename Application = DefaultApplication>
Result<Receipt, Error> hexdump(Level level, std::string_view label, std::span<const std::byte> data,
                               CaptureOptions options = {}) noexcept
{
    static_assert(Source<SourceT>,
                  "SOLAR_DIAGNOSTIC_LOG_SOURCE: source requires a valid log descriptor");
    static_assert(Domain<DomainT>,
                  "SOLAR_DIAGNOSTIC_LOG_DOMAIN: domain requires a valid log descriptor");
    if constexpr (!available) {
        return fail<Error>({.status = solar::Status::NotSupported,
                            .reason = Reason::Disabled,
                            .operation = Operation::Capture,
                            .level = level});
    } else {
        if constexpr (frontend::strict) {
            using System = bound_system_t<Application>;
            static_assert(System::LogSourceCatalog::template contains<SourceT>,
                          "SOLAR_DIAGNOSTIC_LOG_SOURCE_UNREGISTERED: source is absent from the "
                          "bound System");
            static_assert(System::LogDomainCatalog::template contains<DomainT>,
                          "SOLAR_DIAGNOSTIC_LOG_DOMAIN_UNREGISTERED: domain is absent from the "
                          "bound System");
        }
        CaptureRequest request{
            .level = level,
            .origin = Origin::Solar,
            .encoding = Encoding::Hexdump,
            .correlation = options.correlation,
            .domain_token = &type_token<DomainT>,
        };
        const auto label_capacity = request.payload.size() - sizeof(detail::HexdumpPayloadHeader);
        const auto label_size = std::min<std::size_t>(
            label.size(), std::min(detail::max_copied_string_bytes, label_capacity));
        const auto available = label_capacity - label_size;
        const auto data_size = std::min<std::size_t>(
            data.size(), std::min<std::size_t>(available, detail::max_hexdump_bytes));
        const detail::HexdumpPayloadHeader header{
            .label_size = static_cast<std::uint16_t>(label_size),
            .data_size = static_cast<std::uint16_t>(data_size),
        };
        std::memcpy(request.payload.data(), &header, sizeof(header));
        std::memcpy(request.payload.data() + sizeof(header), label.data(), label_size);
        std::memcpy(request.payload.data() + sizeof(header) + label_size, data.data(), data_size);
        request.payload_size = static_cast<std::uint16_t>(sizeof(header) + label_size + data_size);
        if (label_size != label.size() || data_size != data.size()) {
            request.flags |= flag(RecordFlag::Truncated);
        }
        using Frontend = detail::CaptureFrontend<Operation::Capture>;
        return frontend::Operation<Frontend, SourceT, Application>::call(request);
    }
}

template <typename Application = DefaultApplication> [[nodiscard]] Result<void> flush() noexcept
{
    if constexpr (!available) {
        return fail<solar::Error>({.status = solar::Status::NotSupported});
    } else {
        using System = bound_system_t<Application>;
        return System::LogFacility::flush();
    }
}

template <typename Application = DefaultApplication> [[nodiscard]] FacilityRecord record() noexcept
{
    if constexpr (!available) {
        return {.last_status = Status::NotSupported};
    } else {
        using System = bound_system_t<Application>;
        return detail::facility_record<System>();
    }
}

template <typename Application = DefaultApplication>
[[nodiscard]] HistoryPage history(Cursor cursor, std::span<Record> output) noexcept
{
    if constexpr (!available) {
        (void)output;
        return {.next = cursor};
    } else {
        using System = bound_system_t<Application>;
        return detail::read_history<System>(cursor, output);
    }
}

template <typename Application = DefaultApplication>
[[nodiscard]] Result<Record, Error> latest() noexcept
{
    if constexpr (!available) {
        return fail<Error>({.status = solar::Status::NotSupported,
                            .reason = Reason::Disabled,
                            .operation = Operation::Query});
    } else {
        using System = bound_system_t<Application>;
        return detail::latest_history<System>();
    }
}

/**
 * @brief Replay one bounded page of encoded history directly to a sink.
 *
 * Records remain encoded in history and are rendered into one shared scratch
 * buffer only while they are delivered. Check HistoryPage::stale to warn the
 * consumer when its cursor fell behind history eviction.
 */
template <typename Sink, typename Application = DefaultApplication>
[[nodiscard]] Result<HistoryPage, Error> replay(Cursor cursor, std::span<Record> records) noexcept
{
#if !defined(CONFIG_SOLAR_LOG) || !defined(CONFIG_SOLAR_LOG_HISTORY)
    (void)cursor;
    (void)records;
    return fail<Error>({.status = solar::Status::NotSupported,
                        .reason = Reason::Disabled,
                        .operation = Operation::Query});
#else
    using System = bound_system_t<Application>;
    const auto page = detail::read_history<System>(cursor, records);
    std::array<char, CONFIG_SOLAR_LOG_RENDER_BUFFER_BYTES> rendered{};
    for (std::size_t index{}; index < page.written; ++index) {
        const auto record = records[index].view();
        auto rendering = detail::render_message(record, rendered);
        if (!rendering) {
            return fail<Error>({.status = status_of(rendering.error()),
                                .reason = Reason::InternalInvariant,
                                .operation = Operation::Render,
                                .source = record.header.source,
                                .level = record.header.level});
        }
        auto consumed = detail::consume_sink<System, Sink>(
            record, std::string_view{rendered.data(), *rendering});
        if (!consumed) {
            return fail<Error>({.status = status_of(consumed.error()),
                                .reason = Reason::SinkFailure,
                                .operation = Operation::Sink,
                                .source = record.header.source,
                                .level = record.header.level});
        }
    }
    return page;
#endif
}

template <typename SourceT, typename Application = DefaultApplication>
[[nodiscard]] Result<void, Error> set_source_level(Level level) noexcept
{
#if !defined(CONFIG_SOLAR_LOG) || !defined(CONFIG_SOLAR_LOG_RUNTIME_FILTERING)
    (void)level;
    return fail<Error>({.status = solar::Status::NotSupported,
                        .reason = Reason::Disabled,
                        .operation = Operation::Query});
#else
    using System = bound_system_t<Application>;
    static_assert(System::LogSourceCatalog::template contains<SourceT>,
                  "SOLAR_DIAGNOSTIC_LOG_SOURCE_UNREGISTERED: runtime filter source is absent");
    constexpr auto id = System::LogSourceCatalog::template Entry<SourceT>::local_id;
    detail::RuntimeFilters<System>::sources[id.index()].store(static_cast<std::uint8_t>(level),
                                                              std::memory_order_release);
    return {};
#endif
}

template <typename SourceT, typename Application = DefaultApplication>
[[nodiscard]] SourceRecord source_record() noexcept
{
#if !defined(CONFIG_SOLAR_LOG)
    return {.last_status = Status::NotSupported};
#else
    using System = bound_system_t<Application>;
    static_assert(System::LogSourceCatalog::template contains<SourceT>,
                  "SOLAR_DIAGNOSTIC_LOG_SOURCE_UNREGISTERED: source query is absent");
    constexpr auto id = System::LogSourceCatalog::template Entry<SourceT>::local_id;
    auto guard = System::LogFacility::lock.acquire();
    return detail::RuntimeFilters<System>::records[id.index()];
#endif
}

template <typename SinkT, typename Application = DefaultApplication>
[[nodiscard]] Result<SinkRecord, Error> sink_record() noexcept
{
#if !defined(CONFIG_SOLAR_LOG)
    return fail<Error>({.status = solar::Status::NotSupported,
                        .reason = Reason::Disabled,
                        .operation = Operation::Query});
#else
    using System = bound_system_t<Application>;
    std::optional<SinkRecord> found{};
    auto guard = System::LogFacility::lock.acquire();
    detail::for_each_route<typename System::LogFacility::Routes>([&]<typename Route> {
        if constexpr (std::is_same_v<typename detail::route_traits<Route>::Sink, SinkT>) {
            found = detail::RouteRuntime<Route>::record;
        }
    });
    if (!found) {
        return fail<Error>({.status = solar::Status::NotFound,
                            .reason = Reason::NotRegistered,
                            .operation = Operation::Query});
    }
    return *found;
#endif
}

template <typename DomainT, typename Application = DefaultApplication>
[[nodiscard]] Result<void, Error> set_domain_level(Level level) noexcept
{
#if !defined(CONFIG_SOLAR_LOG) || !defined(CONFIG_SOLAR_LOG_RUNTIME_FILTERING)
    (void)level;
    return fail<Error>({.status = solar::Status::NotSupported,
                        .reason = Reason::Disabled,
                        .operation = Operation::Query});
#else
    using System = bound_system_t<Application>;
    static_assert(System::LogDomainCatalog::template contains<DomainT>,
                  "SOLAR_DIAGNOSTIC_LOG_DOMAIN_UNREGISTERED: runtime filter domain is absent");
    constexpr auto id = System::LogDomainCatalog::template Entry<DomainT>::local_id;
    detail::RuntimeFilters<System>::domains[id.index()].store(static_cast<std::uint8_t>(level),
                                                              std::memory_order_release);
    return {};
#endif
}

template <typename SinkT, typename Application = DefaultApplication>
[[nodiscard]] Result<void, Error> set_sink_level(Level level) noexcept
{
#if !defined(CONFIG_SOLAR_LOG) || !defined(CONFIG_SOLAR_LOG_RUNTIME_FILTERING)
    (void)level;
    return fail<Error>({.status = solar::Status::NotSupported,
                        .reason = Reason::Disabled,
                        .operation = Operation::Query});
#else
    using System = bound_system_t<Application>;
    bool found{};
    detail::for_each_route<typename System::LogFacility::Routes>([&]<typename Route> {
        if constexpr (std::is_same_v<typename detail::route_traits<Route>::Sink, SinkT>) {
            detail::RouteRuntime<Route>::minimum.store(static_cast<std::uint8_t>(level),
                                                       std::memory_order_release);
            found = true;
        }
    });
    return found ? Result<void, Error>{}
                 : Result<void, Error>{fail<Error>({.status = solar::Status::NotFound,
                                                    .reason = Reason::NotRegistered,
                                                    .operation = Operation::Query})};
#endif
}

} // namespace solar::log
