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

struct DriveInputGroup
{
    static constexpr solar::remote::InStreamGroupDescriptor descriptor{
        .id = solar::remote::InStreamGroupId{0xC301},
        .name = "fixture.drive-input",
        .description = "Mutually exclusive drive control modes",
    };
};

using InboundQueue =
    solar::execution::WorkQueue<"remote-inbound", solar::execution::StackSize<2048>,
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
    inline static std::atomic_uint32_t opens{};
    inline static std::atomic_uint32_t closes{};
    inline static std::atomic_uint32_t opened_token{};
    inline static std::atomic_uint32_t closed_token{};
    inline static std::atomic_uint8_t close_reason{};

    static solar::Result<void> consume(const Value& value)
    {
        started.fetch_add(1, std::memory_order_relaxed);
        if (k_sem_take(&consumer_gate, K_SECONDS(2)) != 0) {
            return solar::fail<solar::Error>({.status = solar::Status::Timeout});
        }
        const auto index = completed.fetch_add(1, std::memory_order_relaxed);
        order[index] = value.sequence;
        return {};
    }

    static void open(const solar::remote::InStreamOpenContext& context)
    {
        opened_token.store(context.token);
        opens.fetch_add(1);
    }

    static void close(const solar::remote::InStreamCloseContext& context)
    {
        closed_token.store(context.token);
        close_reason.store(static_cast<std::uint8_t>(context.reason));
        closes.fetch_add(1);
    }

    using Capabilities = solar::remote::Capabilities<solar::remote::InStream<
        &Commands::consume, solar::remote::OnOpen<&Commands::open>,
        solar::remote::OnClose<&Commands::close>,
        solar::remote::Exclusive<DriveInputGroup, solar::remote::Replace>,
        solar::remote::ReliableWindow<2>, solar::remote::On<InboundQueue>>>;
};

struct AlternateCommands
{
    static constexpr solar::remote::DataDescriptor descriptor{
        .id = solar::remote::DataId{0xC102},
        .name = "fixture.commands.alternate",
    };
    using Value = Setpoint;
    inline static std::atomic_uint32_t consumed{};
    inline static std::atomic_uint32_t opens{};
    inline static std::atomic_uint32_t closes{};
    inline static std::atomic_uint8_t close_reason{};

    static void consume(const Value&) { consumed.fetch_add(1); }
    static void open() { opens.fetch_add(1); }
    static void close(const solar::remote::InStreamCloseContext& context)
    {
        close_reason.store(static_cast<std::uint8_t>(context.reason));
        closes.fetch_add(1);
    }

    using Capabilities = solar::remote::Capabilities<solar::remote::InStream<
        &AlternateCommands::consume, solar::remote::OnOpen<&AlternateCommands::open>,
        solar::remote::OnClose<&AlternateCommands::close>,
        solar::remote::Exclusive<DriveInputGroup, solar::remote::Replace>,
        solar::remote::ReliableWindow<1>, solar::remote::Inline>>;
};

struct RejectingCommands
{
    static constexpr solar::remote::DataDescriptor descriptor{
        .id = solar::remote::DataId{0xC103},
        .name = "fixture.commands.rejecting",
    };
    using Value = Setpoint;
    inline static std::atomic_uint32_t opens{};
    static void consume(const Value&) {}
    static void open() { opens.fetch_add(1); }
    using Capabilities = solar::remote::Capabilities<solar::remote::InStream<
        &RejectingCommands::consume, solar::remote::OnOpen<&RejectingCommands::open>,
        solar::remote::Exclusive<DriveInputGroup, solar::remote::RejectExisting>,
        solar::remote::ReliableWindow<1>, solar::remote::Inline>>;
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
    using RemoteData =
        solar::remote::ContributeData<Commands, AlternateCommands, RejectingCommands>;
    using RemoteLinks = solar::remote::ContributeLinks<TestLink>;
};

using System =
    solar::System<solar::Blueprint<solar::Facilities<Root>, solar::Executors<InboundQueue>>>;
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
    using Fields = remote::Fields<Field<1, "sequence", &fixture::Setpoint::sequence>>;
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

void inject_setpoint(std::uint32_t frame_sequence, std::uint32_t token, std::uint32_t value)
{
    std::array<std::byte, 32> payload{};
    auto encoded = solar::remote::cbor::encode(fixture::Setpoint{.sequence = value}, payload);
    zassert_true(encoded.has_value());
    solar::remote::protocol::Envelope envelope{
        .kind = solar::remote::protocol::Kind::Data,
        .session_epoch = 1,
        .frame_sequence = frame_sequence,
        .target = fixture::Commands::descriptor.id.value,
        .request_id = token,
        .fragment_count = 1,
    };
    envelope.set_operation(solar::remote::protocol::OperationKind::InStream);
    inject(envelope, std::span{payload}.first(*encoded));
}

void inject_fragmented_setpoint(std::uint32_t frame_sequence, std::uint32_t token,
                                std::uint32_t value, std::uint16_t fragment_id)
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
        .request_id = token,
        .fragment_id = fragment_id,
        .fragment_index = 0,
        .fragment_count = 2,
    };
    envelope.set_operation(solar::remote::protocol::OperationKind::InStream);
    inject(envelope, std::span{payload}.first(split));
    envelope.flags =
        solar::remote::protocol::Flags::Fragmented | solar::remote::protocol::Flags::Final;
    envelope.frame_sequence = frame_sequence + 1;
    envelope.fragment_index = 1;
    inject(envelope, std::span{payload}.subspan(split, *encoded - split));
}

void inject_incomplete_setpoint(std::uint32_t frame_sequence, std::uint32_t token,
                                std::uint32_t value, std::uint16_t fragment_id)
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
        .request_id = token,
        .fragment_id = fragment_id,
        .fragment_index = 0,
        .fragment_count = 2,
    };
    envelope.set_operation(solar::remote::protocol::OperationKind::InStream);
    inject(envelope, std::span{payload}.first(*encoded / 2));
}

void inject_alternate(std::uint32_t frame_sequence, std::uint32_t token,
                      std::uint32_t value)
{
    std::array<std::byte, 32> payload{};
    auto encoded = solar::remote::cbor::encode(fixture::Setpoint{.sequence = value}, payload);
    zassert_true(encoded.has_value());
    solar::remote::protocol::Envelope envelope{
        .kind = solar::remote::protocol::Kind::Data,
        .session_epoch = 1,
        .frame_sequence = frame_sequence,
        .target = fixture::AlternateCommands::descriptor.id.value,
        .request_id = token,
        .fragment_count = 1,
    };
    envelope.set_operation(solar::remote::protocol::OperationKind::InStream);
    inject(envelope, std::span{payload}.first(*encoded));
}

} // namespace

ZTEST(remote_in_stream, test_credit_window_owns_orders_and_releases_values)
{
    zassert_true(fixture::System::boot().has_value());
    auto registration = solar::execution::registration<fixture::InStreamRegistration>();
    zassert_true(registration.has_value());
    zassert_equal(registration->target_kind, solar::execution::TargetKind::OwnedWorkQueue);
    zassert_true(fixture::TestLink::connect().has_value());
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
    zassert_equal(hello_response->envelope.kind, solar::remote::protocol::Kind::ServerHello);

    solar::remote::protocol::Envelope open{
        .kind = solar::remote::protocol::Kind::Subscribe,
        .session_epoch = 1,
        .frame_sequence = 2,
        .target = fixture::Commands::descriptor.id.value,
        .request_id = 1,
        .fragment_count = 1,
    };
    open.set_subscription(solar::remote::protocol::SubscriptionKind::DataInStream);
    constexpr auto open_request =
        solar::remote::protocol::encode(solar::remote::protocol::SubscriptionRequest{});
    inject(open, open_request);
    auto open_response = receive_frame();
    zassert_true(open_response.has_value());
    zassert_equal(
        open_response->envelope.kind, solar::remote::protocol::Kind::Response,
        "open returned kind %u, error %u",
        static_cast<unsigned>(open_response->envelope.kind),
        open_response->payload.size() >= 2
            ? solar::remote::protocol::detail::get_u16(open_response->payload, 0)
            : 0U);
    auto opened =
        solar::remote::protocol::decode_in_stream_open_response(open_response->payload);
    zassert_true(opened.has_value());
    zassert_not_equal(opened->token, 0);

    auto initial_credit = receive_frame();
    zassert_true(initial_credit.has_value());
    zassert_equal(initial_credit->envelope.kind, solar::remote::protocol::Kind::Credit);
    zassert_equal(initial_credit->envelope.operation(),
                  solar::remote::protocol::OperationKind::InStream);
    auto grant = solar::remote::protocol::decode_credit_grant(initial_credit->payload);
    zassert_true(grant.has_value());
    zassert_equal(grant->credits, 2);
    zassert_equal(grant->window, 2);
    zassert_equal(grant->token, opened->token);

    inject_setpoint(3, opened->token, 10);
    inject_setpoint(4, opened->token, 20);
    for (int attempt = 0; attempt < 100 && fixture::Commands::started.load() == 0; ++attempt) {
        k_sleep(K_MSEC(1));
    }
    zassert_equal(fixture::Commands::started.load(), 1);

    inject_setpoint(5, opened->token, 30);
    auto violation = receive_frame();
    zassert_true(violation.has_value());
    zassert_equal(violation->envelope.kind, solar::remote::protocol::Kind::Error);
    zassert_equal(solar::remote::protocol::detail::get_u16(violation->payload, 0),
                  static_cast<std::uint16_t>(solar::remote::protocol::ErrorCode::CreditViolation));

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
        zassert_equal(decoded->token, opened->token);
        returned += decoded->credits;
    }
    zassert_equal(returned, 2);

    inject_fragmented_setpoint(6, opened->token, 30, 7);
    k_sem_give(&consumer_gate);
    for (int attempt = 0; attempt < 200 && fixture::Commands::completed.load() != 3; ++attempt) {
        k_sleep(K_MSEC(1));
    }
    zassert_equal(fixture::Commands::completed.load(), 3);
    zassert_equal(fixture::Commands::order[2], 30);
    zassert_true(receive_frame().has_value());

    auto& state = solar::remote::detail::in_stream_state<fixture::System, fixture::Commands>();
    {
        auto guard = state.lock.acquire();
        zassert_equal(state.admitted, 3);
        zassert_equal(state.completed, 3);
        zassert_equal(state.rejected, 1);
        zassert_equal(state.consumer_failures, 0);
        zassert_equal(state.credits[0], 2);
    }

    const auto rejected_before = fixture::LinkState::rejected_frames.load();
    inject_incomplete_setpoint(8, opened->token, 40, 9);
    for (int attempt = 0;
         attempt < 400 && fixture::LinkState::rejected_frames.load() == rejected_before;
         ++attempt) {
        k_sleep(K_MSEC(1));
    }
    zassert_equal(fixture::Commands::completed.load(), 3);
    zassert_true(fixture::LinkState::rejected_frames.load() > rejected_before);
    zassert_false(fixture::LinkState::reassembly[0].active);

    solar::remote::protocol::Envelope replace{
        .kind = solar::remote::protocol::Kind::Subscribe,
        .session_epoch = 1,
        .frame_sequence = 9,
        .target = fixture::AlternateCommands::descriptor.id.value,
        .request_id = 2,
        .fragment_count = 1,
    };
    replace.set_subscription(solar::remote::protocol::SubscriptionKind::DataInStream);
    inject(replace, open_request);

    std::uint32_t alternate_token{};
    bool saw_replacement{};
    bool saw_initial_credit{};
    for (int index = 0; index < 3; ++index) {
        auto message = receive_frame();
        zassert_true(message.has_value());
        if (message->envelope.kind == solar::remote::protocol::Kind::Response) {
            auto replacement_open =
                solar::remote::protocol::decode_in_stream_open_response(message->payload);
            zassert_true(replacement_open.has_value());
            alternate_token = replacement_open->token;
        } else if (message->envelope.kind == solar::remote::protocol::Kind::Credit) {
            auto replacement_credit =
                solar::remote::protocol::decode_credit_grant(message->payload);
            zassert_true(replacement_credit.has_value());
            alternate_token = replacement_credit->token;
            saw_initial_credit = replacement_credit->credits == 1;
        } else if (message->envelope.kind ==
                   solar::remote::protocol::Kind::InStreamClosed) {
            auto closed =
                solar::remote::protocol::decode_in_stream_closed(message->payload);
            zassert_true(closed.has_value());
            zassert_equal(closed->token, opened->token);
            zassert_equal(closed->reason, solar::remote::InStreamCloseReason::Replaced);
            saw_replacement = true;
        }
    }
    zassert_not_equal(alternate_token, 0);
    zassert_true(saw_initial_credit);
    zassert_true(saw_replacement);
    zassert_equal(fixture::Commands::opens.load(), 1);
    zassert_equal(fixture::Commands::closes.load(), 1);
    zassert_equal(fixture::Commands::opened_token.load(), opened->token);
    zassert_equal(fixture::Commands::closed_token.load(), opened->token);
    zassert_equal(
        fixture::Commands::close_reason.load(),
        static_cast<std::uint8_t>(solar::remote::InStreamCloseReason::Replaced));
    zassert_equal(fixture::AlternateCommands::opens.load(), 1);

    inject_setpoint(10, opened->token, 99);
    auto stale = receive_frame();
    zassert_true(stale.has_value());
    zassert_equal(stale->envelope.kind, solar::remote::protocol::Kind::Error);
    zassert_equal(
        solar::remote::protocol::detail::get_u16(stale->payload, 0),
        static_cast<std::uint16_t>(solar::remote::protocol::ErrorCode::RequestExpired));
    zassert_equal(fixture::Commands::completed.load(), 3);

    inject_alternate(11, alternate_token, 50);
    for (int attempt = 0;
         attempt < 100 && fixture::AlternateCommands::consumed.load() == 0; ++attempt) {
        k_sleep(K_MSEC(1));
    }
    zassert_equal(fixture::AlternateCommands::consumed.load(), 1);
    auto returned_credit = receive_frame();
    zassert_true(returned_credit.has_value());
    zassert_equal(returned_credit->envelope.kind, solar::remote::protocol::Kind::Credit);

    solar::remote::protocol::Envelope rejected_open{
        .kind = solar::remote::protocol::Kind::Subscribe,
        .session_epoch = 1,
        .frame_sequence = 12,
        .target = fixture::RejectingCommands::descriptor.id.value,
        .request_id = 3,
        .fragment_count = 1,
    };
    rejected_open.set_subscription(
        solar::remote::protocol::SubscriptionKind::DataInStream);
    inject(rejected_open, open_request);
    auto rejected = receive_frame();
    zassert_true(rejected.has_value());
    zassert_equal(rejected->envelope.kind, solar::remote::protocol::Kind::Error);
    zassert_equal(
        solar::remote::protocol::detail::get_u16(rejected->payload, 0),
        static_cast<std::uint16_t>(solar::remote::protocol::ErrorCode::Busy));
    zassert_equal(fixture::RejectingCommands::opens.load(), 0);
    zassert_equal(fixture::AlternateCommands::closes.load(), 0);

    solar::remote::protocol::Envelope close{
        .kind = solar::remote::protocol::Kind::Unsubscribe,
        .session_epoch = 1,
        .frame_sequence = 13,
        .target = fixture::AlternateCommands::descriptor.id.value,
        .request_id = 4,
        .fragment_count = 1,
    };
    close.set_subscription(solar::remote::protocol::SubscriptionKind::DataInStream);
    const auto close_request = solar::remote::protocol::encode(
        solar::remote::protocol::InStreamCloseRequest{.token = alternate_token});
    inject(close, close_request);
    auto close_response = receive_frame();
    zassert_true(close_response.has_value());
    zassert_equal(close_response->envelope.kind, solar::remote::protocol::Kind::Response);
    zassert_equal(fixture::AlternateCommands::closes.load(), 1);
    zassert_equal(
        fixture::AlternateCommands::close_reason.load(),
        static_cast<std::uint8_t>(solar::remote::InStreamCloseReason::Closed));

    replace.frame_sequence = 14;
    replace.request_id = 5;
    inject(replace, open_request);
    auto reopened_response = receive_frame();
    auto reopened_credit = receive_frame();
    zassert_true(reopened_response.has_value());
    zassert_true(reopened_credit.has_value());
    zassert_equal(reopened_response->envelope.kind,
                  solar::remote::protocol::Kind::Response);
    zassert_equal(reopened_credit->envelope.kind, solar::remote::protocol::Kind::Credit);

    inject(solar::remote::protocol::Envelope{
        .kind = solar::remote::protocol::Kind::SessionReset,
        .session_epoch = 1,
        .frame_sequence = 15,
        .fragment_count = 1,
    });
    auto reset_hello = receive_frame();
    zassert_true(reset_hello.has_value());
    zassert_equal(reset_hello->envelope.kind, solar::remote::protocol::Kind::ServerHello);
    zassert_equal(fixture::AlternateCommands::closes.load(), 2);
    zassert_equal(
        fixture::AlternateCommands::close_reason.load(),
        static_cast<std::uint8_t>(solar::remote::InStreamCloseReason::Reset));

    zassert_true(fixture::System::stop().has_value());
}

ZTEST_SUITE(remote_in_stream, nullptr, nullptr, nullptr, nullptr, nullptr);
