#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include <zephyr/irq_offload.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <solar/remote.hpp>
#include <solar/remote/testing/in_memory_link.hpp>
#include <solar/solar.hpp>

namespace fixture
{

using ControlQueue =
    solar::execution::WorkQueue<"remote-control", solar::execution::StackSize<2048>,
                                solar::execution::Priority<2>>;

struct Sample
{
    std::uint32_t sequence{};
    float value{};
    constexpr bool operator==(const Sample&) const = default;
};

struct Telemetry
{
    static constexpr solar::remote::DataDescriptor descriptor{
        .id = solar::remote::DataId{0x7101},
        .name = "fixture.telemetry",
    };
    using Value = Sample;
    inline static std::atomic_uint32_t queries{};
    inline static Value updated{};
    using Execution = solar::remote::On<ControlQueue>;

    static Value read_latest()
    {
        queries.fetch_add(1);
        return {.sequence = 99, .value = 2.5F};
    }

    static solar::Result<void> update(const Value& value)
    {
        updated = value;
        return {};
    }

    using Capabilities = solar::remote::Capabilities<
        solar::remote::Query<&Telemetry::read_latest>, solar::remote::Update<&Telemetry::update>,
        solar::remote::OutStream<solar::remote::Push, solar::remote::Latest,
                                 solar::remote::MaxRate<200>>>;
};

struct ScaleRequest
{
    std::int32_t value{};
};

struct ScaleResponse
{
    std::int32_t value{};
    constexpr bool operator==(const ScaleResponse&) const = default;
};

struct Scale
{
    static constexpr solar::remote::ActionDescriptor descriptor{
        .id = solar::remote::ActionId{0x7301},
        .name = "fixture.scale",
    };
    using Request = ScaleRequest;
    using Response = ScaleResponse;
    using Access = solar::remote::Requires<solar::remote::permission::Control>;
    using Execution = solar::remote::On<ControlQueue>;
    inline static std::atomic_uint32_t calls{};

    static Response execute(const Request& request)
    {
        calls.fetch_add(1);
        return {.value = request.value * 2};
    }
};

struct InlinePing
{
    static constexpr solar::remote::ActionDescriptor descriptor{
        .id = solar::remote::ActionId{0x7302},
        .name = "fixture.inline_ping",
    };
    using Execution = solar::remote::Inline;
    inline static std::atomic_uint32_t calls{};

    static void execute()
    {
        calls.fetch_add(1);
    }
};

struct AsyncScale
{
    static constexpr solar::remote::ActionDescriptor descriptor{
        .id = solar::remote::ActionId{0x7303},
        .name = "fixture.async_scale",
    };
    using Request = ScaleRequest;
    using Response = ScaleResponse;
    using Access = solar::remote::Requires<solar::remote::permission::Control>;
    using Execution = solar::remote::On<ControlQueue>;
    inline static std::atomic_int request_value{};
    inline static std::optional<solar::remote::Responder<AsyncScale>> pending{};

    static void execute(const Request& request,
                        solar::remote::Responder<AsyncScale> responder) noexcept
    {
        request_value.store(request.value, std::memory_order_release);
        pending.emplace(std::move(responder));
    }
};

struct TestLink : solar::remote::testing::InMemoryLink<TestLink, 256, 256>
{
    static constexpr solar::remote::LinkDescriptor descriptor{
        .id = solar::remote::LinkId{0x7001},
        .name = "fixture.memory",
    };
    using Grants = solar::remote::Requires<solar::remote::permission::Observe,
                                           solar::remote::permission::Configure,
                                           solar::remote::permission::Control>;
};

struct Root
{
    static constexpr solar::component::Descriptor descriptor{.name = "fixture.root"};
    using RemoteData = solar::remote::ContributeData<Telemetry>;
    using RemoteActions = solar::remote::ContributeActions<Scale, InlinePing, AsyncScale>;
    using RemoteLinks = solar::remote::ContributeLinks<TestLink>;
};

using System =
    solar::System<solar::Blueprint<solar::Facilities<Root>, solar::Executors<ControlQueue>>>;
using Service = typename System::RemoteService;
using Facility = typename System::RemoteFacility;
using LinkState = solar::remote::detail::LinkState<Service, TestLink, 0>;
using ScaleRegistration = Service::ActionRegistration<Scale>;
using TelemetryRegistration = Service::DataRegistration<Telemetry>;

} // namespace fixture

template <> struct solar::remote::Schema<fixture::Sample>
{
    static constexpr SchemaDescriptor descriptor{
        .id = TypeId{0x7201},
        .name = "fixture.Sample",
    };
    using Fields =
        remote::Fields<Field<1, &fixture::Sample::sequence>, Field<2, &fixture::Sample::value>>;
    static constexpr std::size_t max_encoded_size = 24;
    static constexpr Codec codec = Codec::Cbor;
};

template <> struct solar::remote::Schema<fixture::ScaleRequest>
{
    static constexpr SchemaDescriptor descriptor{
        .id = TypeId{0x7401},
        .name = "fixture.ScaleRequest",
    };
    using Fields = remote::Fields<Field<1, &fixture::ScaleRequest::value>>;
    static constexpr std::size_t max_encoded_size = 12;
    static constexpr Codec codec = Codec::Cbor;
};

template <> struct solar::remote::Schema<fixture::ScaleResponse>
{
    static constexpr SchemaDescriptor descriptor{
        .id = TypeId{0x7402},
        .name = "fixture.ScaleResponse",
    };
    using Fields = remote::Fields<Field<1, &fixture::ScaleResponse::value>>;
    static constexpr std::size_t max_encoded_size = 12;
    static constexpr Codec codec = Codec::Cbor;
};

SOLAR_BIND_SYSTEM(fixture::System);

static_assert(solar::contains_v<fixture::Facility, typename fixture::System::Components>);
static_assert(solar::contains_v<fixture::Service, typename fixture::System::Components>);
static_assert(std::is_same_v<typename fixture::System::graph::Category<fixture::Facility>,
                             solar::category::Facility>);
static_assert(std::is_same_v<typename fixture::System::graph::Category<fixture::Service>,
                             solar::category::Service>);

ZTEST(remote_runtime, test_generated_service_and_fragmented_rx)
{
    auto boot = fixture::System::boot();
    zassert_true(boot.has_value());
    auto action_registration = solar::execution::registration<fixture::ScaleRegistration>();
    zassert_true(action_registration.has_value());
    zassert_equal(action_registration->target_kind, solar::execution::TargetKind::OwnedWorkQueue);
    auto data_registration = solar::execution::registration<fixture::TelemetryRegistration>();
    zassert_true(data_registration.has_value());
    zassert_equal(data_registration->target_kind, solar::execution::TargetKind::OwnedWorkQueue);
    zassert_true(fixture::Facility::ready.load());
    zassert_true(fixture::TestLink::opened());
    zassert_false(solar::remote::interested<fixture::Telemetry>());

    auto unwatched =
        solar::remote::write<fixture::Telemetry>(fixture::Sample{.sequence = 1, .value = 0.25F});
    zassert_true(unwatched.has_value());
    zassert_equal(unwatched->disposition, solar::remote::WriteDisposition::NoSubscribers);
    zassert_false(unwatched->wake_queued);

    zassert_true(fixture::TestLink::connect().has_value());

    std::array<std::byte, 256> host_rx{};
    solar::Result<std::size_t, solar::remote::LinkError> server_hello =
        solar::fail<solar::remote::LinkError>({.status = solar::Status::Empty});
    for (int attempt = 0; attempt < 100 && !server_hello; ++attempt) {
        server_hello = fixture::TestLink::take_transmitted(host_rx);
        if (!server_hello) {
            zassert_equal(server_hello.error().status, solar::Status::Empty);
            k_sleep(K_MSEC(1));
        }
    }
    zassert_true(server_hello.has_value());
    std::array<std::byte, 128> host_scratch{};
    auto decoded_hello =
        solar::remote::frame::decode(std::span{host_rx}.first(*server_hello), host_scratch);
    zassert_true(decoded_hello.has_value());
    zassert_equal(decoded_hello->envelope.kind, solar::remote::protocol::Kind::ServerHello);

    for (int attempt = 0; attempt < 100 && fixture::LinkState::tx_in_flight.load(); ++attempt) {
        k_sleep(K_MSEC(1));
    }

    solar::remote::protocol::Envelope envelope{
        .kind = solar::remote::protocol::Kind::ClientHello,
        .session_epoch = 0,
        .frame_sequence = 1,
        .fragment_count = 1,
    };
    constexpr auto payload = fixture::Service::hello_payload();
    std::array<std::byte, 128> scratch{};
    std::array<std::byte, 160> encoded{};
    auto size = solar::remote::frame::encode(envelope, payload, scratch, encoded);
    zassert_true(size.has_value());

    const auto middle = *size / 2;
    zassert_true(fixture::TestLink::inject(std::span{encoded}.first(middle)).has_value());
    bool second_accepted{};
    for (int attempt = 0; attempt < 50 && !second_accepted; ++attempt) {
        auto second = fixture::TestLink::inject(std::span{encoded}.subspan(middle, *size - middle));
        if (second) {
            second_accepted = true;
            break;
        }
        zassert_equal(second.error().status, solar::Status::Busy);
        k_sleep(K_MSEC(1));
    }
    zassert_true(second_accepted);

    for (int attempt = 0;
         attempt < 100 && fixture::LinkState::session.load() != solar::remote::SessionState::Active;
         ++attempt) {
        k_sleep(K_MSEC(1));
    }
    zassert_equal(fixture::LinkState::accepted_frames.load(), 1);
    zassert_true(fixture::LinkState::connected.load());
    zassert_equal(fixture::LinkState::session.load(), solar::remote::SessionState::Active);

    solar::Result<std::size_t, solar::remote::LinkError> hello_response =
        solar::fail<solar::remote::LinkError>({.status = solar::Status::Empty});
    for (int attempt = 0; attempt < 100 && !hello_response; ++attempt) {
        hello_response = fixture::TestLink::take_transmitted(host_rx);
        if (!hello_response) {
            k_sleep(K_MSEC(1));
        }
    }
    zassert_true(hello_response.has_value());
    auto decoded_response =
        solar::remote::frame::decode(std::span{host_rx}.first(*hello_response), host_scratch);
    zassert_true(decoded_response.has_value());
    zassert_equal(decoded_response->envelope.kind, solar::remote::protocol::Kind::ServerHello);
    zassert_equal(decoded_response->envelope.session_epoch, 1);

    for (int attempt = 0; attempt < 100 && fixture::LinkState::tx_in_flight.load(); ++attempt) {
        k_sleep(K_MSEC(1));
    }
    solar::remote::protocol::Envelope subscribe_envelope{
        .kind = solar::remote::protocol::Kind::Subscribe,
        .session_epoch = 1,
        .frame_sequence = 2,
        .target = fixture::Telemetry::descriptor.id.value,
        .request_id = 1,
        .fragment_count = 1,
    };
    constexpr solar::remote::protocol::SubscriptionRequest subscribe_request{
        .minimum_interval_us = 100'000,
        .batch_size = 4,
    };
    constexpr auto subscribe_payload = solar::remote::protocol::encode(subscribe_request);
    std::array<std::byte, 160> subscribe_encoded{};
    auto subscribe_size = solar::remote::frame::encode(subscribe_envelope, subscribe_payload,
                                                       scratch, subscribe_encoded);
    zassert_true(subscribe_size.has_value());
    zassert_true(
        fixture::TestLink::inject(std::span{subscribe_encoded}.first(*subscribe_size)).has_value());
    solar::Result<std::size_t, solar::remote::LinkError> subscribe_response =
        solar::fail<solar::remote::LinkError>({.status = solar::Status::Empty});
    for (int attempt = 0; attempt < 100 && !subscribe_response; ++attempt) {
        subscribe_response = fixture::TestLink::take_transmitted(host_rx);
        if (!subscribe_response) {
            k_sleep(K_MSEC(1));
        }
    }
    zassert_true(subscribe_response.has_value());
    auto decoded_subscribe =
        solar::remote::frame::decode(std::span{host_rx}.first(*subscribe_response), host_scratch);
    zassert_true(decoded_subscribe.has_value());
    zassert_equal(decoded_subscribe->envelope.kind, solar::remote::protocol::Kind::Response);
    zassert_equal(decoded_subscribe->envelope.request_id, 1);
    auto effective_subscription =
        solar::remote::protocol::decode_subscription_policy(decoded_subscribe->payload);
    zassert_true(effective_subscription.has_value());
    zassert_equal(effective_subscription->minimum_interval_us, 100'000);
    zassert_equal(effective_subscription->batch_size, 1);
    zassert_equal(effective_subscription->codec, solar::remote::Codec::Cbor);

    for (int attempt = 0; attempt < 100 && fixture::LinkState::tx_in_flight.load(); ++attempt) {
        k_sleep(K_MSEC(1));
    }
    solar::remote::protocol::Envelope subscribe_ack{
        .kind = solar::remote::protocol::Kind::ResponseAck,
        .session_epoch = 1,
        .frame_sequence = 3,
        .target = fixture::Telemetry::descriptor.id.value,
        .request_id = 1,
        .fragment_count = 1,
    };
    std::array<std::byte, 160> subscribe_ack_encoded{};
    auto subscribe_ack_size =
        solar::remote::frame::encode(subscribe_ack, {}, scratch, subscribe_ack_encoded);
    zassert_true(subscribe_ack_size.has_value());
    zassert_true(
        fixture::TestLink::inject(std::span{subscribe_ack_encoded}.first(*subscribe_ack_size))
            .has_value());
    for (int attempt = 0;
         attempt < 100 && fixture::Service::template response_cached<fixture::TestLink, 0>(1);
         ++attempt) {
        k_sleep(K_MSEC(1));
    }
    const bool subscribe_still_cached =
        fixture::Service::template response_cached<fixture::TestLink, 0>(1);
    zassert_false(subscribe_still_cached);

    const fixture::Sample update_value{.sequence = 100, .value = 3.5F};
    std::array<std::byte, 24> update_payload{};
    auto update_payload_size = solar::remote::cbor::encode(update_value, update_payload);
    zassert_true(update_payload_size.has_value());
    solar::remote::protocol::Envelope update_envelope{
        .kind = solar::remote::protocol::Kind::Request,
        .session_epoch = 1,
        .frame_sequence = 6,
        .target = fixture::Telemetry::descriptor.id.value,
        .request_id = 2,
        .fragment_count = 1,
    };
    update_envelope.set_operation(solar::remote::protocol::OperationKind::Update);
    std::array<std::byte, 160> update_encoded{};
    auto update_size = solar::remote::frame::encode(
        update_envelope, std::span{update_payload}.first(*update_payload_size), scratch,
        update_encoded);
    zassert_true(update_size.has_value());
    zassert_true(
        fixture::TestLink::inject(std::span{update_encoded}.first(*update_size)).has_value());
    solar::Result<std::size_t, solar::remote::LinkError> update_response =
        solar::fail<solar::remote::LinkError>({.status = solar::Status::Empty});
    for (int attempt = 0; attempt < 100 && !update_response; ++attempt) {
        update_response = fixture::TestLink::take_transmitted(host_rx);
        if (!update_response) {
            k_sleep(K_MSEC(1));
        }
    }
    zassert_true(update_response.has_value());
    auto decoded_update =
        solar::remote::frame::decode(std::span{host_rx}.first(*update_response), host_scratch);
    zassert_true(decoded_update.has_value());
    zassert_equal(decoded_update->envelope.kind, solar::remote::protocol::Kind::Response);
    zassert_equal(fixture::Telemetry::updated, update_value);

    for (int attempt = 0; attempt < 100 && fixture::LinkState::tx_in_flight.load(); ++attempt) {
        k_sleep(K_MSEC(1));
    }
    solar::remote::protocol::Envelope update_ack{
        .kind = solar::remote::protocol::Kind::ResponseAck,
        .session_epoch = 1,
        .frame_sequence = 7,
        .target = fixture::Telemetry::descriptor.id.value,
        .request_id = 2,
        .fragment_count = 1,
    };
    std::array<std::byte, 160> update_ack_encoded{};
    auto update_ack_size =
        solar::remote::frame::encode(update_ack, {}, scratch, update_ack_encoded);
    zassert_true(update_ack_size.has_value());
    zassert_true(fixture::TestLink::inject(std::span{update_ack_encoded}.first(*update_ack_size))
                     .has_value());
    for (int attempt = 0;
         attempt < 100 && fixture::Service::template response_cached<fixture::TestLink, 0>(2);
         ++attempt) {
        k_sleep(K_MSEC(1));
    }
    const bool update_still_cached =
        fixture::Service::template response_cached<fixture::TestLink, 0>(2);
    zassert_false(update_still_cached);
    zassert_true(solar::remote::interested<fixture::Telemetry>());

    auto wrong_context = solar::remote::write_from_isr<fixture::Telemetry>(
        fixture::Sample{.sequence = 9, .value = 0.75F});
    zassert_false(wrong_context.has_value());
    zassert_equal(wrong_context.error().reason, solar::remote::Reason::InvalidContext);

    static std::atomic<solar::Status> isr_write_status{solar::Status::Error};
    irq_offload(
        [](const void*) {
            auto result = solar::remote::write_from_isr<fixture::Telemetry>(
                fixture::Sample{.sequence = 10, .value = 1.0F});
            isr_write_status.store(result ? solar::Status::Ok : result.error().status,
                                   std::memory_order_release);
        },
        nullptr);
    zassert_equal(isr_write_status.load(std::memory_order_acquire), solar::Status::Ok);
    solar::Result<std::size_t, solar::remote::LinkError> isr_frame =
        solar::fail<solar::remote::LinkError>({.status = solar::Status::Empty});
    for (int attempt = 0; attempt < 100 && !isr_frame; ++attempt) {
        isr_frame = fixture::TestLink::take_transmitted(host_rx);
        if (!isr_frame) {
            k_sleep(K_MSEC(1));
        }
    }
    zassert_true(isr_frame.has_value());
    auto decoded_isr =
        solar::remote::frame::decode(std::span{host_rx}.first(*isr_frame), host_scratch);
    zassert_true(decoded_isr.has_value());
    zassert_equal(decoded_isr->envelope.kind, solar::remote::protocol::Kind::Data);

    const fixture::Sample sample{.sequence = 42, .value = 1.5F};
    auto rate_skipped = solar::remote::write<fixture::Telemetry>(sample);
    zassert_true(rate_skipped.has_value());
    zassert_equal(rate_skipped->sequence, 3);
    for (int attempt = 0; attempt < 100; ++attempt) {
        bool skipped{};
        {
            auto guard = fixture::LinkState::output_lock.acquire();
            skipped = fixture::LinkState::subscriptions[0].skipped != 0;
        }
        if (skipped) {
            break;
        }
        k_sleep(K_MSEC(1));
    }
    {
        auto guard = fixture::LinkState::output_lock.acquire();
        zassert_equal(fixture::LinkState::subscriptions[0].skipped, 1);
    }

    k_sleep(K_MSEC(250));
    auto write = solar::remote::write<fixture::Telemetry>(sample);
    zassert_true(write.has_value());
    zassert_equal(write->disposition, solar::remote::WriteDisposition::Accepted);
    zassert_equal(write->sequence, 4);
    zassert_true(write->wake_queued);

    solar::Result<std::size_t, solar::remote::LinkError> data_frame =
        solar::fail<solar::remote::LinkError>({.status = solar::Status::Empty});
    for (int attempt = 0; attempt < 100 && !data_frame; ++attempt) {
        data_frame = fixture::TestLink::take_transmitted(host_rx);
        if (!data_frame) {
            k_sleep(K_MSEC(1));
        }
    }
    {
        auto guard = fixture::LinkState::output_lock.acquire();
        zassert_equal(fixture::LinkState::subscriptions[0].delivered, 2,
                      "delivered=%u skipped=%u next=%lld now=%lld",
                      fixture::LinkState::subscriptions[0].delivered,
                      fixture::LinkState::subscriptions[0].skipped,
                      fixture::LinkState::subscriptions[0].next_delivery,
                      solar::kernel::now_ticks());
        zassert_equal(fixture::LinkState::subscriptions[0].dropped, 0);
    }
    zassert_true(data_frame.has_value());
    auto decoded_data =
        solar::remote::frame::decode(std::span{host_rx}.first(*data_frame), host_scratch);
    zassert_true(decoded_data.has_value());
    zassert_equal(decoded_data->envelope.kind, solar::remote::protocol::Kind::Data);
    zassert_equal(decoded_data->envelope.target, fixture::Telemetry::descriptor.id.value);
    auto remote_sample = solar::remote::cbor::decode<fixture::Sample>(decoded_data->payload);
    zassert_true(remote_sample.has_value());
    zassert_equal(*remote_sample, sample);

    for (int attempt = 0; attempt < 100 && fixture::LinkState::tx_in_flight.load(); ++attempt) {
        k_sleep(K_MSEC(1));
    }
    solar::remote::protocol::Envelope query_envelope{
        .kind = solar::remote::protocol::Kind::Request,
        .session_epoch = 1,
        .frame_sequence = 4,
        .target = fixture::Telemetry::descriptor.id.value,
        .request_id = 3,
        .fragment_count = 1,
    };
    query_envelope.set_operation(solar::remote::protocol::OperationKind::Query);
    std::array<std::byte, 160> query_encoded{};
    auto query_size = solar::remote::frame::encode(query_envelope, {}, scratch, query_encoded);
    zassert_true(query_size.has_value());
    zassert_true(
        fixture::TestLink::inject(std::span{query_encoded}.first(*query_size)).has_value());
    solar::Result<std::size_t, solar::remote::LinkError> query_response =
        solar::fail<solar::remote::LinkError>({.status = solar::Status::Empty});
    for (int attempt = 0; attempt < 100 && !query_response; ++attempt) {
        query_response = fixture::TestLink::take_transmitted(host_rx);
        if (!query_response) {
            k_sleep(K_MSEC(1));
        }
    }
    zassert_true(query_response.has_value());
    auto decoded_query =
        solar::remote::frame::decode(std::span{host_rx}.first(*query_response), host_scratch);
    zassert_true(decoded_query.has_value());
    zassert_equal(decoded_query->envelope.kind, solar::remote::protocol::Kind::Response);
    auto queried_sample = solar::remote::cbor::decode<fixture::Sample>(decoded_query->payload);
    zassert_true(queried_sample.has_value());
    const fixture::Sample expected_query{.sequence = 99, .value = 2.5F};
    zassert_equal(*queried_sample, expected_query);
    zassert_equal(fixture::Telemetry::queries.load(), 1);

    for (int attempt = 0; attempt < 100 && fixture::LinkState::tx_in_flight.load(); ++attempt) {
        k_sleep(K_MSEC(1));
    }
    solar::remote::protocol::Envelope query_ack{
        .kind = solar::remote::protocol::Kind::ResponseAck,
        .session_epoch = 1,
        .frame_sequence = 5,
        .target = fixture::Telemetry::descriptor.id.value,
        .request_id = 3,
        .fragment_count = 1,
    };
    std::array<std::byte, 160> query_ack_encoded{};
    auto query_ack_size = solar::remote::frame::encode(query_ack, {}, scratch, query_ack_encoded);
    zassert_true(query_ack_size.has_value());
    zassert_true(
        fixture::TestLink::inject(std::span{query_ack_encoded}.first(*query_ack_size)).has_value());
    for (int attempt = 0;
         attempt < 100 && fixture::Service::template response_cached<fixture::TestLink, 0>(3);
         ++attempt) {
        k_sleep(K_MSEC(1));
    }
    const bool query_still_cached =
        fixture::Service::template response_cached<fixture::TestLink, 0>(3);
    zassert_false(query_still_cached);

    static k_sem release_blocker;
    static std::atomic_bool blocker_started{};
    static solar::kernel::Work blocker{[](solar::kernel::Work&) noexcept {
        blocker_started.store(true, std::memory_order_release);
        (void)k_sem_take(&release_blocker, K_FOREVER);
    }};
    k_sem_init(&release_blocker, 0, 1);
    auto& control_queue =
        solar::execution::detail::executor_state<fixture::System, fixture::ControlQueue>().queue;
    zassert_true(blocker.submit(control_queue).has_value());
    for (int attempt = 0; attempt < 100 && !blocker_started.load(std::memory_order_acquire);
         ++attempt) {
        k_sleep(K_MSEC(1));
    }
    zassert_true(blocker_started.load(std::memory_order_acquire));

    const fixture::ScaleRequest cancelled_request{.value = 11};
    std::array<std::byte, 16> cancelled_payload{};
    auto cancelled_payload_size = solar::remote::cbor::encode(cancelled_request, cancelled_payload);
    zassert_true(cancelled_payload_size.has_value());
    solar::remote::protocol::Envelope cancelled_envelope{
        .kind = solar::remote::protocol::Kind::Request,
        .session_epoch = 1,
        .frame_sequence = 8,
        .target = fixture::Scale::descriptor.id.value,
        .request_id = 4,
        .fragment_count = 1,
    };
    std::array<std::byte, 160> cancelled_encoded{};
    auto cancelled_size = solar::remote::frame::encode(
        cancelled_envelope, std::span{cancelled_payload}.first(*cancelled_payload_size), scratch,
        cancelled_encoded);
    zassert_true(cancelled_size.has_value());
    zassert_true(
        fixture::TestLink::inject(std::span{cancelled_encoded}.first(*cancelled_size)).has_value());

    solar::remote::protocol::Envelope cancel_envelope{
        .kind = solar::remote::protocol::Kind::Cancel,
        .session_epoch = 1,
        .frame_sequence = 9,
        .target = fixture::Scale::descriptor.id.value,
        .request_id = 4,
        .fragment_count = 1,
    };
    std::array<std::byte, 160> cancel_encoded{};
    auto cancel_size = solar::remote::frame::encode(cancel_envelope, {}, scratch, cancel_encoded);
    zassert_true(cancel_size.has_value());
    bool cancel_injected{};
    for (int attempt = 0; attempt < 100 && !cancel_injected; ++attempt) {
        auto injected = fixture::TestLink::inject(std::span{cancel_encoded}.first(*cancel_size));
        if (injected) {
            cancel_injected = true;
        } else {
            zassert_equal(injected.error().status, solar::Status::Busy);
            k_sleep(K_MSEC(1));
        }
    }
    zassert_true(cancel_injected);
    solar::Result<std::size_t, solar::remote::LinkError> cancel_response =
        solar::fail<solar::remote::LinkError>({.status = solar::Status::Empty});
    for (int attempt = 0; attempt < 100 && !cancel_response; ++attempt) {
        cancel_response = fixture::TestLink::take_transmitted(host_rx);
        if (!cancel_response) {
            k_sleep(K_MSEC(1));
        }
    }
    zassert_true(cancel_response.has_value());
    auto decoded_cancel =
        solar::remote::frame::decode(std::span{host_rx}.first(*cancel_response), host_scratch);
    zassert_true(decoded_cancel.has_value());
    zassert_equal(decoded_cancel->envelope.kind, solar::remote::protocol::Kind::Error);
    zassert_equal(solar::remote::protocol::detail::get_u16(decoded_cancel->payload, 0),
                  static_cast<std::uint16_t>(solar::remote::protocol::ErrorCode::Cancelled));
    zassert_equal(fixture::Scale::calls.load(), 0);
    k_sem_give(&release_blocker);
    zassert_true(blocker.flush().has_value());

    for (int attempt = 0; attempt < 100 && fixture::LinkState::tx_in_flight.load(); ++attempt) {
        k_sleep(K_MSEC(1));
    }
    const fixture::ScaleRequest action_request{.value = 21};
    std::array<std::byte, 16> request_payload{};
    auto request_size = solar::remote::cbor::encode(action_request, request_payload);
    zassert_true(request_size.has_value());
    solar::remote::protocol::Envelope request_envelope{
        .kind = solar::remote::protocol::Kind::Request,
        .session_epoch = 1,
        .frame_sequence = 2,
        .target = fixture::Scale::descriptor.id.value,
        .request_id = 7,
        .fragment_count = 1,
    };
    auto action_frame_size = solar::remote::frame::encode(
        request_envelope, std::span{request_payload}.first(*request_size), scratch, encoded);
    zassert_true(action_frame_size.has_value());
    std::array<std::byte, 160> action_encoded{};
    std::copy_n(encoded.begin(), *action_frame_size, action_encoded.begin());
    zassert_true(
        fixture::TestLink::inject(std::span{action_encoded}.first(*action_frame_size)).has_value());

    solar::Result<std::size_t, solar::remote::LinkError> action_frame =
        solar::fail<solar::remote::LinkError>({.status = solar::Status::Empty});
    for (int attempt = 0; attempt < 100 && !action_frame; ++attempt) {
        action_frame = fixture::TestLink::take_transmitted(host_rx);
        if (!action_frame) {
            k_sleep(K_MSEC(1));
        }
    }
    zassert_true(action_frame.has_value());
    auto decoded_action =
        solar::remote::frame::decode(std::span{host_rx}.first(*action_frame), host_scratch);
    zassert_true(decoded_action.has_value());
    zassert_equal(decoded_action->envelope.kind, solar::remote::protocol::Kind::Response);
    zassert_equal(decoded_action->envelope.request_id, 7);
    auto action_response =
        solar::remote::cbor::decode<fixture::ScaleResponse>(decoded_action->payload);
    zassert_true(action_response.has_value());
    zassert_equal(*action_response, fixture::ScaleResponse{.value = 42});
    zassert_equal(fixture::Scale::calls.load(), 1);

    for (int attempt = 0; attempt < 100 && fixture::LinkState::tx_in_flight.load(); ++attempt) {
        k_sleep(K_MSEC(1));
    }
    zassert_true(
        fixture::TestLink::inject(std::span{action_encoded}.first(*action_frame_size)).has_value());
    solar::Result<std::size_t, solar::remote::LinkError> replay =
        solar::fail<solar::remote::LinkError>({.status = solar::Status::Empty});
    for (int attempt = 0; attempt < 100 && !replay; ++attempt) {
        replay = fixture::TestLink::take_transmitted(host_rx);
        if (!replay) {
            k_sleep(K_MSEC(1));
        }
    }
    zassert_true(replay.has_value());
    zassert_equal(fixture::Scale::calls.load(), 1);

    for (int attempt = 0; attempt < 100 && fixture::LinkState::tx_in_flight.load(); ++attempt) {
        k_sleep(K_MSEC(1));
    }
    solar::remote::protocol::Envelope ack_envelope{
        .kind = solar::remote::protocol::Kind::ResponseAck,
        .session_epoch = 1,
        .frame_sequence = 3,
        .target = fixture::Scale::descriptor.id.value,
        .request_id = 7,
        .fragment_count = 1,
    };
    std::array<std::byte, 160> ack_encoded{};
    auto ack_size = solar::remote::frame::encode(ack_envelope, {}, scratch, ack_encoded);
    zassert_true(ack_size.has_value());
    zassert_true(fixture::TestLink::inject(std::span{ack_encoded}.first(*ack_size)).has_value());
    for (int attempt = 0;
         attempt < 100 && fixture::Service::template response_cached<fixture::TestLink, 0>(7);
         ++attempt) {
        k_sleep(K_MSEC(1));
    }
    const bool action_still_cached =
        fixture::Service::template response_cached<fixture::TestLink, 0>(7);
    zassert_false(action_still_cached);

    zassert_true(
        fixture::TestLink::inject(std::span{action_encoded}.first(*action_frame_size)).has_value());
    solar::Result<std::size_t, solar::remote::LinkError> expired =
        solar::fail<solar::remote::LinkError>({.status = solar::Status::Empty});
    for (int attempt = 0; attempt < 100 && !expired; ++attempt) {
        expired = fixture::TestLink::take_transmitted(host_rx);
        if (!expired) {
            k_sleep(K_MSEC(1));
        }
    }
    zassert_true(expired.has_value());
    auto decoded_expired =
        solar::remote::frame::decode(std::span{host_rx}.first(*expired), host_scratch);
    zassert_true(decoded_expired.has_value());
    zassert_equal(decoded_expired->envelope.kind, solar::remote::protocol::Kind::Error);
    zassert_equal(
        solar::remote::protocol::detail::get_u16(decoded_expired->payload, 0),
        static_cast<std::uint16_t>(solar::remote::protocol::ErrorCode::DuplicateResponseExpired));
    zassert_equal(fixture::Scale::calls.load(), 1);

    for (int attempt = 0; attempt < 100 && fixture::LinkState::tx_in_flight.load(); ++attempt) {
        k_sleep(K_MSEC(1));
    }
    solar::remote::protocol::Envelope ping_envelope{
        .kind = solar::remote::protocol::Kind::Request,
        .session_epoch = 1,
        .frame_sequence = 4,
        .target = fixture::InlinePing::descriptor.id.value,
        .request_id = 8,
        .fragment_count = 1,
    };
    std::array<std::byte, 4> ping_payload{};
    auto ping_payload_size = solar::remote::cbor::encode(solar::remote::Empty{}, ping_payload);
    zassert_true(ping_payload_size.has_value());
    std::array<std::byte, 160> ping_encoded{};
    auto ping_size = solar::remote::frame::encode(
        ping_envelope, std::span{ping_payload}.first(*ping_payload_size), scratch, ping_encoded);
    zassert_true(ping_size.has_value());
    zassert_true(fixture::TestLink::inject(std::span{ping_encoded}.first(*ping_size)).has_value());
    solar::Result<std::size_t, solar::remote::LinkError> ping_response =
        solar::fail<solar::remote::LinkError>({.status = solar::Status::Empty});
    for (int attempt = 0; attempt < 100 && !ping_response; ++attempt) {
        ping_response = fixture::TestLink::take_transmitted(host_rx);
        if (!ping_response) {
            k_sleep(K_MSEC(1));
        }
    }
    zassert_true(ping_response.has_value());
    auto decoded_ping =
        solar::remote::frame::decode(std::span{host_rx}.first(*ping_response), host_scratch);
    zassert_true(decoded_ping.has_value());
    zassert_equal(decoded_ping->envelope.kind, solar::remote::protocol::Kind::Response);
    zassert_equal(decoded_ping->envelope.request_id, 8);
    zassert_equal(fixture::InlinePing::calls.load(), 1);

    for (int attempt = 0; attempt < 100 && fixture::LinkState::tx_in_flight.load(); ++attempt) {
        k_sleep(K_MSEC(1));
    }
    const fixture::ScaleRequest async_request{.value = 14};
    std::array<std::byte, 16> async_payload{};
    auto async_payload_size = solar::remote::cbor::encode(async_request, async_payload);
    zassert_true(async_payload_size.has_value());
    solar::remote::protocol::Envelope async_envelope{
        .kind = solar::remote::protocol::Kind::Request,
        .session_epoch = 1,
        .frame_sequence = 18,
        .target = fixture::AsyncScale::descriptor.id.value,
        .request_id = 9,
        .fragment_count = 1,
    };
    std::array<std::byte, 160> async_encoded{};
    auto async_size = solar::remote::frame::encode(
        async_envelope, std::span{async_payload}.first(*async_payload_size), scratch,
        async_encoded);
    zassert_true(async_size.has_value());
    zassert_true(
        fixture::TestLink::inject(std::span{async_encoded}.first(*async_size)).has_value());
    for (int attempt = 0; attempt < 100 && !fixture::AsyncScale::pending; ++attempt) {
        k_sleep(K_MSEC(1));
    }
    zassert_true(fixture::AsyncScale::pending.has_value());
    zassert_equal(fixture::AsyncScale::request_value.load(std::memory_order_acquire), 14);
    zassert_false(fixture::AsyncScale::pending->cancelled());
    zassert_true(fixture::AsyncScale::pending->complete({.value = 42}).has_value());
    auto duplicate_completion = fixture::AsyncScale::pending->complete({.value = 84});
    zassert_false(duplicate_completion.has_value());
    zassert_equal(solar::status_of(duplicate_completion.error()), solar::Status::NotReady);
    fixture::AsyncScale::pending.reset();

    solar::Result<std::size_t, solar::remote::LinkError> async_response =
        solar::fail<solar::remote::LinkError>({.status = solar::Status::Empty});
    for (int attempt = 0; attempt < 100 && !async_response; ++attempt) {
        async_response = fixture::TestLink::take_transmitted(host_rx);
        if (!async_response) {
            k_sleep(K_MSEC(1));
        }
    }
    zassert_true(async_response.has_value());
    auto decoded_async =
        solar::remote::frame::decode(std::span{host_rx}.first(*async_response), host_scratch);
    zassert_true(decoded_async.has_value());
    zassert_equal(decoded_async->envelope.kind, solar::remote::protocol::Kind::Response);
    zassert_equal(decoded_async->envelope.request_id, 9);
    auto async_value = solar::remote::cbor::decode<fixture::ScaleResponse>(decoded_async->payload);
    zassert_true(async_value.has_value());
    zassert_equal(*async_value, fixture::ScaleResponse{.value = 42});

    for (int attempt = 0; attempt < 100 && fixture::LinkState::tx_in_flight.load(); ++attempt) {
        k_sleep(K_MSEC(1));
    }
    async_envelope.frame_sequence = 19;
    async_envelope.request_id = 10;
    async_size = solar::remote::frame::encode(async_envelope,
                                              std::span{async_payload}.first(*async_payload_size),
                                              scratch, async_encoded);
    zassert_true(async_size.has_value());
    zassert_true(
        fixture::TestLink::inject(std::span{async_encoded}.first(*async_size)).has_value());
    for (int attempt = 0; attempt < 100 && !fixture::AsyncScale::pending; ++attempt) {
        k_sleep(K_MSEC(1));
    }
    zassert_true(fixture::AsyncScale::pending.has_value());
    solar::remote::protocol::Envelope async_cancel{
        .kind = solar::remote::protocol::Kind::Cancel,
        .session_epoch = 1,
        .frame_sequence = 20,
        .target = fixture::AsyncScale::descriptor.id.value,
        .request_id = 10,
        .fragment_count = 1,
    };
    std::array<std::byte, 160> async_cancel_encoded{};
    auto async_cancel_size =
        solar::remote::frame::encode(async_cancel, {}, scratch, async_cancel_encoded);
    zassert_true(async_cancel_size.has_value());
    zassert_true(
        fixture::TestLink::inject(std::span{async_cancel_encoded}.first(*async_cancel_size))
            .has_value());
    for (int attempt = 0; attempt < 100 && !fixture::AsyncScale::pending->cancelled(); ++attempt) {
        k_sleep(K_MSEC(1));
    }
    zassert_true(fixture::AsyncScale::pending->cancelled());
    auto late_completion = fixture::AsyncScale::pending->complete({.value = 99});
    zassert_false(late_completion.has_value());
    zassert_equal(solar::status_of(late_completion.error()), solar::Status::NotReady);
    fixture::AsyncScale::pending.reset();

    solar::Result<std::size_t, solar::remote::LinkError> async_cancel_response =
        solar::fail<solar::remote::LinkError>({.status = solar::Status::Empty});
    for (int attempt = 0; attempt < 100 && !async_cancel_response; ++attempt) {
        async_cancel_response = fixture::TestLink::take_transmitted(host_rx);
        if (!async_cancel_response) {
            k_sleep(K_MSEC(1));
        }
    }
    zassert_true(async_cancel_response.has_value());
    auto decoded_async_cancel = solar::remote::frame::decode(
        std::span{host_rx}.first(*async_cancel_response), host_scratch);
    zassert_true(decoded_async_cancel.has_value());
    zassert_equal(decoded_async_cancel->envelope.kind, solar::remote::protocol::Kind::Error);
    zassert_equal(solar::remote::protocol::detail::get_u16(decoded_async_cancel->payload, 0),
                  static_cast<std::uint16_t>(solar::remote::protocol::ErrorCode::Cancelled));

    solar::remote::protocol::Envelope introspection_envelope{
        .kind = solar::remote::protocol::Kind::Introspection,
        .session_epoch = 1,
        .frame_sequence = 21,
        .target = static_cast<std::uint32_t>(
            solar::remote::protocol::IntrospectionTarget::ProtocolSummary),
        .request_id = 11,
        .fragment_count = 1,
    };
    std::array<std::byte, 160> introspection_encoded{};
    auto introspection_size =
        solar::remote::frame::encode(introspection_envelope, {}, scratch, introspection_encoded);
    zassert_true(introspection_size.has_value());
    zassert_true(
        fixture::TestLink::inject(std::span{introspection_encoded}.first(*introspection_size))
            .has_value());
    solar::Result<std::size_t, solar::remote::LinkError> introspection_response =
        solar::fail<solar::remote::LinkError>({.status = solar::Status::Empty});
    for (int attempt = 0; attempt < 100 && !introspection_response; ++attempt) {
        introspection_response = fixture::TestLink::take_transmitted(host_rx);
        if (!introspection_response) {
            k_sleep(K_MSEC(1));
        }
    }
    zassert_true(introspection_response.has_value());
    auto decoded_introspection = solar::remote::frame::decode(
        std::span{host_rx}.first(*introspection_response), host_scratch);
    zassert_true(decoded_introspection.has_value());
#if defined(CONFIG_SOLAR_REMOTE_RUNTIME_INTROSPECTION)
    zassert_equal(decoded_introspection->envelope.kind,
                  solar::remote::protocol::Kind::Introspection);
    auto summary =
        solar::remote::protocol::decode_introspection_summary(decoded_introspection->payload);
    zassert_true(summary.has_value());
    zassert_equal(summary->schemas, 5);
    zassert_equal(summary->data, 1);
    zassert_equal(summary->actions, 3);
    zassert_equal(summary->topics, 0);
    zassert_equal(summary->streams, 0);
    zassert_equal(summary->links, 1);

#if defined(CONFIG_SOLAR_INSPECTION_REMOTE)
    introspection_envelope.frame_sequence = 22;
    introspection_envelope.request_id = 12;
    introspection_envelope.target =
        static_cast<std::uint32_t>(solar::remote::protocol::IntrospectionTarget::Collections);
    constexpr auto collection_request =
        solar::remote::protocol::encode(solar::remote::protocol::CollectionRequest{.limit = 2});
    introspection_size = solar::remote::frame::encode(introspection_envelope, collection_request,
                                                      scratch, introspection_encoded);
    zassert_true(introspection_size.has_value());
    zassert_true(
        fixture::TestLink::inject(std::span{introspection_encoded}.first(*introspection_size))
            .has_value());
    introspection_response =
        solar::fail<solar::remote::LinkError>({.status = solar::Status::Empty});
    for (int attempt = 0; attempt < 100 && !introspection_response; ++attempt) {
        introspection_response = fixture::TestLink::take_transmitted(host_rx);
        if (!introspection_response) {
            k_sleep(K_MSEC(1));
        }
    }
    zassert_true(introspection_response.has_value());
    decoded_introspection = solar::remote::frame::decode(
        std::span{host_rx}.first(*introspection_response), host_scratch);
    zassert_true(decoded_introspection.has_value());
    zassert_equal(decoded_introspection->envelope.kind,
                  solar::remote::protocol::Kind::Introspection);
    auto collection_page =
        solar::remote::protocol::decode_collection_page_header(decoded_introspection->payload);
    zassert_true(collection_page.has_value());
    zassert_equal(collection_page->count, 2);
    zassert_true(collection_page->has_more);
    zassert_equal(
        solar::remote::protocol::detail::get_u16(
            decoded_introspection->payload, solar::remote::protocol::collection_page_header_size),
        0);
    zassert_equal(solar::remote::protocol::detail::get_u32(
                      decoded_introspection->payload,
                      solar::remote::protocol::collection_page_header_size + 2),
                  solar::inspection::Components::descriptor.stable_id.value);

    introspection_envelope.frame_sequence = 23;
    introspection_envelope.request_id = 13;
    introspection_envelope.target =
        static_cast<std::uint32_t>(solar::remote::protocol::IntrospectionTarget::CollectionQuery);
    constexpr auto query_request =
        solar::remote::protocol::encode(solar::remote::protocol::CollectionQueryRequest{
            .stable_id = solar::inspection::LifecycleComponents::descriptor.stable_id.value,
            .limit = 1,
        });
    introspection_size = solar::remote::frame::encode(introspection_envelope, query_request,
                                                      scratch, introspection_encoded);
    zassert_true(introspection_size.has_value());
    zassert_true(
        fixture::TestLink::inject(std::span{introspection_encoded}.first(*introspection_size))
            .has_value());
    introspection_response =
        solar::fail<solar::remote::LinkError>({.status = solar::Status::Empty});
    for (int attempt = 0; attempt < 100 && !introspection_response; ++attempt) {
        introspection_response = fixture::TestLink::take_transmitted(host_rx);
        if (!introspection_response) {
            k_sleep(K_MSEC(1));
        }
    }
    zassert_true(introspection_response.has_value());
    decoded_introspection = solar::remote::frame::decode(
        std::span{host_rx}.first(*introspection_response), host_scratch);
    zassert_true(decoded_introspection.has_value());
    zassert_equal(decoded_introspection->envelope.kind,
                  solar::remote::protocol::Kind::Introspection);
    zassert_true(decoded_introspection->payload.size() > 12);
    zassert_equal(decoded_introspection->payload[0], std::byte{0xAA});
    zassert_equal(decoded_introspection->payload[1], std::byte{0x00});
    zassert_equal(decoded_introspection->payload[2], std::byte{0x1A});
    zassert_equal(decoded_introspection->payload[3], std::byte{0x6D});
    zassert_equal(decoded_introspection->payload[4], std::byte{0x9A});
    zassert_equal(decoded_introspection->payload[5], std::byte{0x00});
    zassert_equal(decoded_introspection->payload[6], std::byte{0x02});
#endif
#else
    zassert_equal(decoded_introspection->envelope.kind, solar::remote::protocol::Kind::Error);
#endif

    const auto service_record = fixture::Service::record();
    const auto link_record = fixture::Service::template link_record<fixture::TestLink, 0>();
    zassert_true(service_record.ready);
    zassert_equal(service_record.dropped_events, 0);
    zassert_true(service_record.event_high_water > 0);
    zassert_equal(link_record.session, solar::remote::SessionState::Active);
    zassert_equal(link_record.subscriptions, 1);
    zassert_true(link_record.received_frames >= 15);
    zassert_true(link_record.received_bytes > 0);
    zassert_true(link_record.transmitted_frames >= 13);
    zassert_true(link_record.transmitted_bytes > 0);
    zassert_equal(link_record.connections, 1);

    auto stop = fixture::System::stop();
    zassert_true(stop.has_value());
    zassert_false(fixture::TestLink::opened());
    zassert_false(solar::remote::interested<fixture::Telemetry>());
}

ZTEST_SUITE(remote_runtime, nullptr, nullptr, nullptr, nullptr, nullptr);
