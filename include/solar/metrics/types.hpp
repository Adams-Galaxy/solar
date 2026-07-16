#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>

#include "solar/catalog/catalog.hpp"
#include "solar/component.hpp"
#include "solar/core/status.hpp"

namespace solar::metrics
{

struct Tag
{};

struct IdentityDomain
{};

using Id = StableId<IdentityDomain>;
using LocalId = solar::LocalId<Tag>;

struct Descriptor
{
    std::string_view name;
    std::string_view description{};
    std::optional<Id> stable_id{};
    std::uint16_t version{1};
};

struct UnitDescriptor
{
    std::string_view symbol;
    std::string_view name;
};

using DescriptorCatalogView = catalog::BasicDescriptorView<Tag, Descriptor>;

enum class InstrumentKind : std::uint8_t
{
    Counter,
    Gauge,
    Distribution,
    Timer,
};

enum class ConcurrencyKind : std::uint8_t
{
    Atomic,
    SpinLocked,
    MutexProtected,
};

enum class OverflowKind : std::uint8_t
{
    Saturate,
    Reject,
    Wrap,
};

enum class UpdateDisposition : std::uint8_t
{
    Updated,
    Unchanged,
    Saturated,
    Wrapped,
};

enum class Operation : std::uint8_t
{
    Increment,
    Add,
    Set,
    Observe,
    RecordDuration,
    Get,
    TryGet,
    GetView,
    Reset,
    Query,
    Initialize,
    Stop,
};

enum class Reason : std::uint8_t
{
    None,
    NotReady,
    Disabled,
    NotRegistered,
    Closed,
    WouldBlock,
    InvalidContext,
    Overflow,
    InvalidNumeric,
    ConversionOverflow,
    ResetForbidden,
    UnsupportedView,
    ReducerFailure,
    ClockFailure,
    InternalInvariant,
};

struct Error
{
    Status status{Status::Error};
    Reason reason{Reason::None};
    Operation operation{Operation::Query};
    LocalId metric{};
    int native_error{};
};

[[nodiscard]] constexpr Status status_of(const Error& error) noexcept
{
    return error.status;
}

struct Update
{
    UpdateDisposition disposition{UpdateDisposition::Updated};
    std::uint64_t revision{};
    std::uint32_t epoch{};
    bool saturated{};
    bool wrapped{};
};

struct ReadingMetadata
{
    std::uint32_t epoch{};
    std::uint64_t revision{};
    std::uint64_t updates{};
    std::int64_t updated_at{};
    bool initialized{};
    bool saturated{};
    bool wrapped{};
    bool degraded{};
};

template <typename Value> struct CounterReading : ReadingMetadata
{
    Value value{};
};

template <typename Value> struct GaugeReading : ReadingMetadata
{
    Value value{};
};

template <typename Value> struct PointReading : ReadingMetadata
{
    Value value{};
    std::uint64_t count{};
};

template <typename Value, typename Sum> struct SummaryReading : ReadingMetadata
{
    std::uint64_t count{};
    Sum sum{};
    Value minimum{};
    Value maximum{};
    double mean{};
};

template <typename Value> struct MeanReading : ReadingMetadata
{
    std::uint64_t count{};
    Value latest{};
    double mean{};
};

template <typename Value, typename Sum, std::size_t Buckets>
struct HistogramReading : ReadingMetadata
{
    std::uint64_t count{};
    Sum sum{};
    std::array<std::uint64_t, Buckets> buckets{};
};

using ScalarValue = std::variant<std::int64_t, std::uint64_t, double, bool>;

enum class ViewKind : std::uint8_t
{
    Value,
    Count,
    Sum,
    Minimum,
    Maximum,
    Mean,
    Last,
    Bucket,
};

struct MetricViewRecord
{
    LocalId metric{};
    ViewKind view{ViewKind::Value};
    std::uint16_t view_index{};
    component::LocalId owner{};
    InstrumentKind instrument{InstrumentKind::Gauge};
    UnitDescriptor unit{};
    ScalarValue value{};
    std::uint32_t epoch{};
    std::uint64_t revision{};
    std::int64_t updated_at{};
    bool initialized{};
    bool saturated{};
    bool wrapped{};
};

struct MetricRecord
{
    LocalId metric{};
    Status last_status{Status::NotReady};
    Reason last_failure{Reason::None};
    ConcurrencyKind concurrency{ConcurrencyKind::MutexProtected};
    std::uint32_t epoch{};
    std::uint64_t revision{};
    std::uint64_t updates{};
    std::uint64_t rejected{};
    std::uint64_t contention{};
    std::uint64_t overflows{};
    std::uint64_t saturations{};
    std::uint64_t invalid_numeric{};
    std::uint64_t resets{};
    std::int64_t updated_at{};
    std::int64_t failure_at{};
    bool initialized{};
    bool saturated{};
    bool wrapped{};
    bool degraded{};
};

struct DescriptorView
{
    LocalId local_id{};
    Descriptor descriptor{};
    OwnerView owner{};
    OriginKind origin{OriginKind::Direct};
    UnitDescriptor unit{};
    InstrumentKind instrument{InstrumentKind::Gauge};
    ConcurrencyKind concurrency{ConcurrencyKind::MutexProtected};
    OverflowKind overflow{OverflowKind::Saturate};
    std::size_t value_size{};
    std::size_t value_alignment{};
    std::size_t state_size{};
    std::size_t view_count{};
    bool runtime_resettable{};
    bool timestamped{};
    bool isr_compatible{};
};

struct FacilityRecord
{
    Status last_status{Status::NotReady};
    std::uint64_t updates{};
    std::uint64_t rejected{};
    std::uint64_t contention{};
    std::uint64_t resets{};
    bool ready{};
    bool accepting{};
};

struct Cursor
{
    std::uint16_t metric{};
    std::uint16_t view{};
    std::uint32_t epoch{};
    bool epoch_valid{};
};

struct RecordPage
{
    Cursor next{};
    std::size_t written{};
    std::size_t available{};
    bool stale{};
};

} // namespace solar::metrics
