#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "solar/catalog/catalog.hpp"
#include "solar/component.hpp"
#include "solar/core/status.hpp"

namespace solar::log
{

struct SourceTag;
struct DomainTag;

struct SourceIdentityDomain;
struct DomainIdentityDomain;

using SourceId = LocalId<SourceTag>;
using DomainId = LocalId<DomainTag>;
using StableSourceId = StableId<SourceIdentityDomain>;
using StableDomainId = StableId<DomainIdentityDomain>;
using Sequence = std::uint64_t;
using Timestamp = std::int64_t;
using CorrelationId = std::uint64_t;
using CallsiteId = std::uint32_t;

enum class Level : std::uint8_t
{
    Trace,
    Debug,
    Info,
    Notice,
    Warning,
    Error,
    Fatal,
};

[[nodiscard]] constexpr bool at_least(Level value, Level minimum) noexcept
{
    return static_cast<std::uint8_t>(value) >= static_cast<std::uint8_t>(minimum);
}

[[nodiscard]] constexpr const char* to_string(Level level) noexcept
{
    switch (level) {
    case Level::Trace:
        return "TRACE";
    case Level::Debug:
        return "DEBUG";
    case Level::Info:
        return "INFO";
    case Level::Notice:
        return "NOTICE";
    case Level::Warning:
        return "WARNING";
    case Level::Error:
        return "ERROR";
    case Level::Fatal:
        return "FATAL";
    }
    return "UNKNOWN";
}

enum class ContextKind : std::uint8_t
{
    Thread,
    Isr,
    EarlyBoot,
    Panic,
};

enum class Origin : std::uint8_t
{
    Solar,
    Zephyr,
    Event,
    RedirectedPrint,
    Infrastructure,
    Emergency,
};

enum class Encoding : std::uint8_t
{
    SolarArguments,
    ZephyrCbprintf,
    Text,
    Hexdump,
};

enum class RecordFlag : std::uint16_t
{
    None = 0,
    Truncated = 1U << 0U,
    Elevated = 1U << 1U,
    Emergency = 1U << 2U,
    PrecedingLoss = 1U << 3U,
    TimestampApproximate = 1U << 4U,
};

using RecordFlags = std::uint16_t;

[[nodiscard]] constexpr RecordFlags flag(RecordFlag value) noexcept
{
    return static_cast<RecordFlags>(value);
}

[[nodiscard]] constexpr bool has_flag(RecordFlags flags, RecordFlag value) noexcept
{
    return (flags & flag(value)) != 0;
}

struct RecordHeader
{
    Sequence sequence{};
    Timestamp timestamp{};
    SourceId source{};
    DomainId domain{};
    std::uint16_t platform_source{};
    std::uint8_t platform_domain{};
    Level level{Level::Info};
    ContextKind context{ContextKind::Thread};
    CorrelationId correlation{};
    CallsiteId callsite{};
    Origin origin{Origin::Solar};
    Encoding encoding{Encoding::SolarArguments};
    std::uint16_t payload_size{};
    RecordFlags flags{};
};

struct RecordView
{
    RecordHeader header{};
    std::span<const std::byte> payload{};
};

enum class Disposition : std::uint8_t
{
    Captured,
    CompileTimeFiltered,
    RuntimeFiltered,
    Suppressed,
    Elevated,
    Emergency,
};

struct Receipt
{
    Disposition disposition{Disposition::Captured};
    Sequence sequence{};
    Timestamp timestamp{};
    std::uint16_t encoded_size{};
    RecordFlags flags{};
};

enum class Operation : std::uint8_t
{
    Capture,
    TryCapture,
    IsrCapture,
    Process,
    Render,
    Sink,
    Flush,
    Query,
    Panic,
};

enum class Reason : std::uint8_t
{
    NotReady,
    Disabled,
    NotRegistered,
    CaptureClosed,
    CapacityExhausted,
    Contended,
    Timeout,
    RecordTooLarge,
    ArgumentEncoding,
    InvalidContext,
    IsrUnsupported,
    TimestampFailure,
    SinkFailure,
    HistoryEmpty,
    InternalInvariant,
};

struct Error
{
    Status status{Status::Error};
    Reason reason{Reason::InternalInvariant};
    Operation operation{Operation::Capture};
    SourceId source{};
    Level level{Level::Info};
};

struct CaptureOptions
{
    CorrelationId correlation{};
};

#if defined(CONFIG_SOLAR_LOG)
inline constexpr std::size_t max_record_payload_bytes = CONFIG_SOLAR_LOG_MAX_RECORD_BYTES;
#else
inline constexpr std::size_t max_record_payload_bytes = 1;
#endif

[[nodiscard]] constexpr CaptureOptions correlated(CorrelationId id) noexcept
{
    return {.correlation = id};
}

template <typename T> inline constexpr std::byte type_token{};

struct CaptureRequest
{
    Level level{Level::Info};
    ContextKind context{ContextKind::Thread};
    Origin origin{Origin::Solar};
    Encoding encoding{Encoding::SolarArguments};
    CorrelationId correlation{};
    CallsiteId callsite{};
    std::uint16_t platform_source{};
    std::uint8_t platform_domain{};
    const void* domain_token{};
    std::uint16_t payload_size{};
    RecordFlags flags{};
    alignas(std::max_align_t) std::array<std::byte, max_record_payload_bytes> payload{};
};

struct SourceRecord
{
    SourceId source{};
    std::uint64_t attempted{};
    std::uint64_t captured{};
    std::uint64_t filtered{};
    std::uint64_t dropped{};
    Status last_status{Status::Ok};
    Timestamp last_failure_at{};
};

struct SinkRecord
{
    std::uint32_t sink{};
    std::uint64_t accepted{};
    std::uint64_t filtered{};
    std::uint64_t failed{};
    Status last_status{Status::Ok};
};

struct FacilityRecord
{
    std::uint64_t attempted{};
    std::uint64_t captured{};
    std::uint64_t processed{};
    std::uint64_t runtime_filtered{};
    std::uint64_t dropped{};
    std::uint64_t truncated{};
    std::uint64_t sink_failures{};
    std::size_t ingress_used{};
    std::size_t ingress_capacity{};
    std::size_t history_used{};
    std::size_t history_capacity{};
    std::uint64_t history_evicted{};
    std::uint64_t history_unstored{};
    Sequence next_sequence{1};
    Status last_status{Status::Ok};
    bool ready{};
    bool accepting{};
    bool panic{};
    bool preceding_loss{};
};

struct Cursor
{
    Sequence next_sequence{1};
};

struct Record
{
    RecordHeader header{};
    std::array<std::byte, max_record_payload_bytes> payload{};

    [[nodiscard]] constexpr RecordView view() const noexcept
    {
        return {.header = header,
                .payload = std::span<const std::byte>{payload}.first(header.payload_size)};
    }
};

struct HistoryPage
{
    Cursor next{};
    std::size_t written{};
    std::size_t available{};
    std::uint64_t evicted_before{};
    bool stale{};
};

} // namespace solar::log
