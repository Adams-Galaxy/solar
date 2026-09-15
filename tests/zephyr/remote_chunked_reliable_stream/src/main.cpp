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

/// Stage 6's own confidence-test requirement (remote-arrays-and-chunked-
/// streaming.md §10): "One reliable-mode stream, built as a synthetic test
/// fixture ... a confidence test that the mode actually works end to end."
/// Unlike remote_outbound_credit (Stage 5, a bare scalar Value) and
/// remote_output_stream_scheduler (delivery-policy/scheduling only, no
/// payload decode), this fixture uses the real chunk envelope shape
/// (generation/chunk_index/final/points: Array<Record, N>) and decodes each
/// received frame's actual CBOR payload back into a typed value, so it
/// verifies content and order, not just frame count.
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
        .id = TypeId{0x9103},
        .name = "fixture.ChunkPoint",
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
    solar::BoundedVector<ChunkPoint, 4> points{};
};

} // namespace fixture

template <> struct solar::remote::Schema<fixture::ScanChunk>
{
    static constexpr SchemaDescriptor descriptor{
        .id = TypeId{0x9104},
        .name = "fixture.ScanChunk",
    };
    using Fields =
        remote::Fields<Field<1, "generation", &fixture::ScanChunk::generation>,
                       Field<2, "chunk_index", &fixture::ScanChunk::chunk_index>,
                       Field<3, "final", &fixture::ScanChunk::final>,
                       Field<4, "points", &fixture::ScanChunk::points>>;
    static constexpr std::size_t max_encoded_size = 96;
    static constexpr Codec codec = Codec::Cbor;
};

namespace fixture
{

/// A `direction: out` chunked Stream declared the same shape codegen now
/// produces for `delivery: reliable<3>` (see
/// tools/solar_codegen/compiler.py's `direction: out` emission).
struct ReliableChunkStream
{
    static constexpr solar::remote::StreamDescriptor descriptor{
        .id = solar::remote::StreamId{0x9203},
        .name = "fixture.reliable_chunk_stream",
        .description = "Chunked reliable-mode OutStream confidence fixture",
    };
    using Value = ScanChunk;
    static constexpr bool input = false;
    // Moderate relative to the ~50ms maintenance tick -- paced production,
    // not a burst, matching how a real producer (e.g. Navigation's own
    // per-revolution chunking) behaves: never far ahead of what the window
    // plus prompt credit can absorb.
    static constexpr std::uint32_t maximum_rate_hz = 20;
    using Capabilities =
        solar::remote::Capabilities<solar::remote::OutStream<solar::remote::Push,
                                                              solar::remote::ReliableWindow<3>>>;
    using ContractType = ReliableChunkStream;
};

struct Link : solar::remote::testing::FakeDmaLink<Link, 512, 512>
{
    static constexpr solar::remote::LinkDescriptor descriptor{
        .id = solar::remote::LinkId{0x8203},
        .name = "fixture.fake_dma_chunked_reliable",
    };
};

/// Three scans of three chunks each (final on chunk_index 2), two points per
/// chunk with deterministic, distinguishable content -- enough to prove
/// order and completeness across generation boundaries, not just count.
constexpr std::size_t total_chunks = 9;

[[nodiscard]] ScanChunk chunk_at(std::size_t index)
{
    const auto generation = 500U + static_cast<std::uint64_t>(index / 3);
    const auto chunk_index = static_cast<std::uint32_t>(index % 3);
    ScanChunk chunk{
        .generation = generation,
        .chunk_index = chunk_index,
        .final = chunk_index == 2,
    };
    for (std::uint32_t point = 0; point < 2; ++point) {
        chunk.points.storage[point] =
            ChunkPoint{.value = static_cast<std::uint32_t>(generation * 1000 + chunk_index * 10 +
                                                            point)};
    }
    chunk.points.size = 2;
    return chunk;
}

/// Stands in for a real generated `system::Dispatch<Contract, Components>`,
/// same technique remote_output_stream_scheduler's PollDispatch uses --
/// `poll_output_streams` calls this exactly the way it would call
/// `Dispatch::publish<Stream>()` for a real service's `Output<Endpoint,
/// Publisher>` binding.
struct PollDispatch
{
    inline static std::size_t next_index{};

    // Gated until the test explicitly arms it, *after* the subscription is
    // fully established -- poll_output_streams ticks on its own schedule
    // regardless of subscriber interest (matching write_discrete's ordinary
    // "NoSubscribers" disposition for any pub-sub delivery: connect/start
    // must precede subscribe here because Link::connect's session handling
    // requires it, so the very first, immediate scheduler tick unavoidably
    // fires before any subscriber exists). Without this gate that first
    // tick would silently burn chunk_at(0) off the fixed test sequence --
    // not a framework bug, just nobody subscribed yet, but it would
    // undermine this test's "every produced chunk arrives" claim.
    inline static bool armed{};

    template <typename StreamT>
    [[nodiscard]] static std::optional<typename StreamT::Value> publish()
    {
        if (!armed || next_index >= total_chunks) {
            return std::nullopt;
        }
        return chunk_at(next_index++);
    }
};

using Architecture =
    solar::remote::Architecture<solar::TypeList<ChunkPoint>, solar::TypeList<>, solar::TypeList<>,
                                solar::TypeList<>, solar::TypeList<ReliableChunkStream>,
                                solar::TypeList<Link>, solar::TypeList<>, solar::TypeList<>>;
using Context = solar::remote::RuntimeContext<Architecture, PollDispatch>;

struct Tag
{};

using Runtime = solar::remote::ByteRuntime<Tag, Architecture, solar::TypeList<>, PollDispatch>;

using ServiceT = Context::RemoteService;
using State = solar::remote::detail::LinkState<ServiceT, Link, 0>;

constexpr auto slot = solar::remote::detail::stream_subscription_slot<Context, ReliableChunkStream>();

void subscribe(std::uint16_t credits)
{
    {
        auto guard = State::output_lock.acquire();
        State::subscriptions[slot].active = true;
        State::subscriptions[slot].credits = credits;
    }
    State::session.store(solar::remote::SessionState::Active, std::memory_order_release);
    solar::remote::detail::push_state<Context, ReliableChunkStream>().interested_sessions.fetch_add(
        1, std::memory_order_acq_rel);
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
    auto& push = solar::remote::detail::push_state<Context, ReliableChunkStream>();
    auto guard = push.lock.acquire();
    push.values = {};
    push.head = 0;
    push.size = 0;
    push.sequence = 0;
    push.replaced = 0;
    push.wake_pending.store(false, std::memory_order_relaxed);
    push.interested_sessions.store(0, std::memory_order_relaxed);
    PollDispatch::next_index = 0;
    PollDispatch::armed = false;
}

/// Drains whatever the (fake) DMA hardware would have transmitted, decoding
/// every `Kind::Data` frame targeting this stream into a real `ScanChunk` --
/// this is the part remote_output_stream_scheduler's fixture does not do
/// (it only counts frames), and is exactly what proves "no silent drop"
/// means "the station can reconstruct the exact sequence that was sent",
/// not merely "some number of frames arrived."
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
        if (!decoded || decoded->envelope.target != ReliableChunkStream::descriptor.id.value) {
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

ZTEST(remote_chunked_reliable_stream, test_all_chunks_arrive_complete_in_order_and_intact)
{
    using namespace fixture;

    zassert_true(Runtime::initialize().has_value());
    zassert_true(Runtime::start().has_value());
    zassert_true(Link::connect().has_value());
    k_sleep(K_MSEC(20));
    discard_handshake_noise();

    // Ample credit granted up front: drain is never the bottleneck, so this
    // exercises the realistic "well-paced producer, attentive consumer"
    // case Stage 6 asks to confirm -- zero manual publish/write_data call
    // anywhere in this test, every chunk comes from the scheduler pulling
    // PollDispatch on its own. PollDispatch::armed is only set true here,
    // after the subscription is fully established, so the scheduler's own
    // periodic polling (which runs regardless of subscriber interest, same
    // as it does for Watch/Topic) cannot burn any of the fixed test
    // sequence before anyone is listening for it -- see PollDispatch::armed.
    subscribe(/*credits=*/total_chunks);
    PollDispatch::armed = true;

    const auto result = pump_wire(/*spin_count=*/160, /*sleep_ms=*/15);
    zassert_false(result.saw_error, "a well-paced reliable stream must not abort");
    zassert_equal(result.chunks.size(), total_chunks,
                  "every chunk produced must arrive -- reliable mode allows no silent drop");

    for (std::size_t index = 0; index < total_chunks; ++index) {
        const auto expected = chunk_at(index);
        const auto& actual = result.chunks[index];
        zassert_equal(actual.generation, expected.generation, "generation must match in order");
        zassert_equal(actual.chunk_index, expected.chunk_index,
                      "chunk_index must arrive in order, no reordering");
        zassert_equal(actual.final, expected.final, "final flag must land on the right chunk");
        zassert_equal(actual.points.size, expected.points.size, "point count must be intact");
        for (std::size_t point = 0; point < expected.points.size; ++point) {
            zassert_equal(actual.points.storage[point].value, expected.points.storage[point].value,
                          "point content must decode intact, not just the envelope");
        }
    }
    zassert_true(subscription_active(), "a fully-drained reliable stream must not abort");
}

ZTEST(remote_chunked_reliable_stream, test_stalled_consumer_aborts_with_zero_partial_delivery)
{
    using namespace fixture;

    zassert_true(Runtime::initialize().has_value());
    zassert_true(Runtime::start().has_value());
    zassert_true(Link::connect().has_value());
    k_sleep(K_MSEC(20));
    discard_handshake_noise();

    // Zero credit, forever: the scheduler keeps producing and enqueueing
    // (bounded by ReliableWindow<3>) but nothing can be sent. Past
    // CONFIG_SOLAR_REMOTE_OUTBOUND_STALL_TIMEOUT_MS (200ms in this test's
    // prj.conf) the subscription must abort with a real protocol error --
    // never silently sit there looking alive while actually starved.
    subscribe(/*credits=*/0);
    PollDispatch::armed = true;

    // Deliberately short and well under the 200ms stall deadline -- this
    // only needs to prove nothing was transmitted promptly, not survive a
    // full abort cycle (that's the second pump below).
    const auto result = pump_wire(/*spin_count=*/5, /*sleep_ms=*/5);
    zassert_equal(result.chunks.size(), 0U, "zero credit must hold back delivery entirely");
    zassert_false(result.saw_error, "must not abort before the stall deadline");
    zassert_true(subscription_active());

    const auto after_stall = pump_wire(/*spin_count=*/64, /*sleep_ms=*/10);
    zassert_equal(after_stall.chunks.size(), 0U,
                  "a stalled reliable stream must never deliver a partial chunk sequence");
    zassert_true(after_stall.saw_error,
                "the station must be told the reliable stream broke, not left silent");
    zassert_false(subscription_active(), "a stalled reliable stream must abort, not hang forever");
}

ZTEST_SUITE(remote_chunked_reliable_stream, nullptr, nullptr, nullptr, fixture::after_each, nullptr);
