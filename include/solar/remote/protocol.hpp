#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "solar/remote/types.hpp"

namespace solar::remote::protocol
{

inline constexpr std::uint8_t major_version = 1;
inline constexpr std::uint8_t minor_version = 1;
inline constexpr std::size_t envelope_size = 32;
inline constexpr std::size_t crc_size = 4;
inline constexpr std::size_t subscription_policy_size = 8;
inline constexpr std::size_t in_stream_open_response_size = 12;
inline constexpr std::size_t in_stream_close_request_size = 4;
inline constexpr std::size_t in_stream_closed_size = 8;
inline constexpr std::size_t credit_grant_size = 8;
inline constexpr std::size_t batch_header_size = 4;
inline constexpr std::size_t introspection_summary_size = 24;
inline constexpr std::size_t collection_request_size = 4;
inline constexpr std::size_t collection_page_header_size = 8;
inline constexpr std::size_t collection_descriptor_header_size = 22;
inline constexpr std::size_t collection_query_request_size = 14;
inline constexpr std::size_t server_information_size = 56;
inline constexpr std::size_t manifest_request_size = 8;
inline constexpr std::size_t manifest_chunk_header_size = 8;
inline constexpr std::uint8_t batch_version = 1;

enum class Kind : std::uint8_t
{
    ClientHello = 1,
    ServerHello = 2,
    Error = 3,
    Request = 4,
    Response = 5,
    Cancel = 6,
    ResponseAck = 7,
    Subscribe = 8,
    Unsubscribe = 9,
    Credit = 10,
    Data = 11,
    Keepalive = 12,
    SessionReset = 13,
    Introspection = 14,
    InStreamClosed = 15,
};

enum class Flags : std::uint8_t
{
    None = 0,
    Fragmented = 1U << 0,
    Final = 1U << 1,
    PackedPayload = 1U << 2,
    ErrorPayload = 1U << 3,
};

enum class OperationKind : std::uint8_t
{
    Action = 0,
    Query = 1,
    Update = 2,
    InStream = 3,
};

enum class SubscriptionKind : std::uint8_t
{
    DataStream = 0,
    DataWatch = 1,
    Topic = 2,
    Stream = 3,
    DataInStream = 4,
};

enum class IntrospectionTarget : std::uint32_t
{
    ProtocolSummary = 0,
    Collections = 1,
    CollectionQuery = 2,
    ServerInformation = 3,
    Manifest = 4,
};

[[nodiscard]] constexpr Flags operator|(Flags left, Flags right) noexcept
{
    return static_cast<Flags>(static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
}

enum class ErrorCode : std::uint16_t
{
    UnsupportedVersion = 1,
    UnsupportedCapability = 2,
    SchemaMismatch = 3,
    MalformedFrame = 4,
    IntegrityFailure = 5,
    OversizedFrame = 6,
    OversizedMessage = 7,
    FragmentRejected = 8,
    UnknownTarget = 9,
    UnsupportedOperation = 10,
    DecodeFailure = 11,
    Unauthorized = 12,
    NotReady = 13,
    Busy = 14,
    NoCapacity = 15,
    RateRejected = 16,
    CreditViolation = 17,
    RequestExpired = 18,
    DuplicateResponseExpired = 19,
    Cancelled = 20,
    TimedOut = 21,
    SessionClosing = 22,
    InternalFailure = 23,
};

[[nodiscard]] constexpr Status status_of(ErrorCode error) noexcept
{
    switch (error) {
    case ErrorCode::UnsupportedVersion:
    case ErrorCode::UnsupportedCapability:
    case ErrorCode::UnsupportedOperation:
        return Status::NotSupported;
    case ErrorCode::SchemaMismatch:
    case ErrorCode::MalformedFrame:
    case ErrorCode::IntegrityFailure:
    case ErrorCode::FragmentRejected:
    case ErrorCode::DecodeFailure:
    case ErrorCode::CreditViolation:
        return Status::ProtocolError;
    case ErrorCode::OversizedFrame:
    case ErrorCode::OversizedMessage:
        return Status::MessageTooLarge;
    case ErrorCode::UnknownTarget:
        return Status::NotFound;
    case ErrorCode::Unauthorized:
        return Status::PermissionDenied;
    case ErrorCode::NotReady:
    case ErrorCode::SessionClosing:
        return Status::NotReady;
    case ErrorCode::Busy:
    case ErrorCode::RateRejected:
        return Status::Busy;
    case ErrorCode::NoCapacity:
        return Status::NoSpace;
    case ErrorCode::RequestExpired:
    case ErrorCode::DuplicateResponseExpired:
        return Status::NotFound;
    case ErrorCode::Cancelled:
        return Status::Cancelled;
    case ErrorCode::TimedOut:
        return Status::Timeout;
    case ErrorCode::InternalFailure:
        return Status::Error;
    }
    return Status::Error;
}

[[nodiscard]] constexpr bool valid(Kind kind) noexcept
{
    const auto value = static_cast<std::uint8_t>(kind);
    return value >= static_cast<std::uint8_t>(Kind::ClientHello) &&
           value <= static_cast<std::uint8_t>(Kind::InStreamClosed);
}

struct Envelope
{
    std::uint8_t major{major_version};
    std::uint8_t minor{minor_version};
    Kind kind{Kind::Keepalive};
    Flags flags{Flags::None};
    std::uint32_t session_epoch{};
    std::uint32_t frame_sequence{};
    std::uint32_t target{};
    std::uint32_t request_id{};
    std::uint16_t payload_size{};
    std::uint16_t fragment_id{};
    std::uint8_t fragment_index{};
    std::uint8_t fragment_count{1};
    std::uint32_t reserved{};

    constexpr bool operator==(const Envelope&) const = default;

    [[nodiscard]] constexpr OperationKind operation() const noexcept
    {
        return static_cast<OperationKind>(reserved & 0xFFU);
    }

    constexpr void set_operation(OperationKind operation_kind) noexcept
    {
        reserved = (reserved & 0xFFFFFF00U) | static_cast<std::uint8_t>(operation_kind);
    }

    [[nodiscard]] constexpr SubscriptionKind subscription() const noexcept
    {
        return static_cast<SubscriptionKind>(reserved & 0xFFU);
    }

    constexpr void set_subscription(SubscriptionKind subscription_kind) noexcept
    {
        reserved = (reserved & 0xFFFFFF00U) | static_cast<std::uint8_t>(subscription_kind);
    }
};

struct SubscriptionRequest
{
    std::uint32_t minimum_interval_us{};
    std::uint16_t batch_size{};
    std::uint8_t codec{};
    std::uint8_t flags{};

    constexpr bool operator==(const SubscriptionRequest&) const = default;
};

struct SubscriptionPolicy
{
    std::uint32_t minimum_interval_us{};
    std::uint16_t batch_size{1};
    Codec codec{Codec::Cbor};
    std::uint8_t flags{};

    constexpr bool operator==(const SubscriptionPolicy&) const = default;
};

struct CreditGrant
{
    std::uint32_t token{};
    std::uint16_t credits{};
    std::uint16_t window{};

    constexpr bool operator==(const CreditGrant&) const = default;
};

struct InStreamOpenResponse
{
    SubscriptionPolicy policy{};
    std::uint32_t token{};

    constexpr bool operator==(const InStreamOpenResponse&) const = default;
};

struct InStreamCloseRequest
{
    std::uint32_t token{};

    constexpr bool operator==(const InStreamCloseRequest&) const = default;
};

struct InStreamClosed
{
    std::uint32_t token{};
    InStreamCloseReason reason{InStreamCloseReason::Closed};

    constexpr bool operator==(const InStreamClosed&) const = default;
};

struct BatchHeader
{
    std::uint16_t count{};
    Codec codec{Codec::Cbor};
    std::uint8_t version{batch_version};
};

struct IntrospectionSummary
{
    std::uint16_t schemas{};
    std::uint16_t data{};
    std::uint16_t actions{};
    std::uint16_t topics{};
    std::uint16_t streams{};
    std::uint16_t links{};
    std::uint32_t maximum_frame_bytes{};
    std::uint32_t maximum_message_bytes{};

    constexpr bool operator==(const IntrospectionSummary&) const = default;
};

struct CollectionRequest
{
    std::uint16_t offset{};
    std::uint16_t limit{};

    constexpr bool operator==(const CollectionRequest&) const = default;
};

struct CollectionPageHeader
{
    std::uint8_t count{};
    std::uint16_t total{};
    std::uint16_t next{};
    bool has_more{};

    constexpr bool operator==(const CollectionPageHeader&) const = default;
};

struct CollectionQueryRequest
{
    std::uint32_t stable_id{};
    std::uint32_t offset{};
    std::uint32_t revision{};
    std::uint16_t limit{};

    constexpr bool operator==(const CollectionQueryRequest&) const = default;
};

struct ServerInformation
{
    std::uint32_t maximum_frame_bytes{};
    std::uint32_t maximum_message_bytes{};
    std::uint64_t build_id{};
    std::array<std::byte, 32> manifest_digest{};
    std::uint32_t manifest_size{};
    std::uint8_t feature_flags{};

    constexpr bool operator==(const ServerInformation&) const = default;
};

struct ManifestRequest
{
    std::uint32_t offset{};
    std::uint16_t limit{};

    constexpr bool operator==(const ManifestRequest&) const = default;
};

namespace detail
{
constexpr void put_u16(std::span<std::byte> output, std::size_t offset, std::uint16_t value)
{
    output[offset] = static_cast<std::byte>(value & 0xFFU);
    output[offset + 1] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

constexpr void put_u32(std::span<std::byte> output, std::size_t offset, std::uint32_t value)
{
    for (std::size_t byte{}; byte < 4; ++byte) {
        output[offset + byte] = static_cast<std::byte>((value >> (byte * 8U)) & 0xFFU);
    }
}

constexpr void put_u64(std::span<std::byte> output, std::size_t offset, std::uint64_t value)
{
    for (std::size_t byte{}; byte < 8; ++byte) {
        output[offset + byte] = static_cast<std::byte>((value >> (byte * 8U)) & 0xFFU);
    }
}

[[nodiscard]] constexpr std::uint16_t get_u16(std::span<const std::byte> input, std::size_t offset)
{
    return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(input[offset])) |
           static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(input[offset + 1])) << 8U;
}

[[nodiscard]] constexpr std::uint32_t get_u32(std::span<const std::byte> input, std::size_t offset)
{
    std::uint32_t value{};
    for (std::size_t byte{}; byte < 4; ++byte) {
        value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(input[offset + byte]))
                 << (byte * 8U);
    }
    return value;
}

[[nodiscard]] constexpr std::uint64_t get_u64(std::span<const std::byte> input, std::size_t offset)
{
    std::uint64_t value{};
    for (std::size_t byte{}; byte < 8; ++byte) {
        value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(input[offset + byte]))
                 << (byte * 8U);
    }
    return value;
}
} // namespace detail

[[nodiscard]] constexpr std::array<std::byte, server_information_size>
encode(const ServerInformation& information) noexcept
{
    std::array<std::byte, server_information_size> output{};
    output[0] = std::byte{1};
    output[1] = static_cast<std::byte>(major_version);
    output[2] = static_cast<std::byte>(minor_version);
    output[3] = static_cast<std::byte>(information.feature_flags);
    detail::put_u32(output, 4, information.maximum_frame_bytes);
    detail::put_u32(output, 8, information.maximum_message_bytes);
    detail::put_u64(output, 12, information.build_id);
    std::copy(information.manifest_digest.begin(), information.manifest_digest.end(),
              output.begin() + 20);
    detail::put_u32(output, 52, information.manifest_size);
    return output;
}

[[nodiscard]] constexpr Result<ServerInformation, Error>
decode_server_information(std::span<const std::byte> input) noexcept
{
    if (input.size() != server_information_size || input[0] != std::byte{1} ||
        input[1] != static_cast<std::byte>(major_version)) {
        return fail<Error>({Status::ProtocolError, Reason::Malformed, Operation::Decode});
    }
    ServerInformation output{
        .maximum_frame_bytes = detail::get_u32(input, 4),
        .maximum_message_bytes = detail::get_u32(input, 8),
        .build_id = detail::get_u64(input, 12),
        .manifest_size = detail::get_u32(input, 52),
        .feature_flags = std::to_integer<std::uint8_t>(input[3]),
    };
    std::copy(input.begin() + 20, input.begin() + 52, output.manifest_digest.begin());
    return output;
}

[[nodiscard]] constexpr std::array<std::byte, manifest_request_size>
encode(const ManifestRequest& request) noexcept
{
    std::array<std::byte, manifest_request_size> output{};
    detail::put_u32(output, 0, request.offset);
    detail::put_u16(output, 4, request.limit);
    return output;
}

[[nodiscard]] constexpr Result<ManifestRequest, Error>
decode_manifest_request(std::span<const std::byte> input) noexcept
{
    if (input.size() != manifest_request_size || detail::get_u16(input, 6) != 0) {
        return fail<Error>({Status::ProtocolError, Reason::Malformed, Operation::Decode});
    }
    return ManifestRequest{
        .offset = detail::get_u32(input, 0),
        .limit = detail::get_u16(input, 4),
    };
}

[[nodiscard]] constexpr std::array<std::byte, subscription_policy_size>
encode(const SubscriptionRequest& request) noexcept
{
    std::array<std::byte, subscription_policy_size> output{};
    detail::put_u32(output, 0, request.minimum_interval_us);
    detail::put_u16(output, 4, request.batch_size);
    output[6] = static_cast<std::byte>(request.codec);
    output[7] = static_cast<std::byte>(request.flags);
    return output;
}

[[nodiscard]] constexpr std::array<std::byte, subscription_policy_size>
encode(const SubscriptionPolicy& policy) noexcept
{
    std::array<std::byte, subscription_policy_size> output{};
    detail::put_u32(output, 0, policy.minimum_interval_us);
    detail::put_u16(output, 4, policy.batch_size);
    output[6] = static_cast<std::byte>(policy.codec);
    output[7] = static_cast<std::byte>(policy.flags);
    return output;
}

[[nodiscard]] constexpr Result<SubscriptionRequest, Error>
decode_subscription_request(std::span<const std::byte> input) noexcept
{
    if (input.size() != subscription_policy_size) {
        return fail<Error>({Status::ProtocolError, Reason::Malformed, Operation::Decode});
    }
    return SubscriptionRequest{
        .minimum_interval_us = detail::get_u32(input, 0),
        .batch_size = detail::get_u16(input, 4),
        .codec = std::to_integer<std::uint8_t>(input[6]),
        .flags = std::to_integer<std::uint8_t>(input[7]),
    };
}

[[nodiscard]] constexpr Result<SubscriptionPolicy, Error>
decode_subscription_policy(std::span<const std::byte> input) noexcept
{
    if (input.size() != subscription_policy_size) {
        return fail<Error>({Status::ProtocolError, Reason::Malformed, Operation::Decode});
    }
    const auto codec = static_cast<Codec>(std::to_integer<std::uint8_t>(input[6]));
    if (codec != Codec::Cbor && codec != Codec::Packed) {
        return fail<Error>({Status::ProtocolError, Reason::InvalidValue, Operation::Decode});
    }
    return SubscriptionPolicy{
        .minimum_interval_us = detail::get_u32(input, 0),
        .batch_size = detail::get_u16(input, 4),
        .codec = codec,
        .flags = std::to_integer<std::uint8_t>(input[7]),
    };
}

[[nodiscard]] constexpr std::array<std::byte, credit_grant_size>
encode(const CreditGrant& grant) noexcept
{
    std::array<std::byte, credit_grant_size> output{};
    detail::put_u32(output, 0, grant.token);
    detail::put_u16(output, 4, grant.credits);
    detail::put_u16(output, 6, grant.window);
    return output;
}

[[nodiscard]] constexpr Result<CreditGrant, Error>
decode_credit_grant(std::span<const std::byte> input) noexcept
{
    if (input.size() != credit_grant_size) {
        return fail<Error>({Status::ProtocolError, Reason::Malformed, Operation::Decode});
    }
    return CreditGrant{
        .token = detail::get_u32(input, 0),
        .credits = detail::get_u16(input, 4),
        .window = detail::get_u16(input, 6),
    };
}

[[nodiscard]] constexpr std::array<std::byte, in_stream_open_response_size>
encode(const InStreamOpenResponse& response) noexcept
{
    std::array<std::byte, in_stream_open_response_size> output{};
    const auto policy = encode(response.policy);
    std::copy(policy.begin(), policy.end(), output.begin());
    detail::put_u32(output, subscription_policy_size, response.token);
    return output;
}

[[nodiscard]] constexpr Result<InStreamOpenResponse, Error>
decode_in_stream_open_response(std::span<const std::byte> input) noexcept
{
    if (input.size() != in_stream_open_response_size) {
        return fail<Error>({Status::ProtocolError, Reason::Malformed, Operation::Decode});
    }
    auto policy = decode_subscription_policy(input.first(subscription_policy_size));
    const auto token = detail::get_u32(input, subscription_policy_size);
    if (!policy || token == 0) {
        return fail<Error>({Status::ProtocolError, Reason::InvalidValue, Operation::Decode});
    }
    return InStreamOpenResponse{.policy = *policy, .token = token};
}

[[nodiscard]] constexpr std::array<std::byte, in_stream_close_request_size>
encode(const InStreamCloseRequest& request) noexcept
{
    std::array<std::byte, in_stream_close_request_size> output{};
    detail::put_u32(output, 0, request.token);
    return output;
}

[[nodiscard]] constexpr Result<InStreamCloseRequest, Error>
decode_in_stream_close_request(std::span<const std::byte> input) noexcept
{
    if (input.size() != in_stream_close_request_size) {
        return fail<Error>({Status::ProtocolError, Reason::Malformed, Operation::Decode});
    }
    const auto token = detail::get_u32(input, 0);
    if (token == 0) {
        return fail<Error>({Status::ProtocolError, Reason::InvalidValue, Operation::Decode});
    }
    return InStreamCloseRequest{.token = token};
}

[[nodiscard]] constexpr std::array<std::byte, in_stream_closed_size>
encode(const InStreamClosed& closed) noexcept
{
    std::array<std::byte, in_stream_closed_size> output{};
    detail::put_u32(output, 0, closed.token);
    output[4] = static_cast<std::byte>(closed.reason);
    return output;
}

[[nodiscard]] constexpr Result<InStreamClosed, Error>
decode_in_stream_closed(std::span<const std::byte> input) noexcept
{
    if (input.size() != in_stream_closed_size || input[5] != std::byte{} ||
        input[6] != std::byte{} || input[7] != std::byte{}) {
        return fail<Error>({Status::ProtocolError, Reason::Malformed, Operation::Decode});
    }
    const auto token = detail::get_u32(input, 0);
    const auto reason =
        static_cast<InStreamCloseReason>(std::to_integer<std::uint8_t>(input[4]));
    if (token == 0 ||
        static_cast<std::uint8_t>(reason) >
            static_cast<std::uint8_t>(InStreamCloseReason::ConfigurationFailed)) {
        return fail<Error>({Status::ProtocolError, Reason::InvalidValue, Operation::Decode});
    }
    return InStreamClosed{.token = token, .reason = reason};
}

[[nodiscard]] constexpr std::array<std::byte, introspection_summary_size>
encode(const IntrospectionSummary& summary) noexcept
{
    std::array<std::byte, introspection_summary_size> output{};
    output[0] = std::byte{1};
    output[1] = static_cast<std::byte>(major_version);
    output[2] = static_cast<std::byte>(minor_version);
    output[3] = std::byte{1};
    detail::put_u16(output, 4, summary.schemas);
    detail::put_u16(output, 6, summary.data);
    detail::put_u16(output, 8, summary.actions);
    detail::put_u16(output, 10, summary.topics);
    detail::put_u16(output, 12, summary.streams);
    detail::put_u16(output, 14, summary.links);
    detail::put_u32(output, 16, summary.maximum_frame_bytes);
    detail::put_u32(output, 20, summary.maximum_message_bytes);
    return output;
}

[[nodiscard]] constexpr Result<IntrospectionSummary, Error>
decode_introspection_summary(std::span<const std::byte> input) noexcept
{
    if (input.size() != introspection_summary_size || input[0] != std::byte{1} ||
        input[1] != static_cast<std::byte>(major_version)) {
        return fail<Error>({Status::ProtocolError, Reason::Malformed, Operation::Decode});
    }
    return IntrospectionSummary{
        .schemas = detail::get_u16(input, 4),
        .data = detail::get_u16(input, 6),
        .actions = detail::get_u16(input, 8),
        .topics = detail::get_u16(input, 10),
        .streams = detail::get_u16(input, 12),
        .links = detail::get_u16(input, 14),
        .maximum_frame_bytes = detail::get_u32(input, 16),
        .maximum_message_bytes = detail::get_u32(input, 20),
    };
}

[[nodiscard]] constexpr std::array<std::byte, collection_request_size>
encode(const CollectionRequest& request) noexcept
{
    std::array<std::byte, collection_request_size> output{};
    detail::put_u16(output, 0, request.offset);
    detail::put_u16(output, 2, request.limit);
    return output;
}

[[nodiscard]] constexpr Result<CollectionRequest, Error>
decode_collection_request(std::span<const std::byte> input) noexcept
{
    if (input.size() != collection_request_size) {
        return fail<Error>({Status::ProtocolError, Reason::Malformed, Operation::Decode});
    }
    return CollectionRequest{.offset = detail::get_u16(input, 0),
                             .limit = detail::get_u16(input, 2)};
}

[[nodiscard]] constexpr Result<CollectionPageHeader, Error>
decode_collection_page_header(std::span<const std::byte> input) noexcept
{
    if (input.size() < collection_page_header_size || input[0] != std::byte{1}) {
        return fail<Error>({Status::ProtocolError, Reason::Malformed, Operation::Decode});
    }
    return CollectionPageHeader{
        .count = std::to_integer<std::uint8_t>(input[1]),
        .total = detail::get_u16(input, 2),
        .next = detail::get_u16(input, 4),
        .has_more = input[6] != std::byte{},
    };
}

[[nodiscard]] constexpr std::array<std::byte, collection_query_request_size>
encode(const CollectionQueryRequest& request) noexcept
{
    std::array<std::byte, collection_query_request_size> output{};
    detail::put_u32(output, 0, request.stable_id);
    detail::put_u32(output, 4, request.offset);
    detail::put_u32(output, 8, request.revision);
    detail::put_u16(output, 12, request.limit);
    return output;
}

[[nodiscard]] constexpr Result<CollectionQueryRequest, Error>
decode_collection_query_request(std::span<const std::byte> input) noexcept
{
    if (input.size() != collection_query_request_size) {
        return fail<Error>({Status::ProtocolError, Reason::Malformed, Operation::Decode});
    }
    return CollectionQueryRequest{
        .stable_id = detail::get_u32(input, 0),
        .offset = detail::get_u32(input, 4),
        .revision = detail::get_u32(input, 8),
        .limit = detail::get_u16(input, 12),
    };
}

[[nodiscard]] constexpr std::array<std::byte, batch_header_size>
encode(const BatchHeader& header) noexcept
{
    std::array<std::byte, batch_header_size> output{};
    detail::put_u16(output, 0, header.count);
    output[2] = static_cast<std::byte>(header.codec);
    output[3] = static_cast<std::byte>(header.version);
    return output;
}

[[nodiscard]] constexpr Result<BatchHeader, Error>
decode_batch_header(std::span<const std::byte> input) noexcept
{
    if (input.size() < batch_header_size) {
        return fail<Error>({Status::ProtocolError, Reason::Malformed, Operation::Decode});
    }
    const auto codec = static_cast<Codec>(std::to_integer<std::uint8_t>(input[2]));
    const auto version = std::to_integer<std::uint8_t>(input[3]);
    if ((codec != Codec::Cbor && codec != Codec::Packed) || version != batch_version) {
        return fail<Error>({Status::ProtocolError, Reason::UnsupportedVersion, Operation::Decode});
    }
    return BatchHeader{.count = detail::get_u16(input, 0), .codec = codec, .version = version};
}

[[nodiscard]] constexpr Result<std::array<std::byte, envelope_size>, Error>
encode(const Envelope& envelope) noexcept
{
    if (envelope.major != major_version || !valid(envelope.kind) ||
        (static_cast<std::uint8_t>(envelope.flags) & 0xF0U) != 0 || envelope.fragment_count == 0 ||
        envelope.fragment_index >= envelope.fragment_count) {
        return fail<Error>({Status::Invalid, Reason::InvalidValue, Operation::FrameEncode});
    }

    std::array<std::byte, envelope_size> output{};
    output[0] = static_cast<std::byte>(envelope.major);
    output[1] = static_cast<std::byte>(envelope.minor);
    output[2] = static_cast<std::byte>(envelope.kind);
    output[3] = static_cast<std::byte>(envelope.flags);
    detail::put_u16(output, 4, static_cast<std::uint16_t>(envelope_size));
    detail::put_u32(output, 6, envelope.session_epoch);
    detail::put_u32(output, 10, envelope.frame_sequence);
    detail::put_u32(output, 14, envelope.target);
    detail::put_u32(output, 18, envelope.request_id);
    detail::put_u16(output, 22, envelope.payload_size);
    detail::put_u16(output, 24, envelope.fragment_id);
    output[26] = static_cast<std::byte>(envelope.fragment_index);
    output[27] = static_cast<std::byte>(envelope.fragment_count);
    detail::put_u32(output, 28, envelope.reserved);
    return output;
}

[[nodiscard]] constexpr Result<Envelope, Error> decode(std::span<const std::byte> input) noexcept
{
    if (input.size() < envelope_size) {
        return fail<Error>({Status::ProtocolError, Reason::Malformed, Operation::FrameDecode});
    }
    if (std::to_integer<std::uint8_t>(input[0]) != major_version ||
        detail::get_u16(input, 4) != envelope_size) {
        return fail<Error>(
            Error{Status::NotSupported, Reason::UnsupportedVersion, Operation::FrameDecode});
    }

    Envelope envelope{
        .major = std::to_integer<std::uint8_t>(input[0]),
        .minor = std::to_integer<std::uint8_t>(input[1]),
        .kind = static_cast<Kind>(std::to_integer<std::uint8_t>(input[2])),
        .flags = static_cast<Flags>(std::to_integer<std::uint8_t>(input[3])),
        .session_epoch = detail::get_u32(input, 6),
        .frame_sequence = detail::get_u32(input, 10),
        .target = detail::get_u32(input, 14),
        .request_id = detail::get_u32(input, 18),
        .payload_size = detail::get_u16(input, 22),
        .fragment_id = detail::get_u16(input, 24),
        .fragment_index = std::to_integer<std::uint8_t>(input[26]),
        .fragment_count = std::to_integer<std::uint8_t>(input[27]),
        .reserved = detail::get_u32(input, 28),
    };
    if (!valid(envelope.kind)) {
        return fail<Error>({Status::NotSupported, Reason::UnsupportedKind, Operation::FrameDecode});
    }
    if ((static_cast<std::uint8_t>(envelope.flags) & 0xF0U) != 0 || envelope.fragment_count == 0 ||
        envelope.fragment_index >= envelope.fragment_count) {
        return fail<Error>({Status::ProtocolError, Reason::Malformed, Operation::FrameDecode});
    }
    return envelope;
}

} // namespace solar::remote::protocol
