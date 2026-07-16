#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "solar/catalog/catalog.hpp"
#include "solar/core/status.hpp"

namespace solar::inspection
{

struct Tag
{};

struct IdentityDomain
{};

using Id = StableId<IdentityDomain>;
using LocalId = solar::LocalId<Tag>;

enum class Subsystem : std::uint8_t
{
    Application,
    Graph,
    Lifecycle,
    Kernel,
    Bus,
    Parameters,
    Events,
    Metrics,
    Logging,
    Execution,
    Remote,
    Custom,
};

enum class Consistency : std::uint8_t
{
    PerRecord = 1U << 0U,
    StablePage = 1U << 1U,
    PointInTime = 1U << 2U,
};

using ConsistencySet = std::uint8_t;

[[nodiscard]] constexpr ConsistencySet consistency(Consistency value) noexcept
{
    return static_cast<ConsistencySet>(value);
}

enum class Synchronization : std::uint8_t
{
    None,
    Atomic,
    MutexCopy,
    SpinCopy,
    SourceDefined,
};

enum class Context : std::uint8_t
{
    Any,
    Thread,
    CooperativeThread,
    NoBlock,
    Isr,
};

enum class Cost : std::uint8_t
{
    Constant,
    LinearPage,
    HistoryRead,
    NativeDiagnostic,
};

enum class OperationCapability : std::uint8_t
{
    Query = 1U << 0U,
    Get = 1U << 1U,
    Text = 1U << 2U,
    Cbor = 1U << 3U,
    Remote = 1U << 4U,
};

using CapabilitySet = std::uint8_t;

[[nodiscard]] constexpr CapabilitySet capability(OperationCapability value) noexcept
{
    return static_cast<CapabilitySet>(value);
}

[[nodiscard]] constexpr CapabilitySet operator|(OperationCapability left,
                                                OperationCapability right) noexcept
{
    return capability(left) | capability(right);
}

struct Descriptor
{
    std::string_view name;
    std::string_view description{};
    Id stable_id{};
    std::uint16_t version{1};
    Subsystem subsystem{Subsystem::Custom};
    CapabilitySet capabilities{capability(OperationCapability::Query)};
    ConsistencySet consistency_modes{consistency(Consistency::PerRecord)};
    Synchronization synchronization{Synchronization::SourceDefined};
    Context context{Context::Thread};
    Cost cost{Cost::LinearPage};
    std::uint16_t maximum_page{};
    std::uint16_t record_size{};
    std::uint16_t query_size{};
    bool may_block{};
    bool expensive{};
    bool values_may_be_stale{};
};

using DescriptorView = catalog::BasicDescriptorView<Tag, Descriptor>;

struct Cursor
{
    LocalId collection{};
    std::uint32_t offset{};
    std::uint32_t revision{};

    constexpr bool operator==(const Cursor&) const = default;
};

struct PageRequest
{
    Cursor cursor{};
    std::size_t limit{};
};

enum class PageConsistency : std::uint8_t
{
    PerRecord,
    StablePage,
    PointInTime,
};

enum class Freshness : std::uint8_t
{
    Current,
    Stale,
    Mixed,
    Unknown,
};

struct LossInfo
{
    std::uint64_t count{};
    bool known{};
};

struct PageResult
{
    std::size_t written{};
    Cursor next{};
    bool has_more{};
    std::uint32_t revision{};
    PageConsistency consistency{PageConsistency::PerRecord};
    Freshness freshness{Freshness::Unknown};
    LossInfo loss{};
};

enum class Availability : std::uint8_t
{
    Available,
    Disabled,
    Unsupported,
    Unavailable,
    Busy,
};

enum class Operation : std::uint8_t
{
    Describe,
    Find,
    Visit,
    Query,
    FormatText,
    EncodeCbor,
};

enum class Reason : std::uint8_t
{
    None,
    Disabled,
    Unsupported,
    Unavailable,
    Busy,
    NotFound,
    StaleCursor,
    SourceFailed,
    InvalidRequest,
    NoSpace,
    InvalidContext,
};

struct Error
{
    Status status{Status::Error};
    Reason reason{Reason::SourceFailed};
    Operation operation{Operation::Query};
    LocalId collection{};
    std::uint32_t detail{};
};

struct BasicQuery
{
    PageRequest page{};
};

struct FormatResult
{
    std::size_t written{};
    std::size_t required{};
    bool truncated{};
};

} // namespace solar::inspection
