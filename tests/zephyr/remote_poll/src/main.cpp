#include <array>
#include <atomic>
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
    constexpr bool operator==(const Sample&) const = default;
};

using PollQueue = solar::execution::WorkQueue<"remote-poll", solar::execution::StackSize<2048>,
                                              solar::execution::Priority<2>>;

struct Polled
{
    static constexpr solar::remote::DataDescriptor descriptor{
        .id = solar::remote::DataId{0x8101},
        .name = "fixture.polled",
    };
    using Value = Sample;
    inline static std::atomic_uint32_t calls{};
    inline static std::atomic_uint32_t delay_ms{};

    static Value read()
    {
        const auto call = calls.fetch_add(1, std::memory_order_acq_rel) + 1;
        const auto delay = delay_ms.load(std::memory_order_acquire);
        if (delay != 0) {
            k_sleep(K_MSEC(delay));
        }
        return {.sequence = call};
    }

    using Capabilities = solar::remote::Capabilities<
        solar::remote::OutStream<solar::remote::Poll<&Polled::read, solar::remote::On<PollQueue>>,
                                 solar::remote::MaxRate<100>>>;
};

struct TestLink : solar::remote::testing::InMemoryLink<TestLink, 256, 256>
{
    static constexpr solar::remote::LinkDescriptor descriptor{
        .id = solar::remote::LinkId{0x8001},
        .name = "fixture.poll.memory",
    };
    using Grants = solar::remote::Requires<solar::remote::permission::Observe>;
};

struct Root
{
    static constexpr solar::component::Descriptor descriptor{.name = "fixture.poll.root"};
    using RemoteData = solar::remote::ContributeData<Polled>;
    using RemoteLinks = solar::remote::ContributeLinks<TestLink>;
};

using System =
    solar::System<solar::Blueprint<solar::Facilities<Root>, solar::Executors<PollQueue>>>;
using Service = typename System::RemoteService;
using LinkState = solar::remote::detail::LinkState<Service, TestLink, 0>;
using PollState = decltype(solar::remote::detail::poll_state<System, Polled>());
using PollRegistration = Service::PollRegistration<Polled>;

} // namespace fixture

template <> struct solar::remote::Schema<fixture::Sample>
{
    static constexpr SchemaDescriptor descriptor{
        .id = TypeId{0x8201},
        .name = "fixture.PollSample",
    };
    using Fields = remote::Fields<Field<1, "sequence", &fixture::Sample::sequence>>;
    static constexpr std::size_t max_encoded_size = 12;
    static constexpr Codec codec = Codec::Cbor;
};

SOLAR_BIND_SYSTEM(fixture::System);

namespace
{

std::array<std::byte, 256> host_rx{};
std::array<std::byte, 160> scratch{};
std::array<std::byte, 256> encoded{};

solar::Result<solar::remote::frame::Decoded, solar::remote::Error>
receive_frame(std::array<std::byte, 160>& decoded_storage, int attempts = 200)
{
    for (int attempt = 0; attempt < attempts; ++attempt) {
        auto bytes = fixture::TestLink::take_transmitted(host_rx);
        if (bytes) {
            return solar::remote::frame::decode(std::span{host_rx}.first(*bytes), decoded_storage);
        }
        k_sleep(K_MSEC(1));
    }
    return solar::fail<solar::remote::Error>({
        .status = solar::Status::Timeout,
        .reason = solar::remote::Reason::Busy,
        .operation = solar::remote::Operation::Receive,
    });
}

void inject(const solar::remote::protocol::Envelope& envelope,
            std::span<const std::byte> payload = {})
{
    auto size = solar::remote::frame::encode(envelope, payload, scratch, encoded);
    zassert_true(size.has_value());
    for (int attempt = 0; attempt < 100; ++attempt) {
        auto result = fixture::TestLink::inject(std::span{encoded}.first(*size));
        if (result) {
            return;
        }
        k_sleep(K_MSEC(1));
    }
    zassert_unreachable("link did not accept host frame");
}

} // namespace

ZTEST(remote_poll, test_subscription_activated_poll_and_overlap_skip)
{
    fixture::Polled::calls.store(0);
    fixture::Polled::delay_ms.store(0);
    zassert_true(fixture::System::boot().has_value());
    auto registration = solar::execution::registration<fixture::PollRegistration>();
    zassert_true(registration.has_value());
    zassert_equal(registration->target_kind, solar::execution::TargetKind::OwnedWorkQueue);
    zassert_true(fixture::TestLink::connect().has_value());

    std::array<std::byte, 160> decoded_storage{};
    auto server_hello = receive_frame(decoded_storage);
    zassert_true(server_hello.has_value());
    zassert_equal(server_hello->envelope.kind, solar::remote::protocol::Kind::ServerHello);

    solar::remote::protocol::Envelope client_hello{
        .kind = solar::remote::protocol::Kind::ClientHello,
        .frame_sequence = 1,
        .fragment_count = 1,
    };
    constexpr auto hello_payload = fixture::Service::hello_payload();
    inject(client_hello, hello_payload);
    auto hello_response = receive_frame(decoded_storage);
    zassert_true(hello_response.has_value());
    zassert_equal(hello_response->envelope.session_epoch, 1);

    k_sleep(K_MSEC(120));
    zassert_equal(fixture::Polled::calls.load(), 0);

    solar::remote::protocol::Envelope subscribe{
        .kind = solar::remote::protocol::Kind::Subscribe,
        .session_epoch = 1,
        .frame_sequence = 2,
        .target = fixture::Polled::descriptor.id.value,
        .request_id = 1,
        .fragment_count = 1,
    };
    constexpr solar::remote::protocol::SubscriptionRequest requested{
        .minimum_interval_us = 20'000,
    };
    constexpr auto requested_bytes = solar::remote::protocol::encode(requested);
    inject(subscribe, requested_bytes);
    auto subscribed = receive_frame(decoded_storage);
    zassert_true(subscribed.has_value());
    zassert_equal(subscribed->envelope.kind, solar::remote::protocol::Kind::Response);
    auto policy = solar::remote::protocol::decode_subscription_policy(subscribed->payload);
    zassert_true(policy.has_value());
    zassert_equal(policy->minimum_interval_us, 20'000);

    solar::remote::protocol::Envelope acknowledge{
        .kind = solar::remote::protocol::Kind::ResponseAck,
        .session_epoch = 1,
        .frame_sequence = 3,
        .target = fixture::Polled::descriptor.id.value,
        .request_id = 1,
        .fragment_count = 1,
    };
    inject(acknowledge);

    std::uint32_t last_sequence{};
    for (int sample{}; sample < 3; ++sample) {
        auto data = receive_frame(decoded_storage, 300);
        zassert_true(data.has_value());
        zassert_equal(data->envelope.kind, solar::remote::protocol::Kind::Data);
        auto value = solar::remote::cbor::decode<fixture::Sample>(data->payload);
        zassert_true(value.has_value());
        zassert_true(value->sequence > last_sequence);
        last_sequence = value->sequence;
    }

    fixture::Polled::delay_ms.store(80);
    for (int attempt = 0;
         attempt < 300 &&
         solar::remote::detail::poll_state<fixture::System, fixture::Polled>().skipped.load() == 0;
         ++attempt) {
        k_sleep(K_MSEC(1));
    }
    auto& poll = solar::remote::detail::poll_state<fixture::System, fixture::Polled>();
    zassert_true(poll.releases.load() >= 4);
    zassert_true(poll.skipped.load() >= 1);

    solar::remote::protocol::Envelope unsubscribe{
        .kind = solar::remote::protocol::Kind::Unsubscribe,
        .session_epoch = 1,
        .frame_sequence = 4,
        .target = fixture::Polled::descriptor.id.value,
        .request_id = 2,
        .fragment_count = 1,
    };
    inject(unsubscribe);
    auto unsubscribed = receive_frame(decoded_storage, 400);
    zassert_true(unsubscribed.has_value());
    while (unsubscribed->envelope.kind == solar::remote::protocol::Kind::Data) {
        unsubscribed = receive_frame(decoded_storage, 400);
        zassert_true(unsubscribed.has_value());
    }
    zassert_equal(unsubscribed->envelope.kind, solar::remote::protocol::Kind::Response);
    zassert_equal(unsubscribed->envelope.request_id, 2);

    fixture::Polled::delay_ms.store(0);
    for (int attempt = 0; attempt < 200 && poll.in_flight.load(); ++attempt) {
        k_sleep(K_MSEC(1));
    }
    const auto stopped_at = fixture::Polled::calls.load();
    k_sleep(K_MSEC(120));
    zassert_equal(fixture::Polled::calls.load(), stopped_at);

    zassert_true(fixture::System::stop().has_value());
}

ZTEST_SUITE(remote_poll, nullptr, nullptr, nullptr, nullptr, nullptr);
