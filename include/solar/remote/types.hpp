#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "solar/core/bounded.hpp"
#include <utility>

#include "solar/catalog/catalog.hpp"
#include "solar/core/status.hpp"

namespace solar::remote
{

struct SchemaTag;
struct Tag;
struct DataTag;
struct ActionTag;
struct TopicTag;
struct StreamTag;
struct LinkTag;
struct InStreamGroupTag;

struct SchemaIdentityDomain;
struct DataIdentityDomain;
struct ActionIdentityDomain;
struct TopicIdentityDomain;
struct StreamIdentityDomain;
struct LinkIdentityDomain;
struct InStreamGroupIdentityDomain;

using TypeId = StableId<SchemaIdentityDomain>;
using DataId = StableId<DataIdentityDomain>;
using ActionId = StableId<ActionIdentityDomain>;
using TopicId = StableId<TopicIdentityDomain>;
using StreamId = StableId<StreamIdentityDomain>;
using LinkId = StableId<LinkIdentityDomain>;
using InStreamGroupId = StableId<InStreamGroupIdentityDomain>;
using FieldId = std::uint16_t;

template <typename Id> struct BasicDescriptor
{
    Id id{};
    std::string_view name;
    std::string_view description{};
    std::uint16_t version{1};
    std::optional<Id> stable_id{id};
};

using SchemaDescriptor = BasicDescriptor<TypeId>;
using DataDescriptor = BasicDescriptor<DataId>;
using ActionDescriptor = BasicDescriptor<ActionId>;
using TopicDescriptor = BasicDescriptor<TopicId>;
using StreamDescriptor = BasicDescriptor<StreamId>;
using LinkDescriptor = BasicDescriptor<LinkId>;
using InStreamGroupDescriptor = BasicDescriptor<InStreamGroupId>;

enum class InStreamCloseReason : std::uint8_t
{
    Closed = 0,
    Replaced = 1,
    Disconnect = 2,
    Reset = 3,
    Fault = 4,
    ConfigurationFailed = 5,
};

struct InStreamOpenContext
{
    std::uint32_t endpoint{};
    std::uint16_t link{};
    std::uint32_t token{};
    std::uint32_t minimum_interval_us{};
    std::uint16_t window{};
};

struct InStreamCloseContext
{
    std::uint32_t endpoint{};
    std::uint16_t link{};
    std::uint32_t token{};
    InStreamCloseReason reason{InStreamCloseReason::Closed};
};

template <typename Tag, typename Descriptor>
using DescriptorView = catalog::BasicDescriptorView<Tag, Descriptor>;

enum class Codec : std::uint8_t
{
    Cbor = 1,
    Packed = 2,
};

enum class SchemaShape : std::uint8_t
{
    Object = 0,
    StatusCode = 1,
    Enumeration = 2,
    /// A bounded, non-recursive element schema usable as an Array<T, N>
    /// element: fixed-width fields only, no optional members, no nested
    /// Array/Record. See remote-arrays-and-chunked-streaming.md.
    Record = 3,
};

enum class EnumOpenness : std::uint8_t
{
    Closed = 0,
    Open = 1,
};

enum class Capability : std::uint8_t
{
    Query = 1,
    Update = 2,
    Watch = 3,
    OutStream = 4,
    InStream = 5,
};

enum class Permission : std::uint8_t
{
    Observe = 1,
    Configure = 2,
    Control = 3,
    Admin = 4,
};

namespace permission
{
inline constexpr Permission Observe = Permission::Observe;
inline constexpr Permission Configure = Permission::Configure;
inline constexpr Permission Control = Permission::Control;
inline constexpr Permission Admin = Permission::Admin;
} // namespace permission

struct Empty
{};

enum class StatusCode : std::uint8_t
{
    Ok = 0,
    Error = 1,
    Invalid = 2,
    NotReady = 3,
    NotFound = 4,
    NotSupported = 5,
    Busy = 6,
    Already = 7,
    Timeout = 8,
    Cancelled = 9,
    NoMemory = 10,
    NoSpace = 11,
    WouldBlock = 12,
    Empty = 13,
    Interrupted = 14,
    Deadlock = 15,
    PermissionDenied = 16,
    NoBuffer = 17,
    MessageTooLarge = 18,
    ProtocolError = 19,
    Overflow = 20,
    DependencyFailed = 21,
    UnexpectedExit = 22,
};

[[nodiscard]] constexpr StatusCode encode_status(Status status)
{
    switch (status) {
#define SOLAR_DETAIL_REMOTE_STATUS(NAME)                                                           \
    case Status::NAME:                                                                             \
        return StatusCode::NAME
        SOLAR_DETAIL_REMOTE_STATUS(Ok);
        SOLAR_DETAIL_REMOTE_STATUS(Error);
        SOLAR_DETAIL_REMOTE_STATUS(Invalid);
        SOLAR_DETAIL_REMOTE_STATUS(NotReady);
        SOLAR_DETAIL_REMOTE_STATUS(NotFound);
        SOLAR_DETAIL_REMOTE_STATUS(NotSupported);
        SOLAR_DETAIL_REMOTE_STATUS(Busy);
        SOLAR_DETAIL_REMOTE_STATUS(Already);
        SOLAR_DETAIL_REMOTE_STATUS(Timeout);
        SOLAR_DETAIL_REMOTE_STATUS(Cancelled);
        SOLAR_DETAIL_REMOTE_STATUS(NoMemory);
        SOLAR_DETAIL_REMOTE_STATUS(NoSpace);
        SOLAR_DETAIL_REMOTE_STATUS(WouldBlock);
        SOLAR_DETAIL_REMOTE_STATUS(Empty);
        SOLAR_DETAIL_REMOTE_STATUS(Interrupted);
        SOLAR_DETAIL_REMOTE_STATUS(Deadlock);
        SOLAR_DETAIL_REMOTE_STATUS(PermissionDenied);
        SOLAR_DETAIL_REMOTE_STATUS(NoBuffer);
        SOLAR_DETAIL_REMOTE_STATUS(MessageTooLarge);
        SOLAR_DETAIL_REMOTE_STATUS(ProtocolError);
        SOLAR_DETAIL_REMOTE_STATUS(Overflow);
        SOLAR_DETAIL_REMOTE_STATUS(DependencyFailed);
        SOLAR_DETAIL_REMOTE_STATUS(UnexpectedExit);
#undef SOLAR_DETAIL_REMOTE_STATUS
    }
    return StatusCode::Error;
}

[[nodiscard]] constexpr std::optional<Status> decode_status(StatusCode code)
{
    switch (code) {
#define SOLAR_DETAIL_REMOTE_STATUS(NAME)                                                           \
    case StatusCode::NAME:                                                                         \
        return Status::NAME
        SOLAR_DETAIL_REMOTE_STATUS(Ok);
        SOLAR_DETAIL_REMOTE_STATUS(Error);
        SOLAR_DETAIL_REMOTE_STATUS(Invalid);
        SOLAR_DETAIL_REMOTE_STATUS(NotReady);
        SOLAR_DETAIL_REMOTE_STATUS(NotFound);
        SOLAR_DETAIL_REMOTE_STATUS(NotSupported);
        SOLAR_DETAIL_REMOTE_STATUS(Busy);
        SOLAR_DETAIL_REMOTE_STATUS(Already);
        SOLAR_DETAIL_REMOTE_STATUS(Timeout);
        SOLAR_DETAIL_REMOTE_STATUS(Cancelled);
        SOLAR_DETAIL_REMOTE_STATUS(NoMemory);
        SOLAR_DETAIL_REMOTE_STATUS(NoSpace);
        SOLAR_DETAIL_REMOTE_STATUS(WouldBlock);
        SOLAR_DETAIL_REMOTE_STATUS(Empty);
        SOLAR_DETAIL_REMOTE_STATUS(Interrupted);
        SOLAR_DETAIL_REMOTE_STATUS(Deadlock);
        SOLAR_DETAIL_REMOTE_STATUS(PermissionDenied);
        SOLAR_DETAIL_REMOTE_STATUS(NoBuffer);
        SOLAR_DETAIL_REMOTE_STATUS(MessageTooLarge);
        SOLAR_DETAIL_REMOTE_STATUS(ProtocolError);
        SOLAR_DETAIL_REMOTE_STATUS(Overflow);
        SOLAR_DETAIL_REMOTE_STATUS(DependencyFailed);
        SOLAR_DETAIL_REMOTE_STATUS(UnexpectedExit);
#undef SOLAR_DETAIL_REMOTE_STATUS
    }
    return std::nullopt;
}

template <std::size_t Capacity> using BoundedText = solar::BoundedText<Capacity>;
template <std::size_t Capacity> using BoundedBytes = solar::BoundedBytes<Capacity>;

/// A bounded array field: T must be a supported_scalar_v scalar/enum or a
/// SchemaShape::Record type (see declaration.hpp) -- checked where Array is
/// accepted as a field type, not here, since RecordSchemaType isn't visible
/// yet at this point in the include graph.
template <typename T, std::size_t Capacity> using Array = solar::BoundedVector<T, Capacity>;

enum class Operation : std::uint8_t
{
    Encode,
    Decode,
    Pack,
    Unpack,
    FrameEncode,
    FrameDecode,
    Generate,
    Initialize,
    Start,
    Stop,
    OpenLink,
    CloseLink,
    AdmitEvent,
    Receive,
    Transmit,
    Handshake,
    Dispatch,
    Publish,
    Query,
};

enum class Reason : std::uint8_t
{
    None,
    Disabled,
    NoSpace,
    Malformed,
    IntegrityFailure,
    UnsupportedVersion,
    UnsupportedKind,
    Oversized,
    MissingField,
    DuplicateField,
    InvalidValue,
    TrailingData,
    SchemaMismatch,
    NotReady,
    NotRegistered,
    InvalidContext,
    Busy,
    NoCapacity,
    LinkFailure,
    SessionClosed,
    Unauthorized,
    UnsupportedOperation,
    InternalInvariant,
};

struct Error
{
    Status status{Status::ProtocolError};
    Reason reason{Reason::None};
    Operation operation{Operation::Decode};
    std::uint32_t detail{};
};

enum class SessionState : std::uint8_t
{
    Disconnected,
    Negotiating,
    Active,
    Closing,
    Faulted,
};

struct ServiceRecord
{
    bool ready{};
    bool accepting{};
    std::uint32_t queued_events{};
    std::uint32_t event_high_water{};
    std::uint32_t processed_events{};
    std::uint32_t dropped_events{};
};

enum class OutputLane : std::uint8_t
{
    Control,
    Response,
    Important,
    Telemetry,
    Bulk,
};

struct LaneRecord
{
    bool occupied{};
    bool transmitting{};
    std::uint16_t depth{};
    std::uint16_t high_water{};
    std::uint32_t admitted{};
    std::uint32_t replaced{};
    std::uint32_t dropped{};
};

struct LinkRecord
{
    LinkId id{};
    SessionState session{SessionState::Disconnected};
    std::uint32_t epoch{};
    std::uint32_t received_frames{};
    std::uint32_t received_bytes{};
    std::uint32_t rejected_frames{};
    std::uint32_t transmitted_frames{};
    std::uint32_t transmitted_bytes{};
    std::uint32_t protocol_errors{};
    std::uint32_t connections{};
    std::uint32_t duplicate_requests{};
    std::uint32_t completed_requests{};
    std::uint32_t subscriptions{};
    std::array<LaneRecord, 5> lanes{};
    bool connected{};
    bool tx_in_flight{};
};

enum class WriteDisposition : std::uint8_t
{
    Accepted,
    ReplacedOlder,
    DroppedNewest,
    Coalesced,
    NoSubscribers,
};

struct WriteReceipt
{
    WriteDisposition disposition{WriteDisposition::Accepted};
    std::uint64_t sequence{};
    bool wake_queued{};
};

template <typename DataT> class Loan
{
  public:
    using Release = void (*)(std::uint16_t, std::uint16_t) noexcept;

    Loan() = default;
    Loan(const Loan&) = delete;
    Loan& operator=(const Loan&) = delete;

    Loan(Loan&& other) noexcept
        : bytes_(other.bytes_), slot_(other.slot_), generation_(other.generation_),
          release_(std::exchange(other.release_, nullptr))
    {}

    Loan& operator=(Loan&& other) noexcept
    {
        if (this != &other) {
            abandon();
            bytes_ = other.bytes_;
            slot_ = other.slot_;
            generation_ = other.generation_;
            release_ = std::exchange(other.release_, nullptr);
        }
        return *this;
    }

    ~Loan()
    {
        abandon();
    }

    [[nodiscard]] std::span<std::byte> data()
    {
        return bytes_;
    }

    [[nodiscard]] std::size_t capacity() const
    {
        return bytes_.size();
    }

    [[nodiscard]] explicit operator bool() const
    {
        return release_ != nullptr;
    }

    [[nodiscard]] std::uint16_t slot() const
    {
        return slot_;
    }

    [[nodiscard]] std::uint16_t generation() const
    {
        return generation_;
    }

    void commit_ownership()
    {
        release_ = nullptr;
        bytes_ = {};
    }

    static Loan make(std::span<std::byte> bytes, std::uint16_t slot, std::uint16_t generation,
                     Release release)
    {
        Loan loan;
        loan.bytes_ = bytes;
        loan.slot_ = slot;
        loan.generation_ = generation;
        loan.release_ = release;
        return loan;
    }

  private:
    void abandon()
    {
        if (release_ != nullptr) {
            release_(slot_, generation_);
            release_ = nullptr;
        }
    }

    std::span<std::byte> bytes_{};
    std::uint16_t slot_{};
    std::uint16_t generation_{};
    Release release_{};
};

[[nodiscard]] constexpr Status status_of(const Error& error) noexcept
{
    return error.status;
}

#if defined(CONFIG_SOLAR_REMOTE)
inline constexpr bool available = true;
#else
inline constexpr bool available = false;
#endif

} // namespace solar::remote
