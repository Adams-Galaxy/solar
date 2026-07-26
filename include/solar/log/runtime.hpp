#pragma once

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string_view>

#include "solar/log/facility.hpp"
#include "solar/log/platform.hpp"

#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_LOG)
#include <zephyr/sys/cbprintf.h>

#include "solar/execution/runtime.hpp"
#include "solar/kernel/deadline.hpp"
#include "solar/kernel/interrupt.hpp"
#include "solar/kernel/time.hpp"
#endif

namespace solar::log::detail
{

template <typename System, typename SourceT>
[[nodiscard]] Result<Receipt, Error> capture(const CaptureRequest& request,
                                             Operation operation) noexcept;

template <typename System> [[nodiscard]] FacilityRecord facility_record() noexcept;

template <typename System>
[[nodiscard]] HistoryPage read_history(Cursor cursor, std::span<Record> output) noexcept;

template <typename System> [[nodiscard]] Result<Record, Error> latest_history() noexcept;

#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_LOG)

inline void CompactHistory::clear() noexcept
{
    used_ = 0;
    evicted_ = 0;
}

inline CompactHistory::AppendResult CompactHistory::append(const StoredRecord& record) noexcept
{
#if !defined(CONFIG_SOLAR_LOG_HISTORY)
    (void)record;
    return {};
#else
    const auto entry_size = sizeof(RecordHeader) + record.header.payload_size;
    const auto total_size = sizeof(std::uint16_t) + entry_size;
    if (total_size > bytes_.size()) {
        return {};
    }
    AppendResult result{.stored = true};
    while (used_ + total_size > bytes_.size()) {
        std::uint16_t oldest_size{};
        if (used_ < sizeof(oldest_size)) {
            used_ = 0;
            break;
        }
        std::memcpy(&oldest_size, bytes_.data(), sizeof(oldest_size));
        const auto oldest_total = sizeof(oldest_size) + oldest_size;
        if (oldest_total > used_) {
            used_ = 0;
            break;
        }
        std::memmove(bytes_.data(), bytes_.data() + oldest_total, used_ - oldest_total);
        used_ -= oldest_total;
        ++result.evicted;
        ++evicted_;
    }
    const auto encoded_size = static_cast<std::uint16_t>(entry_size);
    std::memcpy(bytes_.data() + used_, &encoded_size, sizeof(encoded_size));
    std::memcpy(bytes_.data() + used_ + sizeof(encoded_size), &record.header,
                sizeof(record.header));
    std::memcpy(bytes_.data() + used_ + sizeof(encoded_size) + sizeof(record.header),
                record.payload.data(), record.header.payload_size);
    used_ += total_size;
    return result;
#endif
}

inline HistoryPage CompactHistory::read(Cursor cursor, std::span<Record> output) const noexcept
{
    HistoryPage page{.next = cursor};
    std::size_t offset{};
    Sequence oldest{};
    while (offset + sizeof(std::uint16_t) <= used_) {
        std::uint16_t entry_size{};
        std::memcpy(&entry_size, bytes_.data() + offset, sizeof(entry_size));
        const auto total = sizeof(entry_size) + entry_size;
        if (entry_size < sizeof(RecordHeader) || offset + total > used_) {
            break;
        }
        RecordHeader header{};
        std::memcpy(&header, bytes_.data() + offset + sizeof(entry_size), sizeof(header));
        if (oldest == 0) {
            oldest = header.sequence;
        }
        if (header.sequence >= cursor.next_sequence) {
            ++page.available;
            if (page.written < output.size()) {
                auto& destination = output[page.written++];
                destination.header = header;
                std::memcpy(destination.payload.data(),
                            bytes_.data() + offset + sizeof(entry_size) + sizeof(header),
                            header.payload_size);
                page.next.next_sequence = header.sequence + 1;
            }
        }
        offset += total;
    }
    if (oldest != 0 && cursor.next_sequence < oldest) {
        page.stale = true;
        page.evicted_before = oldest - cursor.next_sequence;
        if (page.written == 0) {
            page.next.next_sequence = oldest;
        }
    }
    return page;
}

inline Result<Record, Error> CompactHistory::latest() const noexcept
{
    std::optional<Record> found{};
    std::size_t offset{};
    while (offset + sizeof(std::uint16_t) <= used_) {
        std::uint16_t entry_size{};
        std::memcpy(&entry_size, bytes_.data() + offset, sizeof(entry_size));
        const auto total = sizeof(entry_size) + entry_size;
        if (entry_size < sizeof(RecordHeader) || offset + total > used_) {
            break;
        }
        Record record{};
        std::memcpy(&record.header, bytes_.data() + offset + sizeof(entry_size),
                    sizeof(record.header));
        std::memcpy(record.payload.data(),
                    bytes_.data() + offset + sizeof(entry_size) + sizeof(record.header),
                    record.header.payload_size);
        found = record;
        offset += total;
    }
    if (!found) {
        return fail<Error>({.status = solar::Status::NotFound,
                            .reason = Reason::HistoryEmpty,
                            .operation = Operation::Query});
    }
    return *found;
}

template <typename Routes, typename Function> void for_each_route(Function&& function);

template <typename... Routes, typename Function>
void for_each_route(Sinks<Routes...>, Function&& function)
{
    (function.template operator()<Routes>(), ...);
}

template <typename Routes, typename Function> void for_each_route(Function&& function)
{
    for_each_route(Routes{}, std::forward<Function>(function));
}

template <typename Routes> [[nodiscard]] constexpr bool any_route_accepts(Level level) noexcept
{
    bool accepted{};
    for_each_route<Routes>([&]<typename Route> {
        accepted = accepted || at_least(level, route_traits<Route>::minimum);
    });
    return accepted;
}

template <typename Route> struct RouteRuntime
{
    inline static std::atomic<std::uint8_t> minimum{
        static_cast<std::uint8_t>(route_traits<Route>::minimum)};
    inline static SinkRecord record{};
};

template <typename Routes> [[nodiscard]] bool any_runtime_route_accepts(Level level) noexcept
{
    bool accepted{};
    for_each_route<Routes>([&]<typename Route> {
        const auto minimum =
            static_cast<Level>(RouteRuntime<Route>::minimum.load(std::memory_order_acquire));
        accepted = accepted || at_least(level, minimum);
    });
    return accepted;
}

template <typename System> struct RuntimeFilters
{
    inline static std::array<std::atomic<std::uint8_t>, System::LogSourceCatalog::size> sources{};
    inline static std::array<std::atomic<std::uint8_t>, System::LogDomainCatalog::size> domains{};
    inline static std::array<SourceRecord, System::LogSourceCatalog::size> records{};

    static void initialize() noexcept
    {
        for_each_type<typename System::LogSourceCatalog::EntryTypes>([]<typename Entry> {
            using SourceT = typename Entry::Declaration;
            constexpr auto id = Entry::local_id;
            sources[id.index()].store(
                static_cast<std::uint8_t>(System::template log_source_compile_level<SourceT>),
                std::memory_order_relaxed);
            records[id.index()] = {
                .source = id,
                .last_status = Status::Ok,
            };
        });
        for_each_type<typename System::LogDomainCatalog::EntryTypes>([]<typename Entry> {
            using DomainT = typename Entry::Declaration;
            constexpr auto id = Entry::local_id;
            domains[id.index()].store(
                static_cast<std::uint8_t>(System::template log_domain_compile_level<DomainT>),
                std::memory_order_relaxed);
        });
    }

    [[nodiscard]] static bool accepts(SourceId source, DomainId domain, Level level) noexcept
    {
        if (source.valid() && !at_least(level, static_cast<Level>(sources[source.index()].load(
                                                   std::memory_order_acquire)))) {
            return false;
        }
        return !domain.valid() || at_least(level, static_cast<Level>(domains[domain.index()].load(
                                                      std::memory_order_acquire)));
    }
};

template <typename System>
[[nodiscard]] std::optional<DomainId> resolve_domain(const void* token) noexcept
{
    std::optional<DomainId> result{};
    for_each_type<typename System::LogDomainCatalog::EntryTypes>([&]<typename Entry> {
        using DomainT = typename Entry::Declaration;
        if (token == &type_token<DomainT>) {
            result = System::LogDomainCatalog::template Entry<DomainT>::local_id;
        }
    });
    return result;
}

[[nodiscard]] inline Timestamp capture_timestamp() noexcept
{
    return static_cast<Timestamp>(k_ticks_to_us_floor64(k_uptime_ticks()));
}

template <typename Facility> [[nodiscard]] Result<void> request_processing(bool from_isr) noexcept
{
#if defined(CONFIG_ZTEST)
    if (Facility::test_hold_processor.load(std::memory_order_acquire)) {
        return {};
    }
#endif
    if (Facility::processor_pending.exchange(true, std::memory_order_acq_rel)) {
        return {};
    }
    if (Facility::schedule_processor == nullptr) {
        Facility::processor_pending.store(false, std::memory_order_release);
        return {};
    }
    auto submitted = Facility::schedule_processor(from_isr);
    if (!submitted) {
        Facility::processor_pending.store(false, std::memory_order_release);
        return submitted;
    }
    return {};
}

template <typename System>
[[nodiscard]] Result<Receipt, Error> capture_resolved(const CaptureRequest& request,
                                                      Operation operation, SourceId source,
                                                      DomainId domain) noexcept
{
    using Facility = typename System::LogFacility;

    if (!Facility::ready.load(std::memory_order_acquire) ||
        !Facility::accepting.load(std::memory_order_acquire)) {
        return fail<Error>({.status = solar::Status::NotReady,
                            .reason = Reason::CaptureClosed,
                            .operation = operation,
                            .source = source,
                            .level = request.level});
    }
    const bool isr = kernel::in_isr();
    if ((operation == Operation::IsrCapture) != isr) {
        return fail<Error>({.status = solar::Status::Invalid,
                            .reason = Reason::InvalidContext,
                            .operation = operation,
                            .source = source,
                            .level = request.level});
    }
    if (!any_runtime_route_accepts<typename Facility::Routes>(request.level) ||
        !RuntimeFilters<System>::accepts(source, domain, request.level)) {
        auto guard = Facility::lock.acquire();
        ++Facility::record.attempted;
        ++Facility::record.runtime_filtered;
        if (source.valid()) {
            ++RuntimeFilters<System>::records[source.index()].attempted;
            ++RuntimeFilters<System>::records[source.index()].filtered;
        }
        return Receipt{.disposition = Disposition::RuntimeFiltered};
    }
    if (sizeof(RecordHeader) + request.payload_size > CONFIG_SOLAR_LOG_MAX_RECORD_BYTES) {
        return fail<Error>({.status = solar::Status::MessageTooLarge,
                            .reason = Reason::RecordTooLarge,
                            .operation = operation,
                            .source = source,
                            .level = request.level});
    }

    std::optional<kernel::SpinLock::Guard> guard{};
    if (operation == Operation::Capture) {
        guard.emplace(Facility::lock.acquire());
    } else {
        auto attempted = Facility::lock.try_acquire();
        if (!attempted) {
            return fail<Error>({.status = solar::Status::WouldBlock,
                                .reason = Reason::Contended,
                                .operation = operation,
                                .source = source,
                                .level = request.level});
        }
        guard.emplace(std::move(*attempted));
    }

    ++Facility::record.attempted;
    if (source.valid()) {
        ++RuntimeFilters<System>::records[source.index()].attempted;
    }
    const auto timestamp = capture_timestamp();
    RecordHeader header{
        .sequence = Facility::record.next_sequence,
        .timestamp = timestamp,
        .source = source,
        .domain = domain,
        .platform_source = request.platform_source,
        .platform_domain = request.platform_domain,
        .level = request.level,
        .context = isr ? ContextKind::Isr : request.context,
        .correlation = request.correlation,
        .callsite = request.callsite,
        .origin = request.origin,
        .encoding = request.encoding,
        .payload_size = request.payload_size,
        .flags = request.flags,
    };
    if (Facility::record.preceding_loss) {
        header.flags |= flag(RecordFlag::PrecedingLoss);
    }
    std::array<std::byte, CONFIG_SOLAR_LOG_MAX_RECORD_BYTES> encoded{};
    std::memcpy(encoded.data(), &header, sizeof(header));
    std::memcpy(encoded.data() + sizeof(header), request.payload.data(), request.payload_size);
    const auto encoded_size = sizeof(header) + request.payload_size;
    const auto elevated = at_least(request.level, Level::Warning);
    const auto emergency = request.level == Level::Fatal;
    const auto admission_limit =
        emergency ? std::size_t{CONFIG_SOLAR_LOG_INGRESS_BYTES}
        : elevated
            ? std::size_t{CONFIG_SOLAR_LOG_INGRESS_BYTES - CONFIG_SOLAR_LOG_EMERGENCY_BYTES}
            : std::size_t{CONFIG_SOLAR_LOG_INGRESS_BYTES - CONFIG_SOLAR_LOG_ELEVATED_RESERVE_BYTES -
                          CONFIG_SOLAR_LOG_EMERGENCY_BYTES};
    if (!Facility::ingress.push(std::span<const std::byte>{encoded}.first(encoded_size),
                                admission_limit)) {
        ++Facility::record.dropped;
        Facility::record.preceding_loss = true;
        Facility::record.last_status = Status::NoBuffer;
        if (source.valid()) {
            auto& source_record = RuntimeFilters<System>::records[source.index()];
            ++source_record.dropped;
            source_record.last_status = Status::NoBuffer;
            source_record.last_failure_at = timestamp;
        }
        return fail<Error>({.status = solar::Status::NoBuffer,
                            .reason = Reason::CapacityExhausted,
                            .operation = operation,
                            .source = source,
                            .level = request.level});
    }
    ++Facility::record.next_sequence;
    ++Facility::record.captured;
    if (source.valid()) {
        ++RuntimeFilters<System>::records[source.index()].captured;
        RuntimeFilters<System>::records[source.index()].last_status = Status::Ok;
    }
    Facility::record.preceding_loss = false;
    Facility::record.ingress_used = Facility::ingress.used();
    const auto flags = static_cast<RecordFlags>(
        header.flags | (elevated ? flag(RecordFlag::Elevated) : RecordFlags{}) |
        (emergency ? flag(RecordFlag::Emergency) : RecordFlags{}));
    guard.reset();
    (void)request_processing<Facility>(isr);
    return Receipt{
        .disposition = emergency  ? Disposition::Emergency
                       : elevated ? Disposition::Elevated
                                  : Disposition::Captured,
        .sequence = header.sequence,
        .timestamp = timestamp,
        .encoded_size = static_cast<std::uint16_t>(encoded_size),
        .flags = flags,
    };
}

template <typename System, typename SourceT>
[[nodiscard]] Result<Receipt, Error> capture(const CaptureRequest& request,
                                             Operation operation) noexcept
{
    constexpr auto source = System::LogSourceCatalog::template Entry<SourceT>::local_id;
    const auto domain = resolve_domain<System>(request.domain_token);
    if (!domain) {
        return fail<Error>({.status = solar::Status::NotFound,
                            .reason = Reason::NotRegistered,
                            .operation = operation,
                            .source = source,
                            .level = request.level});
    }
    return capture_resolved<System>(request, operation, source, *domain);
}

struct CbprintfOutput
{
    std::span<char> output{};
    std::size_t size{};
    bool truncated{};
};

inline int cbprintf_out(int character, void* context)
{
    auto& output = *static_cast<CbprintfOutput*>(context);
    if (output.size < output.output.size()) {
        output.output[output.size++] = static_cast<char>(character);
    } else {
        output.truncated = true;
    }
    return character;
}

[[nodiscard]] inline Result<std::size_t, Error> render_message(RecordView record,
                                                               std::span<char> output) noexcept
{
    switch (record.header.encoding) {
    case Encoding::SolarArguments:
        return render_native(record, output);
    case Encoding::Text: {
        const auto size = std::min(record.payload.size(), output.size());
        std::memcpy(output.data(), record.payload.data(), size);
        if (size != record.payload.size()) {
            return fail<Error>({.status = solar::Status::MessageTooLarge,
                                .reason = Reason::RecordTooLarge,
                                .operation = Operation::Render});
        }
        return size;
    }
    case Encoding::ZephyrCbprintf: {
        if (record.payload.size() < sizeof(PlatformPayloadHeader)) {
            return fail<Error>({.status = solar::Status::ProtocolError,
                                .reason = Reason::InternalInvariant,
                                .operation = Operation::Render});
        }
        PlatformPayloadHeader platform{};
        std::memcpy(&platform, record.payload.data(), sizeof(platform));
        if (sizeof(platform) + platform.package_size + platform.data_size > record.payload.size()) {
            return fail<Error>({.status = solar::Status::ProtocolError,
                                .reason = Reason::InternalInvariant,
                                .operation = Operation::Render});
        }
        if (platform.package_size == 0) {
            constexpr std::string_view label{"<hexdump>"};
            if (label.size() > output.size()) {
                return fail<Error>({.status = solar::Status::MessageTooLarge,
                                    .reason = Reason::RecordTooLarge,
                                    .operation = Operation::Render});
            }
            std::memcpy(output.data(), label.data(), label.size());
            return label.size();
        }
        CbprintfOutput state{.output = output};
        const auto result =
            cbpprintf(reinterpret_cast<cbprintf_cb>(cbprintf_out), &state,
                      const_cast<std::byte*>(record.payload.data() + sizeof(platform)));
        if (result < 0 || state.truncated) {
            return fail<Error>(
                {.status = state.truncated ? Status::MessageTooLarge : Status::ProtocolError,
                 .reason = state.truncated ? Reason::RecordTooLarge : Reason::InternalInvariant,
                 .operation = Operation::Render});
        }
        return state.size;
    }
    case Encoding::Hexdump:
        if (record.payload.size() < sizeof(HexdumpPayloadHeader)) {
            return fail<Error>({.status = solar::Status::ProtocolError,
                                .reason = Reason::InternalInvariant,
                                .operation = Operation::Render});
        } else {
            HexdumpPayloadHeader header{};
            std::memcpy(&header, record.payload.data(), sizeof(header));
            if (sizeof(header) + header.label_size + header.data_size > record.payload.size()) {
                return fail<Error>({.status = solar::Status::ProtocolError,
                                    .reason = Reason::InternalInvariant,
                                    .operation = Operation::Render});
            }
            TextWriter writer{output};
            writer.append({reinterpret_cast<const char*>(record.payload.data() + sizeof(header)),
                           header.label_size});
            if (header.label_size != 0) {
                writer.append(": ");
            }
            constexpr char digits[] = "0123456789abcdef";
            const auto* data = record.payload.data() + sizeof(header) + header.label_size;
            for (std::size_t index{}; index < header.data_size; ++index) {
                const auto value = std::to_integer<std::uint8_t>(data[index]);
                if (index != 0) {
                    writer.append(' ');
                }
                writer.append(digits[value >> 4U]);
                writer.append(digits[value & 0x0FU]);
            }
            if (writer.truncated()) {
                return fail<Error>({.status = solar::Status::MessageTooLarge,
                                    .reason = Reason::RecordTooLarge,
                                    .operation = Operation::Render});
            }
            return writer.size();
        }
    }
    return fail<Error>({.status = solar::Status::ProtocolError,
                        .reason = Reason::InternalInvariant,
                        .operation = Operation::Render});
}

template <typename Sink> [[nodiscard]] Result<void> initialize_sink() noexcept
{
    if constexpr (requires { Sink::init(); }) {
        auto result = Sink::init();
        static_assert(VoidResult<decltype(result)>,
                      "SOLAR_DIAGNOSTIC_LOG_SINK_INIT: sink init must return "
                      "Result<void, ErrorType>");
        return result ? Result<void>{}
                      : Result<void>{fail<solar::Error>({.status = status_of(result.error())})};
    }
    return {};
}

template <typename System, typename Sink>
[[nodiscard]] Result<void> consume_sink(RecordView record, std::string_view rendered) noexcept
{
    if constexpr (requires { Sink::template consume<System>(record, rendered); }) {
        auto result = Sink::template consume<System>(record, rendered);
        static_assert(VoidResult<decltype(result)>);
        return result ? Result<void>{}
                      : Result<void>{fail<solar::Error>({.status = status_of(result.error())})};
    } else if constexpr (requires { Sink::consume(record, rendered); }) {
        auto result = Sink::consume(record, rendered);
        static_assert(VoidResult<decltype(result)>);
        return result ? Result<void>{}
                      : Result<void>{fail<solar::Error>({.status = status_of(result.error())})};
    } else if constexpr (requires { Sink::consume(record); }) {
        auto result = Sink::consume(record);
        static_assert(VoidResult<decltype(result)>);
        return result ? Result<void>{}
                      : Result<void>{fail<solar::Error>({.status = status_of(result.error())})};
    } else if constexpr (requires { Sink::write(rendered); }) {
        auto result = Sink::write(rendered);
        static_assert(VoidResult<decltype(result)>);
        return result ? Result<void>{}
                      : Result<void>{fail<solar::Error>({.status = status_of(result.error())})};
    } else {
        static_assert(solar::detail::dependent_false_v<Sink>,
                      "SOLAR_DIAGNOSTIC_LOG_SINK_CONCEPT: sink requires static consume or write");
    }
}

template <typename System>
[[nodiscard]] Result<void> process_one(const StoredRecord& stored) noexcept
{
    using Facility = typename System::LogFacility;
    const auto record = stored.view();
    Result<void> result{};
    std::array<char, CONFIG_SOLAR_LOG_RENDER_BUFFER_BYTES> rendered{};
    std::optional<std::size_t> rendered_size{};

    for_each_route<typename Facility::Routes>([&]<typename Route> {
        const auto route_minimum =
            static_cast<Level>(RouteRuntime<Route>::minimum.load(std::memory_order_acquire));
        if (!at_least(record.header.level, route_minimum)) {
            auto guard = Facility::lock.acquire();
            ++RouteRuntime<Route>::record.filtered;
            return;
        }
        if (Facility::panic_mode.load(std::memory_order_acquire) &&
            !route_traits<Route>::panic_safe &&
            !std::is_same_v<typename route_traits<Route>::Sink, RetainedHistory>) {
            return;
        }
        using Sink = typename route_traits<Route>::Sink;
        if constexpr (std::is_same_v<Sink, RetainedHistory>) {
#if defined(CONFIG_SOLAR_LOG_HISTORY)
            auto guard = Facility::lock.acquire();
            const auto appended = Facility::history.append(stored);
            Facility::record.history_used = Facility::history.used();
            Facility::record.history_evicted += appended.evicted;
            if (!appended.stored) {
                ++Facility::record.history_unstored;
            }
#endif
        } else {
            if constexpr (!route_traits<Route>::encoded) {
                if (!rendered_size) {
                    auto rendering = render_message(record, rendered);
                    if (!rendering) {
                        result = fail<solar::Error>({.status = status_of(rendering.error())});
                        return;
                    }
                    rendered_size = *rendering;
                }
            }
            auto consumed = consume_sink<System, Sink>(
                record, route_traits<Route>::encoded
                            ? std::string_view{}
                            : std::string_view{rendered.data(), *rendered_size});
            if (!consumed) {
                result = consumed;
                auto guard = Facility::lock.acquire();
                ++Facility::record.sink_failures;
                Facility::record.last_status = status_of(consumed.error());
                ++RouteRuntime<Route>::record.failed;
                RouteRuntime<Route>::record.last_status = status_of(consumed.error());
            } else {
                auto guard = Facility::lock.acquire();
                ++RouteRuntime<Route>::record.accepted;
                RouteRuntime<Route>::record.last_status = Status::Ok;
            }
        }
    });
    {
        auto guard = Facility::lock.acquire();
        ++Facility::record.processed;
    }
    return result;
}

template <typename Facility> [[nodiscard]] Result<void> drain() noexcept
{
    Result<void> result{};
    while (true) {
        std::array<std::byte, CONFIG_SOLAR_LOG_MAX_RECORD_BYTES> encoded{};
        std::size_t size{};
        {
            auto guard = Facility::lock.acquire();
            if (!Facility::ingress.pop(encoded, size)) {
                Facility::processor_pending.store(false, std::memory_order_release);
                Facility::record.ingress_used = Facility::ingress.used();
                break;
            }
            Facility::record.ingress_used = Facility::ingress.used();
        }
        if (size < sizeof(RecordHeader)) {
            result = fail<solar::Error>({.status = solar::Status::ProtocolError});
            continue;
        }
        StoredRecord stored{};
        std::memcpy(&stored.header, encoded.data(), sizeof(stored.header));
        if (sizeof(stored.header) + stored.header.payload_size != size) {
            result = fail<solar::Error>({.status = solar::Status::ProtocolError});
            continue;
        }
        std::memcpy(stored.payload.data(), encoded.data() + sizeof(stored.header),
                    stored.header.payload_size);
        if (Facility::process_record != nullptr) {
            auto processed = Facility::process_record(stored);
            if (!processed && result) {
                result = processed;
            }
        }
    }
    return result;
}

template <typename System> [[nodiscard]] FacilityRecord facility_record() noexcept
{
    using Facility = typename System::LogFacility;
    auto guard = Facility::lock.acquire();
    return Facility::record;
}

template <typename System>
[[nodiscard]] HistoryPage read_history(Cursor cursor, std::span<Record> output) noexcept
{
    using Facility = typename System::LogFacility;
    auto guard = Facility::lock.acquire();
    return Facility::history.read(cursor, output);
}

template <typename System> [[nodiscard]] Result<Record, Error> latest_history() noexcept
{
    using Facility = typename System::LogFacility;
    auto guard = Facility::lock.acquire();
    return Facility::history.latest();
}

#endif

} // namespace solar::log::detail

#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_LOG)
namespace solar::log
{

template <typename Architecture>
template <typename System>
void Facility<Architecture>::activate_runtime() noexcept
{
    detail::RuntimeFilters<System>::initialize();
    schedule_processor = [](bool from_isr) noexcept -> Result<void> {
        auto submitted =
            execution::detail::submit_registration<System, ProcessorRegistration>(from_isr);
        return submitted ? Result<void>{}
                         : Result<void>{fail<solar::Error>({.status = submitted.error().status})};
    };
    process_record = [](const detail::StoredRecord& stored) noexcept -> Result<void> {
        return detail::process_one<System>(stored);
    };
    platform::install({
        .capture =
            [](const platform::Record& platform_record) noexcept {
                auto request = platform_record.request;
                request.platform_source = platform_record.source;
                request.platform_domain = platform_record.domain;
                const auto operation = request.context == ContextKind::Isr ? Operation::IsrCapture
                                                                           : Operation::TryCapture;
                auto captured = detail::capture_resolved<System>(
                    request, operation, SourceId{},
                    System::LogDomainCatalog::template Entry<domain::Unclassified>::local_id);
                (void)captured;
            },
        .panic =
            []() noexcept {
                panic_mode.store(true, std::memory_order_release);
                {
                    auto guard = lock.acquire();
                    record.panic = true;
                }
                (void)flush();
            },
    });
    (void)detail::request_processing<Facility>(false);
}

template <typename Architecture> Result<void> Facility<Architecture>::init() noexcept
{
    {
        auto guard = lock.acquire();
        ingress.clear();
        history.clear();
        record = {
            .ingress_capacity = CONFIG_SOLAR_LOG_INGRESS_BYTES,
#if defined(CONFIG_SOLAR_LOG_HISTORY)
            .history_capacity = CONFIG_SOLAR_LOG_HISTORY_BYTES,
#endif
            .next_sequence = 1,
            .last_status = Status::Ok,
            .ready = true,
            .accepting = true,
        };
    }
    ready.store(true, std::memory_order_release);
    accepting.store(true, std::memory_order_release);
    processor_pending.store(false, std::memory_order_release);
    panic_mode.store(false, std::memory_order_release);

    Result<void> initialized{};
    detail::for_each_route<Routes>([&]<typename Route> {
        using Sink = typename detail::route_traits<Route>::Sink;
        detail::RouteRuntime<Route>::minimum.store(
            static_cast<std::uint8_t>(detail::route_traits<Route>::minimum),
            std::memory_order_relaxed);
        detail::RouteRuntime<Route>::record = {};
        if constexpr (!std::is_same_v<Sink, RetainedHistory>) {
            if (initialized) {
                initialized = detail::initialize_sink<Sink>();
            }
        }
    });
    return initialized;
}

template <typename Architecture> Result<void> Facility<Architecture>::start() noexcept
{
    accepting.store(true, std::memory_order_release);
    auto guard = lock.acquire();
    record.accepting = true;
    return {};
}

template <typename Architecture> Result<void> Facility<Architecture>::run_processor() noexcept
{
    return detail::drain<Facility>();
}

template <typename Architecture> Result<void> Facility<Architecture>::flush() noexcept
{
    auto result = detail::drain<Facility>();
    detail::for_each_route<Routes>([&]<typename Route> {
        using Sink = typename detail::route_traits<Route>::Sink;
        if constexpr (!std::is_same_v<Sink, RetainedHistory> && requires { Sink::flush(); }) {
            Sink::flush();
        }
    });
    return result;
}

template <typename Architecture> Result<void> Facility<Architecture>::stop() noexcept
{
    accepting.store(false, std::memory_order_release);
    {
        auto guard = lock.acquire();
        record.accepting = false;
        if constexpr (std::is_same_v<ProcessorStopPolicy, stop::CancelPending>) {
            ingress.clear();
            record.ingress_used = 0;
            return {};
        }
    }
    return flush();
}

template <typename Architecture> Result<void> Facility<Architecture>::deinit() noexcept
{
    ready.store(false, std::memory_order_release);
    schedule_processor = nullptr;
    process_record = nullptr;
    auto guard = lock.acquire();
    record.ready = false;
    return {};
}

} // namespace solar::log
#endif
