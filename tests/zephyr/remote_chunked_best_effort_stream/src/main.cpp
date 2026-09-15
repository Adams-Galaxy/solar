#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <solar/remote.hpp>
#include <solar/remote/runtime_context.hpp>
#include <solar/remote/testing/fake_dma_link.hpp>

/// Stage 6's soak-test requirement (remote-arrays-and-chunked-streaming.md
/// §10): "verify best-effort mode degrades to dropped-not-stalled under
/// induced backpressure." No physical robot is reachable from this
/// environment, so this is the native-sim equivalent: induce backpressure
/// by never draining the (fake) wire for a while, then confirm the
/// producer never blocked on it (production kept advancing) and the
/// subscription never aborted (best-effort has no abort path at all,
/// unlike ReliableWindow -- see remote_chunked_reliable_stream) -- it just
/// silently drops stale chunks, exactly like navigation.lidar.points'
/// `delivery: queue<8, drop-oldest>` is meant to.
namespace fixture
{

struct ChunkPoint
{
    std::uint32_t value{};
};

} // namespace fixture

template <> struct solar::remote::Schema<fixture::ChunkPoint>
{
    static constexpr SchemaDescriptor descriptor{
        .id = TypeId{0x9105},
        .name = "fixture.BestEffortChunkPoint",
    };
    static constexpr SchemaShape shape = SchemaShape::Record;
    using Fields = remote::Fields<Field<1, "value", &fixture::ChunkPoint::value>>;
    static constexpr std::size_t encoded_size = 4;
    static constexpr Codec codec = Codec::Cbor;
};

namespace fixture
{

struct ScanChunk
{
    std::uint64_t generation{};
    std::uint32_t chunk_index{};
    bool final{};
    solar::BoundedVector<ChunkPoint, 2> points{};
};

} // namespace fixture

template <> struct solar::remote::Schema<fixture::ScanChunk>
{
    static constexpr SchemaDescriptor descriptor{
        .id = TypeId{0x9106},
        .name = "fixture.BestEffortScanChunk",
    };
    using Fields =
        remote::Fields<Field<1, "generation", &fixture::ScanChunk::generation>,
                       Field<2, "chunk_index", &fixture::ScanChunk::chunk_index>,
                       Field<3, "final", &fixture::ScanChunk::final>,
                       Field<4, "points", &fixture::ScanChunk::points>>;
    static constexpr std::size_t max_encoded_size = 80;
    static constexpr Codec codec = Codec::Cbor;
};

namespace fixture
{

/// A `direction: out` chunked Stream declared the same shape codegen
/// produces for `delivery: queue<3, drop-oldest>` -- the same policy shape
/// `navigation.lidar.points` actually uses (Count differs; the mechanism
/// under test does not care about the exact depth).
struct BestEffortChunkStream
{
    static constexpr solar::remote::StreamDescriptor descriptor{
        .id = solar::remote::StreamId{0x9204},
        .name = "fixture.best_effort_chunk_stream",
        .description = "Chunked best-effort OutStream backpressure fixture",
    };
    using Value = ScanChunk;
    static constexpr bool input = false;
    // Fast relative to the ~50ms maintenance tick, and fast relative to how
    // long the test withholds wire draining -- production must comfortably
    // outrun a 3-deep window within the induced-backpressure window below.
    static constexpr std::uint32_t maximum_rate_hz = 200;
    using Capabilities =
        solar::remote::Capabilities<solar::remote::OutStream<solar::remote::Push,
                                                              solar::remote::Queue<3, solar::remote::DropOldest>>>;
    using ContractType = BestEffortChunkStream;
};

struct Link : solar::remote::testing::FakeDmaLink<Link, 512, 512>
{
    static constexpr solar::remote::LinkDescriptor descriptor{
        .id = solar::remote::LinkId{0x8204},
        .name = "fixture.fake_dma_chunked_best_effort",
    };
};

/// One chunk per "scan" (chunk_index always 0, always final) with a
/// strictly increasing generation -- an open-ended production sequence
/// (unlike the reliable fixture's fixed 9), since this test's whole point
/// is "keeps producing forever without blocking," not "produces exactly N
/// and stops."
[[nodiscard]] ScanChunk chunk_at(std::uint64_t generation)
{
    ScanChunk chunk{.generation = generation, .chunk_index = 0, .final = true};
    chunk.points.storage[0] = ChunkPoint{.value = static_cast<std::uint32_t>(generation)};
    chunk.points.size = 1;
    return chunk;
}

struct PollDispatch
{
    inline static std::uint64_t next_generation{1};
    inline static bool armed{};

    template <typename StreamT>
    [[nodiscard]] static std::optional<typename StreamT::Value> publish()
    {
        if (!armed) {
            return std::nullopt;
        }
        return chunk_at(next_generation++);
    }
};

using Architecture =
    solar::remote::Architecture<solar::TypeList<ChunkPoint>, solar::TypeList<>, solar::TypeList<>,
                                solar::TypeList<>, solar::TypeList<BestEffortChunkStream>,
                                solar::TypeList<Link>, solar::TypeList<>, solar::TypeList<>>;
using Context = solar::remote::RuntimeContext<Architecture, PollDispatch>;

struct Tag
{};

using Runtime = solar::remote::ByteRuntime<Tag, Architecture, solar::TypeList<>, PollDispatch>;

using ServiceT = Context::RemoteService;
using State = solar::remote::detail::LinkState<ServiceT, Link, 0>;

constexpr auto slot =
    solar::remote::detail::stream_subscription_slot<Context, BestEffortChunkStream>();

void subscribe()
{
    {
        auto guard = State::output_lock.acquire();
        State::subscriptions[slot].active = true;
    }
    State::session.store(solar::remote::SessionState::Active, std::memory_order_release);
    solar::remote::detail::stream_state<Context, BestEffortChunkStream>()
        .interested_sessions.fetch_add(1, std::memory_order_acq_rel);
}

bool subscription_active()
{
    auto guard = State::output_lock.acquire();
    return State::subscriptions[slot].active;
}

void reset_state()
{
    {
        auto guard = State::output_lock.acquire();
        State::subscriptions[slot] = {};
    }
    auto& state = solar::remote::detail::stream_state<Context, BestEffortChunkStream>();
    auto guard = state.lock.acquire();
    state.values = {};
    state.head = 0;
    state.size = 0;
    state.sequence = 0;
    state.replaced = 0;
    state.wake_pending.store(false, std::memory_order_relaxed);
    state.interested_sessions.store(0, std::memory_order_relaxed);
    PollDispatch::next_generation = 1;
    PollDispatch::armed = false;
}

struct PumpResult
{
    std::vector<fixture::ScanChunk> chunks;
    bool saw_error{};
};

PumpResult pump_wire(int spin_count = 64, std::int32_t sleep_ms = 10)
{
    PumpResult result{};
    for (int spins = 0; spins < spin_count; ++spins) {
        (void)Link::advance_tx(4096);
        std::array<std::byte, 512> wire{};
        auto taken = Link::take_transmitted(wire);
        if (!taken) {
            k_sleep(K_MSEC(sleep_ms));
            continue;
        }
        std::array<std::byte, 512> scratch{};
        auto decoded = solar::remote::frame::decode(std::span{wire}.first(*taken), scratch);
        if (!decoded || decoded->envelope.target != BestEffortChunkStream::descriptor.id.value) {
            continue;
        }
        if (decoded->envelope.kind == solar::remote::protocol::Kind::Error) {
            result.saw_error = true;
            continue;
        }
        if (decoded->envelope.kind != solar::remote::protocol::Kind::Data) {
            continue;
        }
        auto value = solar::remote::cbor::decode<fixture::ScanChunk>(decoded->payload);
        if (value) {
            result.chunks.push_back(*value);
        }
    }
    return result;
}

void discard_handshake_noise()
{
    (void)pump_wire(/*spin_count=*/8, /*sleep_ms=*/10);
}

void after_each(void*)
{
    (void)Runtime::stop();
    (void)Runtime::deinitialize();
    Link::close();
    reset_state();
}

} // namespace fixture

ZTEST(remote_chunked_best_effort_stream, test_backpressure_drops_stale_chunks_never_stalls_or_aborts)
{
    using namespace fixture;

    zassert_true(Runtime::initialize().has_value());
    zassert_true(Runtime::start().has_value());
    zassert_true(Link::connect().has_value());
    k_sleep(K_MSEC(20));
    discard_handshake_noise();

    subscribe();
    PollDispatch::armed = true;

    // Induced backpressure: never touch the (fake) wire for a while, so
    // nothing can be transmitted, while the scheduler keeps polling and
    // enqueueing into a Queue<3, DropOldest> ring -- production must keep
    // advancing regardless (best-effort never blocks the producer on a
    // slow/absent consumer, unlike ReliableWindow). The poll scheduler is
    // itself bounded by the ~50ms service maintenance tick regardless of
    // maximum_rate_hz (confirmed empirically -- maximum_rate_hz can only
    // slow delivery below that floor, never speed it up past it), so 600ms
    // comfortably allows well over 5 ticks.
    k_sleep(K_MSEC(600));
    // Stop production before draining -- otherwise the drain pump's own
    // sleep window gives the scheduler more ticks, so "how many arrived"
    // would measure "how long we drained for" instead of "how much the
    // 3-deep ring actually held," conflating two different things.
    PollDispatch::armed = false;
    const auto produced = PollDispatch::next_generation - 1;
    zassert_true(produced > 5,
                "the scheduler must keep producing during backpressure, not stall waiting for "
                "wire drainage");
    zassert_true(subscription_active(),
                "best-effort delivery must never abort a subscription, only drop stale values");

    // Now drain -- whatever survived the backpressure window (some already
    // in flight, some still in the ring) goes out. The exact mix of which
    // generations survive is an implementation detail of how the ring and
    // the single-frame-in-flight link interact; what "best-effort degrades
    // to dropped, not stalled" actually promises is checked below: not
    // everything produced arrives, whatever does arrive is intact and none
    // of it is corrupted, and the newest value produced is among what
    // survives (the point of DropOldest is that recency wins).
    const auto result = pump_wire(/*spin_count=*/32, /*sleep_ms=*/10);
    zassert_false(result.saw_error, "best-effort delivery has no abort/error path at all");
    zassert_true(result.chunks.size() >= 1, "something must still get delivered");
    zassert_true(result.chunks.size() < produced,
                "far fewer chunks must arrive than were produced -- proof drops actually happened,"
                " not just that production was slow");

    bool saw_newest = false;
    for (const auto& chunk : result.chunks) {
        zassert_true(chunk.generation >= 1 && chunk.generation <= produced,
                    "every delivered chunk must be intact and genuinely one that was produced");
        saw_newest = saw_newest || chunk.generation == produced;
    }
    zassert_true(saw_newest,
                "DropOldest must never sacrifice the newest value in favor of a stale one");
    zassert_true(subscription_active(), "still active after drain -- best-effort never aborts");
}

ZTEST_SUITE(remote_chunked_best_effort_stream, nullptr, nullptr, nullptr, fixture::after_each,
           nullptr);
