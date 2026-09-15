#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <solar/remote.hpp>
#include <solar/remote/runtime_context.hpp>
#include <solar/remote/testing/fake_dma_link.hpp>

namespace fixture
{

struct StreamSample
{
    std::uint32_t sequence{};
};

} // namespace fixture

template <> struct solar::remote::Schema<fixture::StreamSample>
{
    static constexpr SchemaDescriptor descriptor{
        .id = TypeId{0x9102},
        .name = "fixture.StreamSample",
    };
    using Fields = remote::Fields<Field<1, "sequence", &fixture::StreamSample::sequence>>;
    static constexpr std::size_t max_encoded_size = 16;
    static constexpr Codec codec = Codec::Cbor;
};

namespace fixture
{

/// A `direction: out` Stream declared the same shape codegen now produces:
/// `Capabilities<OutStream<Push, ...>>` plus a `ContractType` self-link (in
/// real generated code this points back to the plain, non-Remote contract
/// type `PollDispatch::publish<...>()` is called with -- here the fixture's
/// own type doubles as both, matching how a hand-authored fixture stands in
/// for two-tier codegen elsewhere in this test suite).
struct QueuedStream
{
    static constexpr solar::remote::StreamDescriptor descriptor{
        .id = solar::remote::StreamId{0x9201},
        .name = "fixture.queued_stream",
        .description = "Best-effort poll-scheduled OutStream fixture",
    };
    using Value = StreamSample;
    static constexpr bool input = false;
    // Deliberately slow relative to the ~50ms maintenance tick, so a short
    // pump window can distinguish "rate limit honored" from "delivered every
    // tick regardless."
    static constexpr std::uint32_t maximum_rate_hz = 4;
    using Capabilities = solar::remote::Capabilities<
        solar::remote::OutStream<solar::remote::Push, solar::remote::Queue<4, solar::remote::DropOldest>>>;
    using ContractType = QueuedStream;
};

struct ReliableStream
{
    static constexpr solar::remote::StreamDescriptor descriptor{
        .id = solar::remote::StreamId{0x9202},
        .name = "fixture.reliable_stream",
        .description = "Reliable poll-scheduled OutStream fixture",
    };
    using Value = StreamSample;
    static constexpr bool input = false;
    static constexpr std::uint32_t maximum_rate_hz = 1000;
    using Capabilities = solar::remote::Capabilities<
        solar::remote::OutStream<solar::remote::Push, solar::remote::ReliableWindow<3>>>;
    using ContractType = ReliableStream;
};

/// A high-rate stream makes the service-deadline regression observable: it
/// must be polled near 200 Hz when subscribed, rather than being capped by
/// the old 50 ms maintenance timeout.
struct FastStream
{
    static constexpr solar::remote::StreamDescriptor descriptor{
        .id = solar::remote::StreamId{0x9203},
        .name = "fixture.fast_stream",
        .description = "High-rate poll-scheduled OutStream fixture",
    };
    using Value = StreamSample;
    static constexpr bool input = false;
    static constexpr std::uint32_t maximum_rate_hz = 200;
    using Capabilities = solar::remote::Capabilities<
        solar::remote::OutStream<solar::remote::Push, solar::remote::Queue<4, solar::remote::DropOldest>>>;
    using ContractType = FastStream;
};

struct Link : solar::remote::testing::FakeDmaLink<Link, 512, 512>
{
    static constexpr solar::remote::LinkDescriptor descriptor{
        .id = solar::remote::LinkId{0x8202},
        .name = "fixture.fake_dma_output_streams",
    };
};

template <typename StreamT> struct PollCounter
{
    inline static std::atomic_uint32_t value{};
};

/// Stands in for a real generated `system::Dispatch<Contract, Components>` --
/// `poll_output_streams` calls this exactly the way it would call
/// `Dispatch::publish<Stream>()` for a real service's `Output<Endpoint,
/// Publisher>` binding. Always has a fresh value ready (never returns
/// nullopt) so the tests below isolate scheduling/delivery behavior rather
/// than "nothing to publish this tick."
struct PollDispatch
{
    template <typename StreamT>
    [[nodiscard]] static std::optional<typename StreamT::Value> publish()
    {
        return typename StreamT::Value{
            .sequence = PollCounter<StreamT>::value.fetch_add(1, std::memory_order_relaxed) + 1};
    }
};

using Architecture =
    solar::remote::Architecture<solar::TypeList<>, solar::TypeList<>, solar::TypeList<>,
                                solar::TypeList<>, solar::TypeList<QueuedStream, ReliableStream, FastStream>,
                                solar::TypeList<Link>, solar::TypeList<>, solar::TypeList<>>;
using Context = solar::remote::RuntimeContext<Architecture, PollDispatch>;

struct Tag
{};

using Runtime = solar::remote::ByteRuntime<Tag, Architecture, solar::TypeList<>, PollDispatch>;

using ServiceT = Context::RemoteService;
using State = solar::remote::detail::LinkState<ServiceT, Link, 0>;

constexpr auto queued_slot = solar::remote::detail::stream_subscription_slot<Context, QueuedStream>();
constexpr auto reliable_slot =
    solar::remote::detail::stream_subscription_slot<Context, ReliableStream>();
constexpr auto fast_slot = solar::remote::detail::stream_subscription_slot<Context, FastStream>();

/// Same technique `remote_outbound_credit`'s `subscribe()` uses: pokes the
/// subscription state a real Subscribe request would establish, without
/// needing a full protocol handshake.
void subscribe_queued()
{
    {
        auto guard = State::output_lock.acquire();
        State::subscriptions[queued_slot].active = true;
    }
    State::session.store(solar::remote::SessionState::Active, std::memory_order_release);
    solar::remote::detail::stream_state<Context, QueuedStream>().interested_sessions.fetch_add(
        1, std::memory_order_acq_rel);
}

void subscribe_reliable(std::uint16_t credits)
{
    {
        auto guard = State::output_lock.acquire();
        auto& subscription = State::subscriptions[reliable_slot];
        subscription.active = true;
        subscription.credits = credits;
    }
    State::session.store(solar::remote::SessionState::Active, std::memory_order_release);
    solar::remote::detail::push_state<Context, ReliableStream>().interested_sessions.fetch_add(
        1, std::memory_order_acq_rel);
}

void subscribe_fast()
{
    {
        auto guard = State::output_lock.acquire();
        State::subscriptions[fast_slot].active = true;
    }
    State::session.store(solar::remote::SessionState::Active, std::memory_order_release);
    solar::remote::detail::stream_state<Context, FastStream>().interested_sessions.fetch_add(
        1, std::memory_order_acq_rel);
    // The real Subscribe request itself wakes Service::run. This fixture
    // establishes the state directly, so supply the equivalent wake.
    zassert_true(ServiceT::notify_publication(fast_slot));
}

void set_reliable_credits(std::uint16_t credits)
{
    auto guard = State::output_lock.acquire();
    State::subscriptions[reliable_slot].credits = credits;
}

void reset_state()
{
    {
        auto guard = State::output_lock.acquire();
        State::subscriptions[queued_slot] = {};
        State::subscriptions[reliable_slot] = {};
        State::subscriptions[fast_slot] = {};
    }
    {
        auto& state = solar::remote::detail::stream_state<Context, QueuedStream>();
        auto guard = state.lock.acquire();
        state.values = {};
        state.head = 0;
        state.size = 0;
        state.sequence = 0;
        state.replaced = 0;
        state.wake_pending.store(false, std::memory_order_relaxed);
        state.interested_sessions.store(0, std::memory_order_relaxed);
    }
    {
        auto& push = solar::remote::detail::push_state<Context, ReliableStream>();
        auto guard = push.lock.acquire();
        push.values = {};
        push.head = 0;
        push.size = 0;
        push.sequence = 0;
        push.replaced = 0;
        push.wake_pending.store(false, std::memory_order_relaxed);
        push.interested_sessions.store(0, std::memory_order_relaxed);
    }
    {
        auto& state = solar::remote::detail::stream_state<Context, FastStream>();
        auto guard = state.lock.acquire();
        state.values = {};
        state.head = 0;
        state.size = 0;
        state.sequence = 0;
        state.replaced = 0;
        state.wake_pending.store(false, std::memory_order_relaxed);
        state.interested_sessions.store(0, std::memory_order_relaxed);
    }
    solar::remote::detail::poll_schedule_state<Context, QueuedStream>() = {};
    solar::remote::detail::poll_schedule_state<Context, ReliableStream>() = {};
    solar::remote::detail::poll_schedule_state<Context, FastStream>() = {};
    PollCounter<QueuedStream>::value.store(0, std::memory_order_relaxed);
    PollCounter<ReliableStream>::value.store(0, std::memory_order_relaxed);
    PollCounter<FastStream>::value.store(0, std::memory_order_relaxed);
}

/// Drains whatever the (fake) DMA hardware would have transmitted so far,
/// counting how many decoded frames target `descriptor` -- standing in for
/// real hardware completing a transfer, same technique as
/// `remote_outbound_credit`'s `pump_wire`.
std::size_t pump_wire_for(std::uint32_t target, int spin_count = 64, std::int32_t sleep_ms = 10)
{
    std::size_t matched{};
    for (int spins = 0; spins < spin_count; ++spins) {
        (void)Link::advance_tx(4096);
        std::array<std::byte, 512> wire{};
        auto taken = Link::take_transmitted(wire);
        if (!taken) {
            // Give the event-or-deadline service time to run instead of
            // spinning inside the same instant forever.
            k_sleep(K_MSEC(sleep_ms));
            continue;
        }
        std::array<std::byte, 512> scratch{};
        auto decoded = solar::remote::frame::decode(std::span{wire}.first(*taken), scratch);
        if (decoded && decoded->envelope.kind == solar::remote::protocol::Kind::Data &&
            decoded->envelope.target == target) {
            ++matched;
        }
    }
    return matched;
}

void discard_handshake_noise()
{
    (void)pump_wire_for(0, /*spin_count=*/8, /*sleep_ms=*/10);
}

void after_each(void*)
{
    (void)Runtime::stop();
    (void)Runtime::deinitialize();
    Link::close();
    reset_state();
}

} // namespace fixture

ZTEST(remote_output_stream_scheduler, test_best_effort_stream_is_pulled_and_rate_limited)
{
    using namespace fixture;

    zassert_true(Runtime::initialize().has_value());
    zassert_true(Runtime::start().has_value());
    zassert_true(Link::connect().has_value());
    k_sleep(K_MSEC(20));
    discard_handshake_noise();

    // No manual publish anywhere in this test -- every value in flight comes
    // from the scheduler pulling `PollDispatch::publish<QueuedStream>()` on
    // its own, which is exactly the producer path that did not exist before
    // `poll_output_streams`.
    subscribe_queued();

    // maximum_rate_hz = 4 (250ms interval) against a ~600ms pump window:
    // the first poll fires immediately (a stream is always due on its first
    // tick), then at most ~2 more within the window -- comfortably fewer
    // than the ~12 a 50ms-tick-every-time delivery would produce if the rate
    // limit were not honored.
    const auto delivered = pump_wire_for(QueuedStream::descriptor.id.value, /*spin_count=*/60,
                                         /*sleep_ms=*/10);
    zassert_true(delivered >= 1, "the poll scheduler must deliver at least the first due tick");
    zassert_true(delivered <= 4, "maximum_rate_hz must bound delivery, not fire every maintenance tick");
}

ZTEST(remote_output_stream_scheduler, test_reliable_stream_enqueues_and_drains_on_credit)
{
    using namespace fixture;

    zassert_true(Runtime::initialize().has_value());
    zassert_true(Runtime::start().has_value());
    zassert_true(Link::connect().has_value());
    k_sleep(K_MSEC(20));
    discard_handshake_noise();

    // Subscribed with zero credit: the scheduler still pulls fresh values
    // from PollDispatch every ~1ms-rate-limited tick and enqueues them into
    // the ReliableWindow<3> backlog via enqueue_reliable_stream, but nothing
    // is transmitted while credit is zero -- same contract Stage 5 already
    // verified for the Data-catalog case, now reachable from a poll producer.
    subscribe_reliable(/*credits=*/0);

    const auto before_credit =
        pump_wire_for(ReliableStream::descriptor.id.value, /*spin_count=*/8, /*sleep_ms=*/10);
    zassert_equal(before_credit, 0U, "zero credit must hold back delivery, not drop it silently");

    set_reliable_credits(3);
    const auto after_credit =
        pump_wire_for(ReliableStream::descriptor.id.value, /*spin_count=*/64, /*sleep_ms=*/10);
    zassert_true(after_credit >= 3,
                "granting credit must release the poll-scheduler-enqueued backlog");
}

ZTEST(remote_output_stream_scheduler, test_high_rate_stream_uses_its_deadline_only_when_subscribed)
{
    using namespace fixture;

    zassert_true(Runtime::initialize().has_value());
    zassert_true(Runtime::start().has_value());
    zassert_true(Link::connect().has_value());
    k_sleep(K_MSEC(20));
    discard_handshake_noise();

    // Declaring a stream must not make its stateful producer run while no
    // client has asked for it.
    k_sleep(K_MSEC(70));
    zassert_equal(PollCounter<FastStream>::value.load(std::memory_order_relaxed), 0U,
                  "unsubscribed output streams must not be polled");

    subscribe_fast();
    k_sleep(K_MSEC(80));
    zassert_true(PollCounter<FastStream>::value.load(std::memory_order_relaxed) >= 8U,
                 "a subscribed 200 Hz stream must not be limited by the 50 ms maintenance tick");
}

ZTEST_SUITE(remote_output_stream_scheduler, nullptr, nullptr, nullptr, fixture::after_each, nullptr);
