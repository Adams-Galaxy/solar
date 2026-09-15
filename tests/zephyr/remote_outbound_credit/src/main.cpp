#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <solar/remote.hpp>
#include <solar/remote/runtime_context.hpp>
#include <solar/remote/testing/fake_dma_link.hpp>

namespace fixture
{

struct Sample
{
    std::uint32_t sequence{};
};

} // namespace fixture

template <> struct solar::remote::Schema<fixture::Sample>
{
    static constexpr SchemaDescriptor descriptor{
        .id = TypeId{0x9101},
        .name = "fixture.Sample",
    };
    using Fields = remote::Fields<Field<1, "sequence", &fixture::Sample::sequence>>;
    static constexpr std::size_t max_encoded_size = 16;
    static constexpr Codec codec = Codec::Cbor;
};

namespace fixture
{

struct Probe
{
    static constexpr solar::remote::DataDescriptor descriptor{
        .id = solar::remote::DataId{0x9001},
        .name = "fixture.probe",
        .description = "Reliable OutStream credit-accounting fixture",
    };
    using Value = Sample;
    using Capabilities =
        solar::remote::Capabilities<solar::remote::OutStream<solar::remote::Push,
                                                              solar::remote::ReliableWindow<3>>>;
};

struct Link : solar::remote::testing::FakeDmaLink<Link, 512, 512>
{
    static constexpr solar::remote::LinkDescriptor descriptor{
        .id = solar::remote::LinkId{0x8201},
        .name = "fixture.fake_dma",
    };
};

using Architecture =
    solar::remote::Architecture<solar::TypeList<>, solar::TypeList<Probe>, solar::TypeList<>,
                                solar::TypeList<>, solar::TypeList<>, solar::TypeList<Link>,
                                solar::TypeList<>, solar::TypeList<>>;
using Context = solar::remote::RuntimeContext<Architecture>;

struct Tag
{};

using Runtime = solar::remote::ByteRuntime<Tag, Architecture>;

using ServiceT = Context::RemoteService;
using State = solar::remote::detail::LinkState<ServiceT, Link, 0>;

constexpr auto slot = solar::remote::detail::data_stream_subscription_slot<Context, Probe>();

/// Directly establishes what a real Subscribe request would otherwise set up
/// (SubscriptionSlot.active plus PushState.interested_sessions, which gates
/// whether write_data even wakes the service to drain) -- this test exercises
/// the reliable-mode drain/credit machinery in isolation, without needing a
/// full protocol handshake.
void subscribe()
{
    {
        auto guard = State::output_lock.acquire();
        auto& subscription = State::subscriptions[slot];
        subscription.active = true;
        subscription.credits = 0;
        subscription.sent_current = false;
        subscription.stalled = false;
    }
    State::session.store(solar::remote::SessionState::Active, std::memory_order_release);
    solar::remote::detail::push_state<Context, Probe>().interested_sessions.fetch_add(
        1, std::memory_order_acq_rel);
}

void set_credits(std::uint16_t credits)
{
    auto guard = State::output_lock.acquire();
    State::subscriptions[slot].credits = credits;
}

void reset_state()
{
    {
        auto guard = State::output_lock.acquire();
        State::subscriptions[slot] = {};
    }
    auto& push = solar::remote::detail::push_state<Context, Probe>();
    auto guard = push.lock.acquire();
    push.values = {};
    push.head = 0;
    push.size = 0;
    push.sequence = 0;
    push.replaced = 0;
    push.wake_pending.store(false, std::memory_order_relaxed);
    push.interested_sessions.store(0, std::memory_order_relaxed);
}

bool subscription_active()
{
    auto guard = State::output_lock.acquire();
    return State::subscriptions[slot].active;
}

std::uint16_t current_credits()
{
    auto guard = State::output_lock.acquire();
    return State::subscriptions[slot].credits;
}

bool publish(std::uint32_t sequence)
{
    Sample value{.sequence = sequence};
    return solar::remote::detail::write_data<Context, Probe>(value).has_value();
}

/// Drains whatever the (fake) DMA hardware would have transmitted so far and
/// appends every accepted protocol::Kind byte it finds to `kinds`. Standing
/// in for real hardware completing a transfer -- FakeDmaLink never advances
/// on its own.
void pump_wire(std::array<std::uint8_t, 16>& kinds, std::size_t& kind_count, int spin_count = 64,
              std::int32_t sleep_ms = 10)
{
    for (int spins = 0; spins < spin_count; ++spins) {
        (void)Link::advance_tx(4096);
        std::array<std::byte, 512> wire{};
        auto taken = Link::take_transmitted(wire);
        if (!taken) {
            // The link only carries one in-flight frame at a time (same as
            // real hardware), and the service's own maintenance tick is what
            // moves the next queued message onto it -- give real wall-clock
            // time a chance to let that tick run instead of spinning inside
            // the same instant forever.
            k_sleep(K_MSEC(sleep_ms));
            continue;
        }
        std::array<std::byte, 512> scratch{};
        auto decoded = solar::remote::frame::decode(std::span{wire}.first(*taken), scratch);
        if (decoded) {
            printk("pump_wire: kind=%u target=0x%x request_id=%u payload_size=%u\n",
                  static_cast<unsigned>(decoded->envelope.kind), decoded->envelope.target,
                  decoded->envelope.request_id, decoded->envelope.payload_size);
            if (kind_count < kinds.size()) {
                kinds[kind_count++] = static_cast<std::uint8_t>(decoded->envelope.kind);
            }
        } else {
            printk("pump_wire: %zu bytes failed to decode as a frame\n", *taken);
        }
    }
}

/// Drains and discards the ServerHello (and any other) traffic the service
/// sends unsolicited as soon as a link connects -- unrelated to the credit
/// behavior under test here.
void discard_handshake_noise()
{
    std::array<std::uint8_t, 16> kinds{};
    std::size_t kind_count{};
    pump_wire(kinds, kind_count, /*spin_count=*/8, /*sleep_ms=*/10);
}

void after_each(void*)
{
    (void)Runtime::stop();
    (void)Runtime::deinitialize();
    Link::close();
    reset_state();
}

} // namespace fixture

ZTEST(remote_outbound_credit, test_zero_credit_blocks_and_grant_releases)
{
    using namespace fixture;

    zassert_true(Runtime::initialize().has_value());
    zassert_true(Runtime::start().has_value());
    zassert_true(Link::connect().has_value());
    k_sleep(K_MSEC(20));
    discard_handshake_noise();

    subscribe();

    zassert_true(publish(1));
    zassert_true(publish(2));
    zassert_true(publish(3));

    std::array<std::uint8_t, 16> kinds{};
    std::size_t kind_count{};
    // Deliberately short and well under the stall deadline -- this only
    // needs to prove nothing was transmitted promptly, not survive a full
    // abort cycle.
    pump_wire(kinds, kind_count, /*spin_count=*/5, /*sleep_ms=*/5);
    zassert_equal(kind_count, 0, "no credit means nothing should be transmitted yet");
    zassert_equal(current_credits(), 0);

    // Values 1-3 already exactly fill the ReliableWindow<3> retry buffer
    // (nothing has drained -- zero credit). A 4th publish now must Reject,
    // the same overflow behavior Queue<3, Reject> already has once its
    // buffer is genuinely full -- reliable mode reuses that, it does not
    // invent silent unbounded queuing.
    const bool fourth = publish(4);
    zassert_false(fourth, "a truly full reliable window must reject, not grow unbounded");

    set_credits(3);
    // Reliable-mode retry is driven by the ~50ms periodic maintenance sweep
    // (poll_reliable_out_streams), not an immediate wake -- pump_wire's own
    // internal sleeps between spins give it room to run.
    kind_count = 0;
    pump_wire(kinds, kind_count);
    zassert_true(kind_count >= 3, "granting credit must release the queued values");
    for (std::size_t index = 0; index < kind_count; ++index) {
        zassert_equal(kinds[index], static_cast<std::uint8_t>(solar::remote::protocol::Kind::Data));
    }
    zassert_equal(current_credits(), 0, "every credit spent should be reflected exactly");
    zassert_true(subscription_active());
}

ZTEST(remote_outbound_credit, test_stall_past_deadline_aborts_the_subscription)
{
    using namespace fixture;

    zassert_true(Runtime::initialize().has_value());
    zassert_true(Runtime::start().has_value());
    zassert_true(Link::connect().has_value());
    k_sleep(K_MSEC(20));
    discard_handshake_noise();

    subscribe();
    zassert_true(publish(42));

    std::array<std::uint8_t, 16> kinds{};
    std::size_t kind_count{};
    // Deliberately short and well under the stall deadline.
    pump_wire(kinds, kind_count, /*spin_count=*/5, /*sleep_ms=*/5);
    zassert_equal(kind_count, 0);
    zassert_true(subscription_active(), "must not abort before the stall deadline");

    // CONFIG_SOLAR_REMOTE_OUTBOUND_STALL_TIMEOUT_MS is overridden to 200 in
    // this test's prj.conf specifically so this wait stays short.
    kind_count = 0;
    pump_wire(kinds, kind_count, /*spin_count=*/64, /*sleep_ms=*/10);

    zassert_false(subscription_active(),
                  "a reliable OutStream stalled past its deadline must abort, not silently drop");
    bool saw_error{};
    for (std::size_t index = 0; index < kind_count; ++index) {
        if (kinds[index] == static_cast<std::uint8_t>(solar::remote::protocol::Kind::Error)) {
            saw_error = true;
        }
    }
    zassert_true(saw_error, "the station must be told the reliable stream broke, not left silent");
}

ZTEST_SUITE(remote_outbound_credit, nullptr, nullptr, nullptr, fixture::after_each, nullptr);
