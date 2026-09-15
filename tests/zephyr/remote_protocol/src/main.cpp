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

struct Point
{
    std::uint16_t angle_centidegrees{};
    std::uint16_t distance_mm{};
    std::uint8_t confidence{};
};

struct Scan
{
    std::uint64_t generation{};
    solar::remote::Array<Point, 4> points{};
    solar::remote::Array<std::uint16_t, 4> checksums{};
};

/// Same wire shape as Scan, but with a checksums capacity wide enough to
/// encode more elements than Scan's receiver can hold -- used to verify
/// decode rejects an over-capacity array instead of truncating or
/// overrunning it.
struct WideScan
{
    std::uint64_t generation{};
    solar::remote::Array<Point, 4> points{};
    solar::remote::Array<std::uint16_t, 6> checksums{};
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

template <> struct solar::remote::Schema<fixture::Point>
{
    static constexpr SchemaDescriptor descriptor{
        .id = TypeId{0x3002},
        .name = "fixture.Point",
    };
    static constexpr SchemaShape shape = SchemaShape::Record;
    using Fields = remote::Fields<Field<1, "angle_centidegrees", &fixture::Point::angle_centidegrees>,
                                  Field<2, "distance_mm", &fixture::Point::distance_mm>,
                                  Field<3, "confidence", &fixture::Point::confidence>>;
};

template <> struct solar::remote::Schema<fixture::Scan>
{
    static constexpr SchemaDescriptor descriptor{
        .id = TypeId{0x3003},
        .name = "fixture.Scan",
    };
    using Fields = remote::Fields<Field<1, "generation", &fixture::Scan::generation>,
                                  Field<2, "points", &fixture::Scan::points>,
                                  Field<3, "checksums", &fixture::Scan::checksums>>;
    static constexpr std::size_t max_encoded_size = 64;
    static constexpr Codec codec = Codec::Cbor;
};

template <> struct solar::remote::Schema<fixture::WideScan>
{
    static constexpr SchemaDescriptor descriptor{
        .id = TypeId{0x3004},
        .name = "fixture.WideScan",
    };
    using Fields = remote::Fields<Field<1, "generation", &fixture::WideScan::generation>,
                                  Field<2, "points", &fixture::WideScan::points>,
                                  Field<3, "checksums", &fixture::WideScan::checksums>>;
    static constexpr std::size_t max_encoded_size = 64;
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

ZTEST(solar_remote_protocol, test_array_and_record_round_trip)
{
    fixture::Scan scan{};
    scan.generation = 7;
    scan.points.size = 2;
    scan.points.storage[0] = {.angle_centidegrees = 100, .distance_mm = 2000, .confidence = 200};
    scan.points.storage[1] = {.angle_centidegrees = 200, .distance_mm = 3000, .confidence = 150};
    scan.checksums.size = 3;
    scan.checksums.storage[0] = 11;
    scan.checksums.storage[1] = 22;
    scan.checksums.storage[2] = 33;

    std::array<std::byte, 64> encoded{};
    auto size = solar::remote::cbor::encode(scan, encoded);
    zassert_true(size.has_value());

    auto decoded = solar::remote::cbor::decode<fixture::Scan>(std::span{encoded}.first(*size));
    zassert_true(decoded.has_value());
    zassert_equal(decoded->generation, scan.generation);
    zassert_equal(decoded->points.size, scan.points.size);
    zassert_equal(decoded->points.storage[0].angle_centidegrees,
                  scan.points.storage[0].angle_centidegrees);
    zassert_equal(decoded->points.storage[1].distance_mm, scan.points.storage[1].distance_mm);
    zassert_equal(decoded->checksums.size, scan.checksums.size);
    zassert_equal(decoded->checksums.storage[2], scan.checksums.storage[2]);

    // A wire array with more elements than the receiving Capacity must be
    // rejected cleanly, not truncated or overrun. Scan's checksums field
    // holds at most 4; encode 5 via WideScan (identical wire shape, wider
    // capacity) and decode that payload as a Scan.
    fixture::WideScan wide{};
    wide.checksums.size = 5;
    for (std::size_t index{}; index < wide.checksums.size; ++index) {
        wide.checksums.storage[index] = static_cast<std::uint16_t>(index + 1);
    }
    std::array<std::byte, 64> wide_encoded{};
    auto wide_size = solar::remote::cbor::encode(wide, wide_encoded);
    zassert_true(wide_size.has_value());
    auto rejected =
        solar::remote::cbor::decode<fixture::Scan>(std::span{wide_encoded}.first(*wide_size));
    zassert_false(rejected.has_value());
}

ZTEST(solar_remote_protocol, test_shared_frame_vector_and_corruption)
{
    solar::remote::protocol::Envelope envelope{
        .minor = 1,
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
        std::byte{0x06}, std::byte{0x01}, std::byte{0x01}, std::byte{0x04}, std::byte{0x02},
        std::byte{0x20}, std::byte{0x12}, std::byte{0x04}, std::byte{0x03}, std::byte{0x02},
        std::byte{0x01}, std::byte{0x14}, std::byte{0x13}, std::byte{0x12}, std::byte{0x11},
        std::byte{0x24}, std::byte{0x23}, std::byte{0x22}, std::byte{0x21}, std::byte{0x34},
        std::byte{0x33}, std::byte{0x32}, std::byte{0x31}, std::byte{0x03}, std::byte{0x03},
        std::byte{0x42}, std::byte{0x41}, std::byte{0x02}, std::byte{0x01}, std::byte{0x01},
        std::byte{0x01}, std::byte{0x01}, std::byte{0x02}, std::byte{0xA1}, std::byte{0x06},
        std::byte{0x7F}, std::byte{0xBE}, std::byte{0x06}, std::byte{0x08}, std::byte{0xFB},
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
