#include <array>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>

#include <zephyr/ztest.h>

#include <solar/remote.hpp>

namespace fixture
{
struct Sample
{
    std::uint32_t sequence{};
    std::int16_t value{};
    float gain{};
    bool valid{};
    constexpr bool operator==(const Sample&) const = default;
};
} // namespace fixture

template <> struct solar::remote::Schema<fixture::Sample>
{
    static constexpr SchemaDescriptor descriptor{
        .id = TypeId{0x3001},
        .name = "fixture.Sample",
    };
    using Fields = remote::Fields<Field<1, "sequence", &fixture::Sample::sequence>,
                                  Field<2, "value", &fixture::Sample::value>,
                                  Field<3, "gain", &fixture::Sample::gain>,
                                  Field<4, "valid", &fixture::Sample::valid>>;
    static constexpr std::size_t max_encoded_size = 32;
    static constexpr Codec codec = Codec::Cbor;
};

ZTEST(solar_remote_protocol, test_canonical_cbor_round_trip_and_validation)
{
    const fixture::Sample sample{.sequence = 42, .value = -3, .gain = 1.5F, .valid = true};
    std::array<std::byte, 32> encoded{};
    auto size = solar::remote::cbor::encode(sample, encoded);
    zassert_true(size.has_value());
    constexpr std::array expected{
        std::byte{0xA4}, std::byte{0x01}, std::byte{0x18}, std::byte{0x2A},
        std::byte{0x02}, std::byte{0x22}, std::byte{0x03}, std::byte{0xF9},
        std::byte{0x3E}, std::byte{0x00}, std::byte{0x04}, std::byte{0xF5},
    };
    zassert_equal(*size, expected.size());
    zassert_true(std::ranges::equal(std::span{encoded}.first(*size), expected));
    auto decoded = solar::remote::cbor::decode<fixture::Sample>(expected);
    zassert_true(decoded.has_value());
    zassert_equal(decoded->sequence, sample.sequence);
    zassert_equal(decoded->value, sample.value);
    zassert_equal(decoded->gain, sample.gain);
    zassert_equal(decoded->valid, sample.valid);

    std::array<std::byte, 2> status_bytes{};
    auto status_size = solar::remote::cbor::encode(solar::Status::Timeout, status_bytes);
    zassert_true(status_size.has_value());
    zassert_equal(*status_size, 1);
    auto status = solar::remote::cbor::decode<solar::Status>(std::span{status_bytes}.first(1));
    zassert_true(status.has_value());
    zassert_equal(*status, solar::Status::Timeout);

    constexpr std::array with_unknown{
        std::byte{0xA5}, std::byte{0x01}, std::byte{0x18}, std::byte{0x2A}, std::byte{0x02},
        std::byte{0x22}, std::byte{0x03}, std::byte{0xF9}, std::byte{0x3E}, std::byte{0x00},
        std::byte{0x04}, std::byte{0xF5}, std::byte{0x05}, std::byte{0x01},
    };
    zassert_true(solar::remote::cbor::decode<fixture::Sample>(with_unknown).has_value());

    constexpr std::array duplicate{std::byte{0xA2}, std::byte{0x01}, std::byte{0x01},
                                   std::byte{0x01}, std::byte{0x02}};
    auto duplicate_result = solar::remote::cbor::decode<fixture::Sample>(duplicate);
    zassert_false(duplicate_result.has_value());
    zassert_equal(duplicate_result.error().reason, solar::remote::Reason::DuplicateField);

    constexpr std::array missing{std::byte{0xA1}, std::byte{0x01}, std::byte{0x01}};
    auto missing_result = solar::remote::cbor::decode<fixture::Sample>(missing);
    zassert_false(missing_result.has_value());
    zassert_equal(missing_result.error().reason, solar::remote::Reason::MissingField);

    std::array<std::byte, expected.size() + 1> trailing{};
    std::ranges::copy(expected, trailing.begin());
    trailing.back() = std::byte{0x00};
    auto trailing_result = solar::remote::cbor::decode<fixture::Sample>(trailing);
    zassert_false(trailing_result.has_value());
    zassert_equal(trailing_result.error().reason, solar::remote::Reason::TrailingData);
}

ZTEST(solar_remote_protocol, test_shared_frame_vector_and_corruption)
{
    solar::remote::protocol::Envelope envelope{
        .kind = solar::remote::protocol::Kind::Request,
        .flags = solar::remote::protocol::Flags::Final,
        .session_epoch = 0x01020304,
        .frame_sequence = 0x11121314,
        .target = 0x21222324,
        .request_id = 0x31323334,
        .fragment_id = 0x4142,
    };
    constexpr std::array payload{std::byte{0xA1}, std::byte{0x00}, std::byte{0x7F}};
    constexpr std::array expected{
        std::byte{0x02}, std::byte{0x01}, std::byte{0x04}, std::byte{0x04}, std::byte{0x02},
        std::byte{0x20}, std::byte{0x12}, std::byte{0x04}, std::byte{0x03}, std::byte{0x02},
        std::byte{0x01}, std::byte{0x14}, std::byte{0x13}, std::byte{0x12}, std::byte{0x11},
        std::byte{0x24}, std::byte{0x23}, std::byte{0x22}, std::byte{0x21}, std::byte{0x34},
        std::byte{0x33}, std::byte{0x32}, std::byte{0x31}, std::byte{0x03}, std::byte{0x03},
        std::byte{0x42}, std::byte{0x41}, std::byte{0x02}, std::byte{0x01}, std::byte{0x01},
        std::byte{0x01}, std::byte{0x01}, std::byte{0x02}, std::byte{0xA1}, std::byte{0x06},
        std::byte{0x7F}, std::byte{0x6F}, std::byte{0x8D}, std::byte{0xFC}, std::byte{0x85},
        std::byte{0x00},
    };
    std::array<std::byte, 64> scratch{};
    std::array<std::byte, 80> output{};
    auto size = solar::remote::frame::encode(envelope, payload, scratch, output);
    zassert_true(size.has_value());
    zassert_equal(*size, expected.size(), "encoded frame size %zu, expected %zu", *size,
                  expected.size());
    zassert_true(std::ranges::equal(std::span{output}.first(*size), expected));
    std::array<std::byte, 64> decoded{};
    auto result = solar::remote::frame::decode(std::span{output}.first(*size), decoded);
    zassert_true(result.has_value());
    zassert_true(std::ranges::equal(result->payload, payload));

    solar::remote::frame::StreamDecoder<80, 64> parser;
    std::size_t delivered{};
    auto handler = [&](const solar::remote::frame::Decoded& complete) {
        ++delivered;
        zassert_true(std::ranges::equal(complete.payload, payload));
    };
    zassert_equal(parser.feed(std::span{output}.first(9), handler).accepted, 0);
    zassert_equal(parser.feed(std::span{output}.subspan(9, *size - 9), handler).accepted, 1);
    zassert_equal(delivered, 1);

    output[8] ^= std::byte{0x10};
    auto corrupt = solar::remote::frame::decode(std::span{output}.first(*size), decoded);
    zassert_false(corrupt.has_value());
}

ZTEST_SUITE(solar_remote_protocol, nullptr, nullptr, nullptr, nullptr, nullptr);
