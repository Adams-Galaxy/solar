#define CONFIG_SOLAR_REMOTE 1
#define CONFIG_SOLAR_REMOTE_MAX_SCHEMAS 32
#define CONFIG_SOLAR_REMOTE_MAX_ENDPOINTS 32

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include <solar/remote.hpp>
#include <solar/system.hpp>

namespace fixture
{

enum class Mode : std::int8_t
{
    Idle = -1,
    Active = 2,
};

enum class OpenMode : std::uint8_t
{
    Known = 1,
};

struct ModeValue
{
    Mode mode{};
};

struct OpenModeValue
{
    OpenMode mode{};
};

struct Sample
{
    std::uint32_t sequence{};
    std::int16_t value{};
    float gain{};
    bool valid{};
    std::optional<std::uint16_t> quality{};
    constexpr bool operator==(const Sample&) const = default;
};

struct PackedSample
{
    std::uint16_t sequence{};
    float value{};
    constexpr bool operator==(const PackedSample&) const = default;
};

struct Telemetry
{
    static constexpr solar::remote::DataDescriptor descriptor{
        .id = solar::remote::DataId{0x2001},
        .name = "fixture.telemetry",
        .description = "Host protocol fixture",
    };
    using Value = Sample;
    using Capabilities = solar::remote::Capabilities<solar::remote::Watch<>>;
};

struct Reset
{
    static constexpr solar::remote::ActionDescriptor descriptor{
        .id = solar::remote::ActionId{0x1001},
        .name = "fixture.reset",
    };
    using Access = solar::remote::Requires<solar::remote::permission::Control>;
};

struct Component
{
    static constexpr solar::component::Descriptor descriptor{.name = "fixture.component"};
    using RemoteData = solar::remote::ContributeData<Telemetry>;
    using RemoteActions = solar::remote::ContributeActions<Reset>;
    using RemoteSchemas = solar::remote::ContributeSchemas<Mode, OpenMode>;
};

using System = solar::System<solar::Blueprint<solar::Facilities<Component>>>;

} // namespace fixture

template <> struct solar::remote::Schema<fixture::Mode>
{
    static constexpr SchemaDescriptor descriptor{
        .id = TypeId{0x3000},
        .name = "fixture.Mode",
        .description = "Fixture operating mode",
    };
    static constexpr SchemaShape shape = SchemaShape::Enumeration;
    static constexpr EnumOpenness openness = EnumOpenness::Closed;
    using Values = remote::EnumValues<remote::EnumValue<fixture::Mode::Idle, "idle">,
                                      remote::EnumValue<fixture::Mode::Active, "active",
                                                        remote::Description<"Actively sampling">>>;
};

template <> struct solar::remote::Schema<fixture::OpenMode>
{
    static constexpr SchemaDescriptor descriptor{
        .id = TypeId{0x3003},
        .name = "fixture.OpenMode",
    };
    static constexpr SchemaShape shape = SchemaShape::Enumeration;
    static constexpr EnumOpenness openness = EnumOpenness::Open;
    using Values = remote::EnumValues<remote::EnumValue<fixture::OpenMode::Known, "known">>;
};

template <> struct solar::remote::Schema<fixture::ModeValue>
{
    static constexpr SchemaDescriptor descriptor{.id = TypeId{0x3004}, .name = "fixture.ModeValue"};
    using Fields = remote::Fields<Field<1, "mode", &fixture::ModeValue::mode>>;
    static constexpr std::size_t max_encoded_size = 1;
    static constexpr Codec codec = Codec::Packed;
};

template <> struct solar::remote::Schema<fixture::OpenModeValue>
{
    static constexpr SchemaDescriptor descriptor{.id = TypeId{0x3005},
                                                 .name = "fixture.OpenModeValue"};
    using Fields = remote::Fields<Field<1, "mode", &fixture::OpenModeValue::mode>>;
    static constexpr std::size_t max_encoded_size = 1;
    static constexpr Codec codec = Codec::Packed;
};

template <> struct solar::remote::Schema<fixture::Sample>
{
    static constexpr SchemaDescriptor descriptor{
        .id = TypeId{0x3001},
        .name = "fixture.Sample",
    };
    using Fields = remote::Fields<
        Field<1, "sequence", &fixture::Sample::sequence>,
        Field<2, "value", &fixture::Sample::value, remote::Description<"Measured value">>,
        Field<3, "gain", &fixture::Sample::gain, remote::Unit<"ratio">>,
        Field<4, "valid", &fixture::Sample::valid, remote::Deprecated<"use status">>,
        Field<5, "quality", &fixture::Sample::quality>>;
    static constexpr std::size_t max_encoded_size = 32;
    static constexpr Codec codec = Codec::Cbor;
};

template <> struct solar::remote::Schema<fixture::PackedSample>
{
    static constexpr SchemaDescriptor descriptor{
        .id = TypeId{0x3002},
        .name = "fixture.PackedSample",
    };
    using Fields = remote::Fields<Field<1, "sequence", &fixture::PackedSample::sequence>,
                                  Field<2, "value", &fixture::PackedSample::value>>;
    static constexpr std::size_t max_encoded_size = 6;
    static constexpr Codec codec = Codec::Packed;
};

SOLAR_REMOTE_EMIT_MANIFEST(fixture::System);

static_assert(solar::remote::validate_schema<fixture::Sample>());
static_assert(solar::remote::validate_enum_schema<fixture::Mode>());
static_assert(solar::remote::detail::enum_openness<fixture::OpenMode>() ==
              solar::remote::EnumOpenness::Open);
static_assert(solar::remote::packed::encoded_size<fixture::PackedSample> == 6);
static_assert(fixture::System::RemoteDataCatalog::contains<fixture::Telemetry>);
static_assert(fixture::System::RemoteActionCatalog::contains<fixture::Reset>);
static constexpr std::array sha_input{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
static_assert(solar::remote::detail::sha256(std::span<const std::byte>{sha_input}) ==
              std::array<std::byte, 32>{
                  std::byte{0xba}, std::byte{0x78}, std::byte{0x16}, std::byte{0xbf},
                  std::byte{0x8f}, std::byte{0x01}, std::byte{0xcf}, std::byte{0xea},
                  std::byte{0x41}, std::byte{0x41}, std::byte{0x40}, std::byte{0xde},
                  std::byte{0x5d}, std::byte{0xae}, std::byte{0x22}, std::byte{0x23},
                  std::byte{0xb0}, std::byte{0x03}, std::byte{0x61}, std::byte{0xa3},
                  std::byte{0x96}, std::byte{0x17}, std::byte{0x7a}, std::byte{0x9c},
                  std::byte{0xb4}, std::byte{0x10}, std::byte{0xff}, std::byte{0x61},
                  std::byte{0xf2}, std::byte{0x00}, std::byte{0x15}, std::byte{0xad},
              });

int main()
{
    using namespace solar::remote;
    protocol::Envelope envelope{
        .minor = 1,
        .kind = protocol::Kind::Request,
        .flags = protocol::Flags::Final,
        .session_epoch = 0x01020304,
        .frame_sequence = 0x11121314,
        .target = 0x21222324,
        .request_id = 0x31323334,
        .payload_size = 3,
        .fragment_id = 0x4142,
        .fragment_index = 0,
        .fragment_count = 1,
    };
    const auto bytes = protocol::encode(envelope);
    assert(bytes);
    constexpr std::array expected{
        std::byte{0x01}, std::byte{0x01}, std::byte{0x04}, std::byte{0x02}, std::byte{0x20},
        std::byte{0x00}, std::byte{0x04}, std::byte{0x03}, std::byte{0x02}, std::byte{0x01},
        std::byte{0x14}, std::byte{0x13}, std::byte{0x12}, std::byte{0x11}, std::byte{0x24},
        std::byte{0x23}, std::byte{0x22}, std::byte{0x21}, std::byte{0x34}, std::byte{0x33},
        std::byte{0x32}, std::byte{0x31}, std::byte{0x03}, std::byte{0x00}, std::byte{0x42},
        std::byte{0x41}, std::byte{0x00}, std::byte{0x01}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00},
    };
    assert(*bytes == expected);
    const auto decoded_envelope = protocol::decode(*bytes);
    assert(decoded_envelope && *decoded_envelope == envelope);
    protocol::ServerInformation information{
        .maximum_frame_bytes = 1024,
        .maximum_message_bytes = 4096,
        .build_id = 0x12345678,
        .manifest_digest = solar::remote::manifest::Image<fixture::System>::digest,
        .manifest_size =
            static_cast<std::uint32_t>(solar::remote::manifest::Image<fixture::System>::byte_count),
        .feature_flags = 1,
    };
    const auto information_bytes = protocol::encode(information);
    const auto decoded_information = protocol::decode_server_information(information_bytes);
    assert(decoded_information && *decoded_information == information);
    constexpr protocol::ManifestRequest manifest_request{.offset = 12, .limit = 64};
    assert(protocol::decode_manifest_request(protocol::encode(manifest_request)) ==
           manifest_request);
    auto query_envelope = envelope;
    query_envelope.set_operation(protocol::OperationKind::Query);
    const auto query_bytes = protocol::encode(query_envelope);
    assert(query_bytes);
    const auto decoded_query = protocol::decode(*query_bytes);
    assert(decoded_query && decoded_query->operation() == protocol::OperationKind::Query);

    constexpr protocol::SubscriptionRequest subscription_request{
        .minimum_interval_us = 20'000,
        .batch_size = 4,
    };
    constexpr auto subscription_request_bytes = protocol::encode(subscription_request);
    static_assert(protocol::decode_subscription_request(subscription_request_bytes) ==
                  subscription_request);
    constexpr protocol::SubscriptionPolicy subscription_policy{
        .minimum_interval_us = 20'000,
        .batch_size = 1,
        .codec = Codec::Cbor,
    };
    constexpr auto subscription_policy_bytes = protocol::encode(subscription_policy);
    static_assert(protocol::decode_subscription_policy(subscription_policy_bytes) ==
                  subscription_policy);
    constexpr protocol::PingRequest ping_request{
        .nonce = 0x0102030405060708ULL,
        .host_monotonic_ns = 0x1112131415161718ULL,
    };
    static_assert(protocol::decode_ping_request(protocol::encode(ping_request)) == ping_request);
    constexpr protocol::PingResponse ping_response{
        .nonce = ping_request.nonce,
        .host_monotonic_ns = ping_request.host_monotonic_ns,
        .remote_receive_us = 100,
        .remote_send_us = 104,
    };
    static_assert(protocol::decode_ping_response(protocol::encode(ping_response)) ==
                  ping_response);
    constexpr protocol::InStreamOpenResponse opened{
        .policy = subscription_policy,
        .token = 0x11223344,
    };
    static_assert(protocol::encode(opened) ==
                  std::array{std::byte{0x20}, std::byte{0x4e}, std::byte{0x00},
                             std::byte{0x00}, std::byte{0x01}, std::byte{0x00},
                             std::byte{0x01}, std::byte{0x00}, std::byte{0x44},
                             std::byte{0x33}, std::byte{0x22}, std::byte{0x11}});
    static_assert(protocol::decode_in_stream_open_response(protocol::encode(opened)) == opened);
    constexpr protocol::CreditGrant credit{
        .token = opened.token,
        .credits = 3,
        .window = 8,
    };
    static_assert(protocol::encode(credit) ==
                  std::array{std::byte{0x44}, std::byte{0x33}, std::byte{0x22},
                             std::byte{0x11}, std::byte{0x03}, std::byte{0x00},
                             std::byte{0x08}, std::byte{0x00}});
    static_assert(protocol::decode_credit_grant(protocol::encode(credit)) == credit);
    constexpr protocol::InStreamCloseRequest close{.token = opened.token};
    static_assert(protocol::decode_in_stream_close_request(protocol::encode(close)) == close);
    constexpr protocol::InStreamClosed replaced{
        .token = opened.token,
        .reason = InStreamCloseReason::Replaced,
    };
    static_assert(protocol::decode_in_stream_closed(protocol::encode(replaced)) == replaced);
    constexpr protocol::CollectionRequest collection_request{.offset = 3, .limit = 7};
    constexpr auto collection_request_bytes = protocol::encode(collection_request);
    static_assert(protocol::decode_collection_request(collection_request_bytes) ==
                  collection_request);
    constexpr protocol::CollectionQueryRequest collection_query{
        .stable_id = 0x6D9A0002U,
        .offset = 4,
        .revision = 9,
        .limit = 2,
    };
    constexpr auto collection_query_bytes = protocol::encode(collection_query);
    static_assert(protocol::decode_collection_query_request(collection_query_bytes) ==
                  collection_query);
    auto unknown_kind = *bytes;
    unknown_kind[2] = std::byte{0x7F};
    const auto rejected_kind = protocol::decode(unknown_kind);
    assert(!rejected_kind && rejected_kind.error().reason == Reason::UnsupportedKind);

    constexpr std::array crc_input{std::byte{'1'}, std::byte{'2'}, std::byte{'3'},
                                   std::byte{'4'}, std::byte{'5'}, std::byte{'6'},
                                   std::byte{'7'}, std::byte{'8'}, std::byte{'9'}};
    static_assert(frame::crc32c(crc_input) == 0xE3069283U);

    constexpr std::array payload{std::byte{0xA1}, std::byte{0x00}, std::byte{0x7F}};
    std::array<std::byte, 64> scratch{};
    std::array<std::byte, 80> framed{};
    auto framed_size = frame::encode(envelope, payload, scratch, framed);
    assert(framed_size);
    std::array<std::byte, 64> decoded{};
    auto complete = frame::decode(std::span{framed}.first(*framed_size), decoded);
    assert(complete && complete->envelope.payload_size == payload.size());
    assert(std::ranges::equal(complete->payload, payload));
    framed[5] ^= std::byte{0x20};
    auto corrupt = frame::decode(std::span{framed}.first(*framed_size), decoded);
    assert(!corrupt);
    framed[5] ^= std::byte{0x20};

    frame::StreamDecoder<80, 64> stream_decoder;
    std::size_t delivered{};
    auto handler = [&](const frame::Decoded& value) {
        ++delivered;
        assert(std::ranges::equal(value.payload, payload));
    };
    auto first_chunk = stream_decoder.feed(std::span{framed}.first(*framed_size / 2), handler);
    assert(first_chunk.accepted == 0);
    auto second_chunk = stream_decoder.feed(
        std::span{framed}.subspan(*framed_size / 2, *framed_size - *framed_size / 2), handler);
    assert(second_chunk.accepted == 1 && delivered == 1);

    std::array<std::byte, 164> resync{};
    std::fill_n(resync.begin(), 81, std::byte{0x55});
    resync[81] = std::byte{};
    std::copy_n(framed.begin(), *framed_size, resync.begin() + 82);
    auto recovered = stream_decoder.feed(std::span{resync}.first(82 + *framed_size), handler);
    assert(recovered.overflowed == 1 && recovered.accepted == 1 && delivered == 2);

    fixture::PackedSample sample{.sequence = 0x1234, .value = 1.5F};
    std::array<std::byte, 6> packed_bytes{};
    auto packed_size = packed::encode(sample, packed_bytes);
    assert(packed_size && *packed_size == packed_bytes.size());
    constexpr std::array packed_expected{std::byte{0x34}, std::byte{0x12}, std::byte{0x00},
                                         std::byte{0x00}, std::byte{0xC0}, std::byte{0x3F}};
    assert(packed_bytes == packed_expected);
    auto unpacked = packed::decode<fixture::PackedSample>(packed_bytes);
    assert(unpacked && *unpacked == sample);
    constexpr std::array closed_unknown{std::byte{0x03}};
    assert(!packed::decode<fixture::ModeValue>(closed_unknown));
    constexpr std::array open_unknown{std::byte{0x07}};
    auto open_value = packed::decode<fixture::OpenModeValue>(open_unknown);
    assert(open_value && static_cast<std::uint8_t>(open_value->mode) == 7);

    constexpr auto& manifest = solar::remote::manifest::Image<fixture::System>::bytes;
    static_assert(manifest.size() > 16);
    assert(manifest[0] == std::byte{'S'} && manifest[3] == std::byte{'M'});
}
