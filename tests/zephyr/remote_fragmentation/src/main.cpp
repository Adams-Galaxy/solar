#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <solar/remote.hpp>
#include <solar/remote/testing/in_memory_link.hpp>
#include <solar/solar.hpp>

namespace fixture
{

struct Blob
{
    solar::remote::BoundedBytes<700> bytes{};
};

struct Bulk
{
    static constexpr solar::remote::DataDescriptor descriptor{
        .id = solar::remote::DataId{0xD101},
        .name = "fixture.bulk",
    };
    using Value = Blob;
    using Capabilities = solar::remote::Capabilities<solar::remote::OutStream<
        solar::remote::Push, solar::remote::Latest, solar::remote::MaxRate<100>>>;
};

struct TestLink : solar::remote::testing::InMemoryLink<TestLink, 512, 512>
{
    static constexpr solar::remote::LinkDescriptor descriptor{
        .id = solar::remote::LinkId{0xD001},
        .name = "fixture.fragment.memory",
    };
    using Grants = solar::remote::Requires<solar::remote::permission::Observe>;
};

struct Root
{
    static constexpr solar::component::Descriptor descriptor{.name = "fixture.fragment.root"};
    using RemoteData = solar::remote::ContributeData<Bulk>;
    using RemoteLinks = solar::remote::ContributeLinks<TestLink>;
};

using System = solar::System<solar::Blueprint<solar::Facilities<Root>>>;
using Service = typename System::RemoteService;

} // namespace fixture

template <> struct solar::remote::Schema<fixture::Blob>
{
    static constexpr SchemaDescriptor descriptor{
        .id = TypeId{0xD201},
        .name = "fixture.Blob",
    };
    using Fields = remote::Fields<Field<1, &fixture::Blob::bytes>>;
    static constexpr std::size_t max_encoded_size = 720;
    static constexpr Codec codec = Codec::Cbor;
};

SOLAR_BIND_SYSTEM(fixture::System);

namespace
{

std::array<std::byte, 512> host_bytes{};
std::array<std::byte, 256> decoded_bytes{};

solar::Result<solar::remote::frame::Decoded, solar::remote::Error> receive_frame(int attempts = 300)
{
    for (int attempt = 0; attempt < attempts; ++attempt) {
        auto bytes = fixture::TestLink::take_transmitted(host_bytes);
        if (bytes) {
            return solar::remote::frame::decode(std::span{host_bytes}.first(*bytes), decoded_bytes);
        }
        k_sleep(K_MSEC(1));
    }
    return solar::fail<solar::remote::Error>({.status = solar::Status::Timeout});
}

void inject(const solar::remote::protocol::Envelope& envelope,
            std::span<const std::byte> payload = {})
{
    std::array<std::byte, 256> scratch{};
    std::array<std::byte, 512> encoded{};
    auto size = solar::remote::frame::encode(envelope, payload, scratch, encoded);
    zassert_true(size.has_value());
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (fixture::TestLink::inject(std::span{encoded}.first(*size))) {
            return;
        }
        k_sleep(K_MSEC(1));
    }
    zassert_unreachable("host frame was not admitted");
}

void establish_subscription()
{
    zassert_true(fixture::TestLink::connect().has_value());
    zassert_true(receive_frame().has_value());
    solar::remote::protocol::Envelope hello{
        .kind = solar::remote::protocol::Kind::ClientHello,
        .frame_sequence = 1,
        .fragment_count = 1,
    };
    constexpr auto hello_payload = fixture::Service::hello_payload();
    inject(hello, hello_payload);
    zassert_true(receive_frame().has_value());

    solar::remote::protocol::Envelope subscribe{
        .kind = solar::remote::protocol::Kind::Subscribe,
        .session_epoch = 1,
        .frame_sequence = 2,
        .target = fixture::Bulk::descriptor.id.value,
        .request_id = 1,
        .fragment_count = 1,
    };
    inject(subscribe);
    zassert_true(receive_frame().has_value());
    subscribe.kind = solar::remote::protocol::Kind::ResponseAck;
    subscribe.frame_sequence = 3;
    inject(subscribe);
}

} // namespace

ZTEST(remote_fragmentation, test_large_publication_is_bounded_and_reassemblable)
{
    zassert_true(fixture::System::boot().has_value());
    establish_subscription();

    fixture::Blob authored{};
    authored.bytes.size = authored.bytes.storage.size();
    for (std::size_t index{}; index < authored.bytes.size; ++index) {
        authored.bytes.storage[index] = static_cast<std::byte>(index & 0xFFU);
    }
    auto written = solar::remote::write<fixture::Bulk>(authored);
    zassert_true(written.has_value());
    zassert_true(written->wake_queued);

    std::array<std::byte, 800> logical{};
    std::size_t logical_size{};
    std::uint16_t fragment_id{};
    std::uint8_t fragment_count{};
    for (std::uint8_t expected{};; ++expected) {
        auto fragment = receive_frame();
        zassert_true(fragment.has_value());
        zassert_equal(fragment->envelope.kind, solar::remote::protocol::Kind::Data);
        zassert_true((static_cast<std::uint8_t>(fragment->envelope.flags) &
                      static_cast<std::uint8_t>(solar::remote::protocol::Flags::Fragmented)) != 0);
        if (expected == 0) {
            fragment_id = fragment->envelope.fragment_id;
            fragment_count = fragment->envelope.fragment_count;
            zassert_true(fragment_id != 0);
            zassert_true(fragment_count > 1);
        }
        zassert_equal(fragment->envelope.fragment_id, fragment_id);
        zassert_equal(fragment->envelope.fragment_index, expected);
        zassert_equal(fragment->envelope.fragment_count, fragment_count);
        std::copy(fragment->payload.begin(), fragment->payload.end(),
                  logical.begin() + logical_size);
        logical_size += fragment->payload.size();
        const bool final = (static_cast<std::uint8_t>(fragment->envelope.flags) &
                            static_cast<std::uint8_t>(solar::remote::protocol::Flags::Final)) != 0;
        zassert_equal(final, expected + 1U == fragment_count);
        if (final) {
            break;
        }
    }

    auto decoded =
        solar::remote::cbor::decode<fixture::Blob>(std::span{logical}.first(logical_size));
    zassert_true(decoded.has_value());
    zassert_equal(decoded->bytes.size, authored.bytes.size);
    zassert_mem_equal(decoded->bytes.storage.data(), authored.bytes.storage.data(),
                      authored.bytes.size);

    zassert_true(fixture::System::stop().has_value());
}

ZTEST_SUITE(remote_fragmentation, nullptr, nullptr, nullptr, nullptr, nullptr);
