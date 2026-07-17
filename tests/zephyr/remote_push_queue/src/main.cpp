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

struct Sample
{
    std::uint32_t sequence{};
};

struct Queued
{
    static constexpr solar::remote::DataDescriptor descriptor{
        .id = solar::remote::DataId{0x9101},
        .name = "fixture.queued",
    };
    using Value = Sample;
    using Capabilities = solar::remote::Capabilities<solar::remote::OutStream<
        solar::remote::Push, solar::remote::Queue<3, solar::remote::Reject>,
        solar::remote::Batch<3>, solar::remote::MaxRate<1000>>>;
};

struct TestLink : solar::remote::testing::InMemoryLink<TestLink, 256, 256>
{
    static constexpr solar::remote::LinkDescriptor descriptor{
        .id = solar::remote::LinkId{0x9001},
        .name = "fixture.queue.memory",
    };
    using Grants = solar::remote::Requires<solar::remote::permission::Observe>;
};

struct Root
{
    static constexpr solar::component::Descriptor descriptor{.name = "fixture.queue.root"};
    using RemoteData = solar::remote::ContributeData<Queued>;
    using RemoteLinks = solar::remote::ContributeLinks<TestLink>;
};

using System = solar::System<solar::Blueprint<solar::Facilities<Root>>>;
using Service = typename System::RemoteService;
using LinkState = solar::remote::detail::LinkState<Service, TestLink, 0>;

} // namespace fixture

template <> struct solar::remote::Schema<fixture::Sample>
{
    static constexpr SchemaDescriptor descriptor{
        .id = TypeId{0x9201},
        .name = "fixture.QueueSample",
    };
    using Fields = remote::Fields<Field<1, &fixture::Sample::sequence>>;
    static constexpr std::size_t max_encoded_size = 12;
    static constexpr Codec codec = Codec::Cbor;
};

SOLAR_BIND_SYSTEM(fixture::System);

namespace
{

std::array<std::byte, 256> host_bytes{};
std::array<std::byte, 160> decoded_bytes{};

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
    std::array<std::byte, 160> scratch{};
    std::array<std::byte, 256> encoded{};
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

} // namespace

ZTEST(remote_push_queue, test_rejecting_queue_drains_as_one_batch)
{
    zassert_true(fixture::System::boot().has_value());

    auto first = solar::remote::write<fixture::Queued>({.sequence = 1});
    auto second = solar::remote::write<fixture::Queued>({.sequence = 2});
    auto third = solar::remote::write<fixture::Queued>({.sequence = 3});
    auto full = solar::remote::write<fixture::Queued>({.sequence = 4});
    zassert_true(first.has_value());
    zassert_true(second.has_value());
    zassert_true(third.has_value());
    zassert_equal(first->disposition, solar::remote::WriteDisposition::NoSubscribers);
    zassert_equal(second->disposition, solar::remote::WriteDisposition::NoSubscribers);
    zassert_equal(third->disposition, solar::remote::WriteDisposition::NoSubscribers);
    zassert_false(full.has_value());
    zassert_equal(full.error().status, solar::Status::NoSpace);

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
        .target = fixture::Queued::descriptor.id.value,
        .request_id = 1,
        .fragment_count = 1,
    };
    inject(subscribe);
    auto response = receive_frame();
    zassert_true(response.has_value());
    zassert_equal(response->envelope.kind, solar::remote::protocol::Kind::Response);

    solar::remote::protocol::Envelope ack{
        .kind = solar::remote::protocol::Kind::ResponseAck,
        .session_epoch = 1,
        .frame_sequence = 3,
        .target = fixture::Queued::descriptor.id.value,
        .request_id = 1,
        .fragment_count = 1,
    };
    inject(ack);
    constexpr auto endpoint =
        fixture::System::RemoteDataCatalog::Entry<fixture::Queued>::local_id.value;
    zassert_true(fixture::Service::notify_publication(endpoint).has_value());

    auto data = receive_frame();
    zassert_true(data.has_value());
    zassert_equal(data->envelope.kind, solar::remote::protocol::Kind::Data);
    auto header = solar::remote::protocol::decode_batch_header(data->payload);
    zassert_true(header.has_value());
    zassert_equal(header->count, 3);
    zassert_equal(header->codec, solar::remote::Codec::Cbor);
    std::size_t offset = solar::remote::protocol::batch_header_size;
    for (std::uint32_t expected = 1; expected <= 3; ++expected) {
        const auto size = solar::remote::protocol::detail::get_u16(data->payload, offset);
        offset += sizeof(std::uint16_t);
        auto value =
            solar::remote::cbor::decode<fixture::Sample>(data->payload.subspan(offset, size));
        zassert_true(value.has_value());
        zassert_equal(value->sequence, expected);
        offset += size;
    }
    zassert_equal(offset, data->payload.size());

    auto& ingress = solar::remote::detail::push_state<fixture::System, fixture::Queued>();
    for (int attempt = 0; attempt < 100; ++attempt) {
        bool empty{};
        {
            auto guard = ingress.lock.acquire();
            empty = ingress.size == 0;
        }
        if (empty) {
            break;
        }
        k_sleep(K_MSEC(1));
    }
    {
        auto guard = ingress.lock.acquire();
        zassert_equal(ingress.size, 0);
    }
    {
        auto guard = fixture::LinkState::output_lock.acquire();
        zassert_equal(fixture::LinkState::subscriptions[0].delivered, 1);
        zassert_equal(fixture::LinkState::subscriptions[0].skipped, 0);
    }

    zassert_true(fixture::System::stop().has_value());
}

ZTEST_SUITE(remote_push_queue, nullptr, nullptr, nullptr, nullptr, nullptr);
