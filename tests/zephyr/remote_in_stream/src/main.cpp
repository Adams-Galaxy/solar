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

K_SEM_DEFINE(consumer_gate, 0, 3);

namespace fixture
{

struct Setpoint
{
    std::uint32_t sequence{};
};

using InboundQueue = solar::execution::WorkQueue<
    "remote-inbound", solar::execution::StackSize<2048>,
    solar::execution::Priority<2>>;

struct Commands
{
    static constexpr solar::remote::DataDescriptor descriptor{
        .id = solar::remote::DataId{0xC101},
        .name = "fixture.commands",
    };
    using Value = Setpoint;
    inline static std::atomic_uint32_t started{};
    inline static std::atomic_uint32_t completed{};
    inline static std::array<std::uint32_t, 3> order{};

    static solar::Status consume(const Value& value)
    {
        started.fetch_add(1, std::memory_order_relaxed);
        if (k_sem_take(&consumer_gate, K_SECONDS(2)) != 0) {
            return solar::Status::Timeout;
        }
        const auto index = completed.fetch_add(1, std::memory_order_relaxed);
        order[index] = value.sequence;
        return solar::Status::Ok;
    }

    using Capabilities = solar::remote::Capabilities<solar::remote::InStream<
        &Commands::consume, solar::remote::ReliableWindow<2>,
        solar::remote::On<InboundQueue>>>;
};

struct TestLink : solar::remote::testing::InMemoryLink<TestLink, 256, 256>
{
    static constexpr solar::remote::LinkDescriptor descriptor{
        .id = solar::remote::LinkId{0xC001},
        .name = "fixture.inbound.memory",
    };
    using Grants = solar::remote::Requires<solar::remote::permission::Control>;
};

struct Root
{
    static constexpr solar::component::Descriptor descriptor{.name = "fixture.inbound.root"};
    using RemoteData = solar::remote::ContributeData<Commands>;
    using RemoteLinks = solar::remote::ContributeLinks<TestLink>;
};

using System = solar::System<solar::Blueprint<solar::Facilities<Root>,
                                            solar::Executors<InboundQueue>>>;
using Service = typename System::RemoteService;
using LinkState = solar::remote::detail::LinkState<Service, TestLink, 0>;
using InStreamRegistration = Service::InStreamRegistration<Commands>;

} // namespace fixture

template <> struct solar::remote::Schema<fixture::Setpoint>
{
    static constexpr SchemaDescriptor descriptor{
        .id = TypeId{0xC201},
        .name = "fixture.Setpoint",
    };
    using Fields = remote::Fields<Field<1, &fixture::Setpoint::sequence>>;
    static constexpr std::size_t max_encoded_size = 12;
    static constexpr Codec codec = Codec::Cbor;
};

SOLAR_BIND_SYSTEM(fixture::System);

namespace
{

std::array<std::byte, 256> host_bytes{};
std::array<std::byte, 160> decoded_bytes{};

solar::Result<solar::remote::frame::Decoded, solar::remote::Error>
receive_frame(int attempts = 300)
{
    for (int attempt = 0; attempt < attempts; ++attempt) {
        auto bytes = fixture::TestLink::take_transmitted(host_bytes);
        if (bytes) {
            return solar::remote::frame::decode(std::span{host_bytes}.first(*bytes),
                                                decoded_bytes);
        }
        k_sleep(K_MSEC(1));
    }
    return solar::fail(solar::remote::Error{.status = solar::Status::Timeout});
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

void inject_setpoint(std::uint32_t frame_sequence, std::uint32_t value)
{
    std::array<std::byte, 32> payload{};
    auto encoded = solar::remote::cbor::encode(fixture::Setpoint{.sequence = value}, payload);
    zassert_true(encoded.has_value());
    solar::remote::protocol::Envelope envelope{
        .kind = solar::remote::protocol::Kind::Data,
        .session_epoch = 1,
        .frame_sequence = frame_sequence,
        .target = fixture::Commands::descriptor.id.value,
        .fragment_count = 1,
    };
    envelope.set_operation(solar::remote::protocol::OperationKind::InStream);
    inject(envelope, std::span{payload}.first(*encoded));
}

void inject_fragmented_setpoint(std::uint32_t frame_sequence, std::uint32_t value,
                                std::uint16_t fragment_id)
{
    std::array<std::byte, 32> payload{};
    auto encoded = solar::remote::cbor::encode(fixture::Setpoint{.sequence = value}, payload);
    zassert_true(encoded.has_value());
    const auto split = *encoded / 2;
    solar::remote::protocol::Envelope envelope{
        .kind = solar::remote::protocol::Kind::Data,
        .flags = solar::remote::protocol::Flags::Fragmented,
        .session_epoch = 1,
        .frame_sequence = frame_sequence,
        .target = fixture::Commands::descriptor.id.value,
        .fragment_id = fragment_id,
        .fragment_index = 0,
        .fragment_count = 2,
    };
    envelope.set_operation(solar::remote::protocol::OperationKind::InStream);
    inject(envelope, std::span{payload}.first(split));
    envelope.flags = solar::remote::protocol::Flags::Fragmented |
                     solar::remote::protocol::Flags::Final;
    envelope.frame_sequence = frame_sequence + 1;
    envelope.fragment_index = 1;
    inject(envelope, std::span{payload}.subspan(split, *encoded - split));
}

void inject_incomplete_setpoint(std::uint32_t frame_sequence, std::uint32_t value,
                                std::uint16_t fragment_id)
{
    std::array<std::byte, 32> payload{};
    auto encoded = solar::remote::cbor::encode(fixture::Setpoint{.sequence = value}, payload);
    zassert_true(encoded.has_value());
    solar::remote::protocol::Envelope envelope{
        .kind = solar::remote::protocol::Kind::Data,
        .flags = solar::remote::protocol::Flags::Fragmented,
        .session_epoch = 1,
        .frame_sequence = frame_sequence,
        .target = fixture::Commands::descriptor.id.value,
        .fragment_id = fragment_id,
        .fragment_index = 0,
        .fragment_count = 2,
    };
    envelope.set_operation(solar::remote::protocol::OperationKind::InStream);
    inject(envelope, std::span{payload}.first(*encoded / 2));
}

} // namespace

ZTEST(remote_in_stream, test_credit_window_owns_orders_and_releases_values)
{
    zassert_true(fixture::System::boot().has_value());
    auto registration =
        solar::execution::registration<fixture::InStreamRegistration>();
    zassert_true(registration.has_value());
    zassert_equal(registration->target_kind,
                  solar::execution::TargetKind::OwnedWorkQueue);
    zassert_equal(fixture::TestLink::connect(), solar::Status::Ok);
    zassert_true(receive_frame().has_value());

    solar::remote::protocol::Envelope hello{
        .kind = solar::remote::protocol::Kind::ClientHello,
        .frame_sequence = 1,
        .fragment_count = 1,
    };
    constexpr auto hello_payload = fixture::Service::hello_payload();
    inject(hello, hello_payload);
    auto hello_response = receive_frame();
    zassert_true(hello_response.has_value());
    zassert_equal(hello_response->envelope.kind,
                  solar::remote::protocol::Kind::ServerHello);

    auto initial_credit = receive_frame();
    zassert_true(initial_credit.has_value());
    zassert_equal(initial_credit->envelope.kind, solar::remote::protocol::Kind::Credit);
    zassert_equal(initial_credit->envelope.operation(),
                  solar::remote::protocol::OperationKind::InStream);
    auto grant = solar::remote::protocol::decode_credit_grant(initial_credit->payload);
    zassert_true(grant.has_value());
    zassert_equal(grant->credits, 2);
    zassert_equal(grant->window, 2);

    inject_setpoint(2, 10);
    inject_setpoint(3, 20);
    for (int attempt = 0; attempt < 100 && fixture::Commands::started.load() == 0; ++attempt) {
        k_sleep(K_MSEC(1));
    }
    zassert_equal(fixture::Commands::started.load(), 1);

    inject_setpoint(4, 30);
    auto violation = receive_frame();
    zassert_true(violation.has_value());
    zassert_equal(violation->envelope.kind, solar::remote::protocol::Kind::Error);
    zassert_equal(solar::remote::protocol::detail::get_u16(violation->payload, 0),
                  static_cast<std::uint16_t>(
                      solar::remote::protocol::ErrorCode::CreditViolation));

    k_sem_give(&consumer_gate);
    k_sem_give(&consumer_gate);
    for (int attempt = 0; attempt < 200 && fixture::Commands::completed.load() != 2; ++attempt) {
        k_sleep(K_MSEC(1));
    }
    zassert_equal(fixture::Commands::completed.load(), 2);
    zassert_equal(fixture::Commands::order[0], 10);
    zassert_equal(fixture::Commands::order[1], 20);

    std::uint16_t returned{};
    for (int index = 0; index < 2; ++index) {
        auto credit = receive_frame();
        zassert_true(credit.has_value());
        zassert_equal(credit->envelope.kind, solar::remote::protocol::Kind::Credit);
        auto decoded = solar::remote::protocol::decode_credit_grant(credit->payload);
        zassert_true(decoded.has_value());
        returned += decoded->credits;
    }
    zassert_equal(returned, 2);

    inject_fragmented_setpoint(5, 30, 7);
    k_sem_give(&consumer_gate);
    for (int attempt = 0; attempt < 200 && fixture::Commands::completed.load() != 3; ++attempt) {
        k_sleep(K_MSEC(1));
    }
    zassert_equal(fixture::Commands::completed.load(), 3);
    zassert_equal(fixture::Commands::order[2], 30);
    zassert_true(receive_frame().has_value());

    auto& state = solar::remote::detail::in_stream_state<fixture::System,
                                                         fixture::Commands>();
    {
        auto guard = state.lock.acquire();
        zassert_equal(state.admitted, 3);
        zassert_equal(state.completed, 3);
        zassert_equal(state.rejected, 1);
        zassert_equal(state.consumer_failures, 0);
        zassert_equal(state.credits[0], 2);
    }


    const auto rejected_before = fixture::LinkState::rejected_frames.load();
    inject_incomplete_setpoint(7, 40, 9);
    for (int attempt = 0; attempt < 400 &&
                          fixture::LinkState::rejected_frames.load() == rejected_before;
         ++attempt) {
        k_sleep(K_MSEC(1));
    }
    zassert_equal(fixture::Commands::completed.load(), 3);
    zassert_true(fixture::LinkState::rejected_frames.load() > rejected_before);
    zassert_false(fixture::LinkState::reassembly[0].active);

    zassert_true(fixture::System::stop().has_value());
}

ZTEST_SUITE(remote_in_stream, nullptr, nullptr, nullptr, nullptr, nullptr);
