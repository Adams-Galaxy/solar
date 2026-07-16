#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

#include "solar/catalog/catalog.hpp"
#include "solar/component.hpp"
#include "solar/core/status.hpp"

namespace solar::events
{

struct Tag
{};

struct ProcessorTag
{};

struct IdentityDomain
{};

struct ProcessorIdentityDomain
{};

using Id = StableId<IdentityDomain>;
using LocalId = solar::LocalId<Tag>;
using ProcessorLocalId = solar::LocalId<ProcessorTag>;
using Sequence = std::uint64_t;
using CorrelationId = std::uint64_t;

enum class Severity : std::uint8_t
{
    Trace,
    Informational,
    Warning,
    Error,
    Critical,
};

struct Domain
{
    std::uint16_t id{};
    std::string_view name{};

    constexpr bool operator==(const Domain&) const = default;
};

namespace domain
{
inline constexpr Domain Lifecycle{1, "lifecycle"};
inline constexpr Domain Scheduling{2, "scheduling"};
inline constexpr Domain Communication{3, "communication"};
inline constexpr Domain Storage{4, "storage"};
inline constexpr Domain Power{5, "power"};
inline constexpr Domain Safety{6, "safety"};
inline constexpr Domain Device{7, "device"};
inline constexpr Domain Resource{8, "resource"};
} // namespace domain

struct Descriptor
{
    std::string_view name;
    Severity severity{Severity::Informational};
    Domain domain{domain::Resource};
    std::string_view description{};
    std::optional<Id> stable_id{};
    std::uint16_t version{1};
};

struct ProcessorDescriptor
{
    std::string_view name;
    std::uint16_t version{1};
};

using DescriptorCatalogView = catalog::BasicDescriptorView<Tag, Descriptor>;
using ProcessorCatalogView = catalog::BasicDescriptorView<ProcessorTag, ProcessorDescriptor>;

enum class SourceKind : std::uint8_t
{
    Application,
    Component,
    Builtin,
};

struct SourceId
{
    SourceKind kind{SourceKind::Application};
    component::LocalId component{};

    constexpr bool operator==(const SourceId&) const = default;
};

enum class ContextKind : std::uint8_t
{
    Thread,
    Isr,
    Infrastructure,
};

enum class TimestampQuality : std::uint8_t
{
    Monotonic,
    CycleDomain,
    Degraded,
};

enum class CaptureDisposition : std::uint8_t
{
    Captured,
    SampledOut,
    RateLimited,
    Aggregated,
};

enum class CaptureKind : std::uint8_t
{
    EveryOccurrence,
    SampleEvery,
    RateLimited,
    AggregateCount,
};

enum class RetentionKind : std::uint8_t
{
    Transient,
    Buffered,
    Critical,
    Persistent,
};

enum class LogIntent : std::uint8_t
{
    Default,
    Suppress,
    Force,
};

struct ObserveOptions
{
    CorrelationId correlation{};
    LogIntent log_intent{LogIntent::Default};
};

struct Receipt
{
    CaptureDisposition disposition{CaptureDisposition::Captured};
    Sequence sequence{};
    std::int64_t timestamp{};
    std::uint32_t occurrence_count{1};
    bool materialized{true};
};

enum class Operation : std::uint8_t
{
    Observe,
    TryObserve,
    ObserveIsr,
    Query,
    Process,
    Stop,
};

enum class Reason : std::uint8_t
{
    None,
    NotReady,
    Disabled,
    NotRegistered,
    CaptureClosed,
    CaptureFull,
    WouldBlock,
    Timeout,
    InvalidContext,
    IsrUnsupported,
    RequiredCaptureExhausted,
    AggregationKeysFull,
    ExecutorUnavailable,
    HistoryEmpty,
    HistoryStale,
    DecodeMismatch,
    ProcessorFailed,
    InternalInvariant,
};

struct Error
{
    Status status{Status::Error};
    Reason reason{Reason::None};
    Operation operation{Operation::Query};
    LocalId event{};
    ProcessorLocalId processor{};
    SourceId source{};
    std::uint64_t rejected{};
    int native_error{};
};

[[nodiscard]] constexpr Status status_of(const Error& error) noexcept
{
    return error.status;
}

enum class RecordFlag : std::uint16_t
{
    None = 0,
    Aggregated = 1U << 0U,
    LostBefore = 1U << 1U,
    Critical = 1U << 2U,
    Recovery = 1U << 3U,
};

[[nodiscard]] constexpr RecordFlag operator|(RecordFlag left, RecordFlag right) noexcept
{
    return static_cast<RecordFlag>(static_cast<std::uint16_t>(left) |
                                   static_cast<std::uint16_t>(right));
}

struct RecordHeader
{
    LocalId event{};
    SourceId source{};
    Sequence sequence{};
    std::int64_t timestamp{};
    ContextKind context{ContextKind::Thread};
    TimestampQuality timestamp_quality{TimestampQuality::Monotonic};
    Severity severity{Severity::Informational};
    CorrelationId correlation{};
    LogIntent log_intent{LogIntent::Default};
    std::uint32_t occurrence_count{1};
    std::uint32_t lost_before{};
    std::uint16_t payload_size{};
    std::uint16_t schema_version{1};
    RecordFlag flags{RecordFlag::None};
};

struct RecordView
{
    RecordHeader header{};
    std::span<const std::byte> payload{};
};

#if defined(CONFIG_SOLAR_EVENTS)
inline constexpr std::size_t max_record_payload_bytes = CONFIG_SOLAR_EVENTS_MAX_PAYLOAD_BYTES;
#else
inline constexpr std::size_t max_record_payload_bytes = 1;
#endif

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

struct Cursor
{
    Sequence next_sequence{1};
};

struct HistoryPage
{
    Cursor next{};
    std::size_t written{};
    std::size_t available{};
    std::uint64_t evicted_before{};
    bool stale{};
};

struct DescriptorView
{
    LocalId local_id{};
    Descriptor descriptor{};
    OwnerView owner{};
    OriginKind origin{OriginKind::Direct};
    std::size_t payload_size{};
    std::size_t payload_alignment{};
    CaptureKind capture{CaptureKind::EveryOccurrence};
    RetentionKind retention{RetentionKind::Buffered};
    std::size_t critical_reservation{};
    bool payload_free{};
    bool isr_compatible{};
};

struct EventRecord
{
    LocalId event{};
    Status last_status{Status::NotReady};
    Reason last_failure{Reason::None};
    Sequence last_sequence{};
    std::int64_t last_timestamp{};
    std::uint64_t attempts{};
    std::uint64_t captured{};
    std::uint64_t sampled{};
    std::uint64_t rate_limited{};
    std::uint64_t aggregated{};
    std::uint64_t aggregation_rejected{};
    std::uint64_t ingress_rejected{};
    std::uint64_t retained{};
    std::uint64_t history_evicted{};
    std::uint64_t known_lost{};
    std::uint64_t processor_failures{};
    std::uint32_t consecutive{};
    bool overflow_latched{};
    bool condition_active{};
};

struct ProcessorRecord
{
    ProcessorLocalId processor{};
    LocalId event{};
    Status last_status{Status::NotReady};
    std::uint64_t offered{};
    std::uint64_t accepted{};
    std::uint64_t rejected{};
    std::uint64_t failed{};
};

struct ConditionRecord
{
    LocalId event{};
    SourceId source{};
    std::uint32_t consecutive{};
    Sequence last_sequence{};
    std::int64_t last_timestamp{};
    bool active{};
};

struct FacilityRecord
{
    Status last_status{Status::NotReady};
    std::uint64_t next_sequence{1};
    std::uint64_t accepted{};
    std::uint64_t processed{};
    std::uint64_t ingress_rejected{};
    std::uint64_t history_evicted{};
    std::uint64_t processor_failures{};
    std::uint32_t thread_pending{};
    std::uint32_t isr_pending{};
    std::uint32_t critical_pending{};
    std::size_t history_used{};
    std::size_t history_capacity{};
    bool ready{};
    bool accepting{};
    bool processor_pending{};
    bool overflow_latched{};
};

template <typename Event> using PayloadOf = typename Event::Payload;

template <typename Event>
concept EventDeclaration = requires {
    typename Event::Payload;
    { Event::descriptor } -> std::convertible_to<Descriptor>;
};

template <typename Event> inline constexpr bool payload_free_v = std::is_void_v<PayloadOf<Event>>;

namespace detail
{

template <typename Event, bool PayloadFree = payload_free_v<Event>> struct PayloadShape
{
    static constexpr std::size_t size = sizeof(PayloadOf<Event>);
    static constexpr std::size_t alignment = alignof(PayloadOf<Event>);
};

template <typename Event> struct PayloadShape<Event, true>
{
    static constexpr std::size_t size = 0;
    static constexpr std::size_t alignment = 1;
};

} // namespace detail

template <typename Event>
inline constexpr std::size_t payload_size_v = detail::PayloadShape<Event>::size;

template <typename Event>
inline constexpr std::size_t payload_alignment_v = detail::PayloadShape<Event>::alignment;

} // namespace solar::events
