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

struct Telemetry
{
    static constexpr solar::remote::DataDescriptor descriptor{
        .id = solar::remote::DataId{0xE101},
        .name = "fixture.multi.telemetry",
    };
    using Value = Sample;
    using Capabilities = solar::remote::Capabilities<solar::remote::OutStream<
        solar::remote::Push, solar::remote::Latest, solar::remote::MaxRate<1000>>>;
};

struct LinkA : solar::remote::testing::InMemoryLink<LinkA, 256, 256>
{
    static constexpr solar::remote::LinkDescriptor descriptor{
        .id = solar::remote::LinkId{0xE001},
        .name = "fixture.multi.a",
    };
    using Grants = solar::remote::Requires<solar::remote::permission::Observe>;
};

struct LinkB : solar::remote::testing::InMemoryLink<LinkB, 256, 256>
{
    static constexpr solar::remote::LinkDescriptor descriptor{
        .id = solar::remote::LinkId{0xE002},
        .name = "fixture.multi.b",
    };
    using Grants = solar::remote::Requires<solar::remote::permission::Observe>;
};

struct Root
{
    static constexpr solar::component::Descriptor descriptor{.name = "fixture.multi.root"};
    using RemoteData = solar::remote::ContributeData<Telemetry>;
    using RemoteLinks = solar::remote::ContributeLinks<LinkA, LinkB>;
};

using System = solar::System<solar::Blueprint<solar::Facilities<Root>>>;
using Service = typename System::RemoteService;
using StateA = solar::remote::detail::LinkState<Service, LinkA, 0>;
using StateB = solar::remote::detail::LinkState<Service, LinkB, 1>;

} // namespace fixture

template <> struct solar::remote::Schema<fixture::Sample>
{
    static constexpr SchemaDescriptor descriptor{
        .id = TypeId{0xE201},
        .name = "fixture.MultiSample",
    };
    using Fields = remote::Fields<Field<1, &fixture::Sample::sequence>>;
    static constexpr std::size_t max_encoded_size = 12;
    static constexpr Codec codec = Codec::Cbor;
};

SOLAR_BIND_SYSTEM(fixture::System);

namespace
{

template <typename LinkT>
solar::Result<solar::remote::frame::Decoded, solar::remote::Error>
receive_frame(std::array<std::byte, 256>& host, std::array<std::byte, 160>& scratch,
              int attempts = 200)
{
    for (int attempt = 0; attempt < attempts; ++attempt) {
        auto bytes = LinkT::take_transmitted(host);
        if (bytes) {
            return solar::remote::frame::decode(std::span{host}.first(*bytes), scratch);
        }
        k_sleep(K_MSEC(1));
    }
    return solar::fail<solar::remote::Error>({.status = solar::Status::Timeout});
}

template <typename LinkT>
void inject(const solar::remote::protocol::Envelope& envelope,
            std::span<const std::byte> payload = {})
{
    std::array<std::byte, 160> scratch{};
    std::array<std::byte, 256> encoded{};
    auto size = solar::remote::frame::encode(envelope, payload, scratch, encoded);
    zassert_true(size.has_value());
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (LinkT::inject(std::span{encoded}.first(*size))) {
            return;
        }
        k_sleep(K_MSEC(1));
    }
    zassert_unreachable("host frame was not admitted");
}

template <typename LinkT>
void establish(std::array<std::byte, 256>& host, std::array<std::byte, 160>& scratch)
{
    zassert_true(LinkT::connect().has_value());
    zassert_true(receive_frame<LinkT>(host, scratch).has_value());
    solar::remote::protocol::Envelope hello{
        .kind = solar::remote::protocol::Kind::ClientHello,
        .frame_sequence = 1,
        .fragment_count = 1,
    };
    constexpr auto hello_payload = fixture::Service::hello_payload();
    inject<LinkT>(hello, hello_payload);
    auto response = receive_frame<LinkT>(host, scratch);
    zassert_true(response.has_value());
    zassert_equal(response->envelope.kind, solar::remote::protocol::Kind::ServerHello);
}

template <typename LinkT>
void subscribe(std::array<std::byte, 256>& host, std::array<std::byte, 160>& scratch)
{
    solar::remote::protocol::Envelope request{
        .kind = solar::remote::protocol::Kind::Subscribe,
        .session_epoch = 1,
        .frame_sequence = 2,
        .target = fixture::Telemetry::descriptor.id.value,
        .request_id = 1,
        .fragment_count = 1,
    };
    inject<LinkT>(request);
    auto response = receive_frame<LinkT>(host, scratch);
    zassert_true(response.has_value());
    zassert_equal(response->envelope.kind, solar::remote::protocol::Kind::Response);
    request.kind = solar::remote::protocol::Kind::ResponseAck;
    request.frame_sequence = 3;
    inject<LinkT>(request);
}

template <typename LinkT>
std::uint32_t receive_sample(std::array<std::byte, 256>& host, std::array<std::byte, 160>& scratch)
{
    auto frame = receive_frame<LinkT>(host, scratch);
    zassert_true(frame.has_value());
    zassert_equal(frame->envelope.kind, solar::remote::protocol::Kind::Data);
    auto sample = solar::remote::cbor::decode<fixture::Sample>(frame->payload);
    zassert_true(sample.has_value());
    return sample->sequence;
}

} // namespace

ZTEST(remote_multi_session, test_subscriptions_and_disconnect_are_link_local)
{
    zassert_true(fixture::System::boot().has_value());
    std::array<std::byte, 256> host_a{};
    std::array<std::byte, 256> host_b{};
    std::array<std::byte, 160> scratch_a{};
    std::array<std::byte, 160> scratch_b{};
    establish<fixture::LinkA>(host_a, scratch_a);
    establish<fixture::LinkB>(host_b, scratch_b);

    subscribe<fixture::LinkA>(host_a, scratch_a);
    zassert_true(solar::remote::interested<fixture::Telemetry>());
    zassert_true(solar::remote::write<fixture::Telemetry>({.sequence = 1}).has_value());
    zassert_equal(receive_sample<fixture::LinkA>(host_a, scratch_a), 1);
    zassert_false(receive_frame<fixture::LinkB>(host_b, scratch_b, 20).has_value());

    subscribe<fixture::LinkB>(host_b, scratch_b);
    zassert_true(solar::remote::write<fixture::Telemetry>({.sequence = 2}).has_value());
    zassert_equal(receive_sample<fixture::LinkA>(host_a, scratch_a), 2);
    zassert_equal(receive_sample<fixture::LinkB>(host_b, scratch_b), 2);
    zassert_equal(fixture::StateA::subscription_count, 1);
    zassert_equal(fixture::StateB::subscription_count, 1);

    fixture::LinkA::disconnect();
    for (int attempt = 0; attempt < 100 && fixture::StateA::connected.load(); ++attempt) {
        k_sleep(K_MSEC(1));
    }
    zassert_false(fixture::StateA::connected.load());
    zassert_true(fixture::StateB::connected.load());
    zassert_true(solar::remote::interested<fixture::Telemetry>());

    zassert_true(solar::remote::write<fixture::Telemetry>({.sequence = 3}).has_value());
    zassert_equal(receive_sample<fixture::LinkB>(host_b, scratch_b), 3);
    zassert_false(receive_frame<fixture::LinkA>(host_a, scratch_a, 20).has_value());

    zassert_true(fixture::System::stop().has_value());
}

ZTEST_SUITE(remote_multi_session, nullptr, nullptr, nullptr, nullptr, nullptr);
