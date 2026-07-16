#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>

#include <zephyr/irq_offload.h>
#include <zephyr/ztest.h>

#include <native_rtc.h>

#include <solar/solar.hpp>

namespace fixture
{

struct Frames
{
    using Value = std::uint64_t;
    using Instrument = solar::metrics::Counter;
    using Unit = solar::metrics::units::Frames;
    static constexpr solar::metrics::Descriptor descriptor{.name = "frames"};
};

struct Depth
{
    using Value = std::int32_t;
    using Instrument = solar::metrics::Gauge;
    using Unit = solar::metrics::units::Items;
    static constexpr Value initial_value = 3;
    static constexpr solar::metrics::Descriptor descriptor{.name = "depth"};
};

struct Voltage
{
    using Value = float;
    using Instrument = solar::metrics::Gauge;
    using Unit = solar::metrics::units::Volts;
    using Concurrency = solar::metrics::concurrency::SpinLocked;
    static constexpr solar::metrics::Descriptor descriptor{.name = "voltage"};
};

struct Samples
{
    using Value = std::int32_t;
    using Instrument = solar::metrics::Distribution<solar::metrics::Summary>;
    using Unit = solar::metrics::units::Count;
    static constexpr solar::metrics::Descriptor descriptor{.name = "samples"};
};

struct Recent
{
    using Value = std::uint16_t;
    using Instrument = solar::metrics::Distribution<solar::metrics::WindowMean<3>>;
    using Unit = solar::metrics::units::Count;
    static constexpr solar::metrics::Descriptor descriptor{.name = "recent"};
};

struct Smoothed
{
    using Value = float;
    using Instrument = solar::metrics::Distribution<solar::metrics::Ema<1, 2>>;
    using Unit = solar::metrics::units::Ratio;
    static constexpr solar::metrics::Descriptor descriptor{.name = "smoothed"};
};

struct Buckets
{
    using Value = std::uint16_t;
    using Instrument = solar::metrics::Distribution<solar::metrics::Histogram<10U, 20U>>;
    using Unit = solar::metrics::units::Count;
    static constexpr solar::metrics::Descriptor descriptor{.name = "buckets"};
};

struct Latency
{
    using Value = std::uint64_t;
    using Instrument = solar::metrics::Timer<solar::metrics::Summary>;
    using Unit = solar::metrics::units::Microseconds;
    static constexpr solar::metrics::Descriptor descriptor{.name = "latency"};
};

struct Saturating
{
    using Value = std::uint8_t;
    using Instrument = solar::metrics::Counter;
    using Unit = solar::metrics::units::Count;
    using Overflow = solar::metrics::overflow::Saturate;
    static constexpr solar::metrics::Descriptor descriptor{.name = "saturating"};
};

struct Rejecting
{
    using Value = std::uint8_t;
    using Instrument = solar::metrics::Counter;
    using Unit = solar::metrics::units::Count;
    using Overflow = solar::metrics::overflow::Reject;
    static constexpr solar::metrics::Descriptor descriptor{.name = "rejecting"};
};

struct Wrapping
{
    using Value = std::uint8_t;
    using Instrument = solar::metrics::Counter;
    using Unit = solar::metrics::units::Count;
    using Overflow = solar::metrics::overflow::Wrap;
    static constexpr solar::metrics::Descriptor descriptor{.name = "wrapping"};
};

struct Resettable
{
    using Value = std::uint32_t;
    using Instrument = solar::metrics::Counter;
    using Unit = solar::metrics::units::Count;
    using Reset = solar::metrics::RuntimeResettable;
    static constexpr solar::metrics::Descriptor descriptor{.name = "resettable"};
};

struct ThreadSummary
{
    using Value = std::uint32_t;
    using Instrument = solar::metrics::Distribution<solar::metrics::Summary>;
    using Unit = solar::metrics::units::Count;
    using Concurrency = solar::metrics::concurrency::MutexProtected;
    static constexpr solar::metrics::Descriptor descriptor{.name = "thread-summary"};
};

struct SaturatingSummary
{
    using Value = std::uint64_t;
    using Instrument = solar::metrics::Distribution<solar::metrics::Summary>;
    using Unit = solar::metrics::units::Count;
    using Overflow = solar::metrics::overflow::Saturate;
    static constexpr solar::metrics::Descriptor descriptor{.name = "saturating-summary"};
};

struct RejectingSummary : SaturatingSummary
{
    using Overflow = solar::metrics::overflow::Reject;
    static constexpr solar::metrics::Descriptor descriptor{.name = "rejecting-summary"};
};

struct WrappingSummary : SaturatingSummary
{
    using Overflow = solar::metrics::overflow::Wrap;
    static constexpr solar::metrics::Descriptor descriptor{.name = "wrapping-summary"};
};

struct ResettableSummary : SaturatingSummary
{
    using Reset = solar::metrics::RuntimeResettable;
    static constexpr solar::metrics::Descriptor descriptor{.name = "resettable-summary"};
};

struct Interrupts
{
    using Value = std::uint64_t;
    using Instrument = solar::metrics::Counter;
    using Unit = solar::metrics::units::Count;
    using Concurrency = solar::metrics::concurrency::Atomic;
    static constexpr solar::metrics::Descriptor descriptor{.name = "interrupts"};
};

struct ConcurrentCount
{
    using Value = std::uint64_t;
    using Instrument = solar::metrics::Counter;
    using Unit = solar::metrics::units::Count;
    using Concurrency = solar::metrics::concurrency::Atomic;
    static constexpr solar::metrics::Descriptor descriptor{.name = "concurrent-count"};
};

struct BenchmarkCounter
{
    using Value = std::uint64_t;
    using Instrument = solar::metrics::Counter;
    using Unit = solar::metrics::units::Count;
    using Concurrency = solar::metrics::concurrency::Atomic;
    static constexpr solar::metrics::Descriptor descriptor{.name = "benchmark-counter"};
};

struct ConcurrentSpin
{
    using Value = std::uint32_t;
    using Instrument = solar::metrics::Distribution<solar::metrics::Summary>;
    using Unit = solar::metrics::units::Count;
    using Concurrency = solar::metrics::concurrency::SpinLocked;
    static constexpr solar::metrics::Descriptor descriptor{.name = "concurrent-spin"};
};

struct ConcurrentMutex
{
    using Value = std::uint32_t;
    using Instrument = solar::metrics::Distribution<solar::metrics::Summary>;
    using Unit = solar::metrics::units::Count;
    using Concurrency = solar::metrics::concurrency::MutexProtected;
    static constexpr solar::metrics::Descriptor descriptor{.name = "concurrent-mutex"};
};

struct FrameDropped
{
    struct Payload
    {
        std::uint32_t bytes{};
        std::chrono::microseconds latency{};
    };

    static constexpr solar::events::Descriptor descriptor{.name = "frame-dropped"};
};

struct Burst
{
    using Payload = std::uint32_t;
    using Capture =
        solar::events::capture::AggregateCount<solar::events::interval::Milliseconds<5>>;
    static constexpr solar::events::Descriptor descriptor{.name = "burst"};
};

struct NarrowEvent
{
    struct Payload
    {
        std::uint64_t value{};
    };

    static constexpr solar::events::Descriptor descriptor{.name = "narrow-event"};
};

struct EventCount
{
    using Value = std::uint64_t;
    using Instrument = solar::metrics::Counter;
    using Unit = solar::metrics::units::Frames;
    static constexpr solar::metrics::Descriptor descriptor{.name = "event-count"};
};

struct EventBytes
{
    using Value = std::uint64_t;
    using Instrument = solar::metrics::Counter;
    using Unit = solar::metrics::units::Bytes;
    static constexpr solar::metrics::Descriptor descriptor{.name = "event-bytes"};
};

struct LastEventBytes
{
    using Value = std::uint32_t;
    using Instrument = solar::metrics::Gauge;
    using Unit = solar::metrics::units::Bytes;
    static constexpr solar::metrics::Descriptor descriptor{.name = "last-event-bytes"};
};

struct EventByteSummary
{
    using Value = std::uint32_t;
    using Instrument = solar::metrics::Distribution<solar::metrics::Summary>;
    using Unit = solar::metrics::units::Bytes;
    static constexpr solar::metrics::Descriptor descriptor{.name = "event-byte-summary"};
};

struct NarrowGauge
{
    using Value = std::uint8_t;
    using Instrument = solar::metrics::Gauge;
    using Unit = solar::metrics::units::Count;
    static constexpr solar::metrics::Descriptor descriptor{.name = "narrow-gauge"};
};

struct EventLatency
{
    using Value = std::uint64_t;
    using Instrument = solar::metrics::Timer<solar::metrics::Summary>;
    using Unit = solar::metrics::units::Microseconds;
    static constexpr solar::metrics::Descriptor descriptor{.name = "event-latency"};
};

struct BurstCount
{
    using Value = std::uint64_t;
    using Instrument = solar::metrics::Counter;
    using Unit = solar::metrics::units::Count;
    static constexpr solar::metrics::Descriptor descriptor{.name = "burst-count"};
};

using NarrowAdapter = solar::events::metrics::On<
    NarrowEvent, solar::events::metrics::Set<NarrowGauge, &NarrowEvent::Payload::value>>;
using BurstAdapter =
    solar::events::metrics::On<Burst, solar::events::metrics::Increment<BurstCount>>;

struct AdapterOwner
{
    static constexpr solar::component::Descriptor descriptor{.name = "adapter-owner"};
    using Events = solar::events::Events<FrameDropped, NarrowEvent, Burst>;
    using Metrics =
        solar::metrics::Metrics<EventCount, EventBytes, LastEventBytes, EventByteSummary,
                                NarrowGauge, EventLatency, BurstCount>;
    using EventMetrics = solar::events::metrics::Adapters<
        solar::events::metrics::On<
            FrameDropped, solar::events::metrics::Increment<EventCount>,
            solar::events::metrics::Add<EventBytes, &FrameDropped::Payload::bytes>,
            solar::events::metrics::Set<LastEventBytes, &FrameDropped::Payload::bytes>,
            solar::events::metrics::Observe<EventByteSummary, &FrameDropped::Payload::bytes>,
            solar::events::metrics::Record<EventLatency, &FrameDropped::Payload::latency>>,
        NarrowAdapter, BurstAdapter>;
};

using System = solar::System<solar::Blueprint<
    solar::Facilities<AdapterOwner>,
    solar::Metrics<Frames, Depth, Voltage, Samples, Recent, Smoothed, Buckets, Latency, Saturating,
                   Rejecting, Wrapping, Resettable, ThreadSummary, Interrupts, ConcurrentCount,
                   BenchmarkCounter, ConcurrentSpin, ConcurrentMutex, SaturatingSummary,
                   RejectingSummary, WrappingSummary, ResettableSummary>>>;

inline bool isr_succeeded{};
inline bool ordinary_isr_rejected{};
inline std::atomic_uint concurrent_done{};
K_THREAD_STACK_ARRAY_DEFINE(concurrent_stacks, 4, 4096);
inline std::array<k_thread, 4> concurrent_threads{};

struct ManualClock
{
    using rep = std::int64_t;
    using period = std::micro;
    using duration = std::chrono::duration<rep, period>;
    using time_point = std::chrono::time_point<ManualClock>;
    static constexpr bool is_steady = true;
    inline static time_point current{};

    [[nodiscard]] static time_point now() noexcept
    {
        return current;
    }
};

void update_from_isr(const void*)
{
    isr_succeeded = solar::metrics::try_inc_isr<Interrupts>().has_value();
    auto ordinary = solar::metrics::inc<Interrupts>();
    ordinary_isr_rejected =
        !ordinary && ordinary.error().reason == solar::metrics::Reason::InvalidContext;
}

void concurrent_producer(void*, void*, void*)
{
    for (int index = 0; index < 200; ++index) {
        (void)solar::metrics::inc<ConcurrentCount>();
        (void)solar::metrics::observe<ConcurrentSpin>(1);
        (void)solar::metrics::observe<ConcurrentMutex>(1);
    }
    concurrent_done.fetch_add(1, std::memory_order_release);
}

[[nodiscard]] std::uint64_t benchmark_now_ns() noexcept
{
    std::uint32_t nanoseconds{};
    std::uint64_t seconds{};
    native_rtc_gettime(RTC_CLOCK_PSEUDOHOSTREALTIME, &nanoseconds, &seconds);
    return seconds * 1'000'000'000ULL + nanoseconds;
}

} // namespace fixture

SOLAR_BIND_SYSTEM(fixture::System);

ZTEST(solar_metrics, test_scalar_instruments_and_lifecycle)
{
    zassert_true(solar::metrics::inc<fixture::Frames>().has_value());
    zassert_true(solar::metrics::add<fixture::Frames>(4).has_value());
    auto frames = solar::metrics::get<fixture::Frames>();
    zassert_true(frames.has_value());
    zassert_equal(frames->value, 5);
    zassert_equal(frames->updates, 2);

    auto depth = solar::metrics::get<fixture::Depth>();
    zassert_true(depth.has_value());
    zassert_true(depth->initialized);
    zassert_equal(depth->value, 3);
    zassert_true(solar::metrics::set<fixture::Depth>(9).has_value());
    zassert_equal(solar::metrics::get<fixture::Depth>()->value, 9);

    auto invalid = solar::metrics::set<fixture::Voltage>(std::numeric_limits<float>::infinity());
    zassert_false(invalid.has_value());
    zassert_equal(invalid.error().reason, solar::metrics::Reason::InvalidNumeric);
}

ZTEST(solar_metrics, test_reducers_views_timers_and_reset)
{
    zassert_true(solar::metrics::observe<fixture::Samples>(4).has_value());
    zassert_true(solar::metrics::observe<fixture::Samples>(8).has_value());
    zassert_true(solar::metrics::observe<fixture::Samples>(12).has_value());
    auto summary = solar::metrics::get<fixture::Samples>();
    zassert_true(summary.has_value());
    zassert_equal(summary->count, 3);
    zassert_equal(summary->sum, 24);
    zassert_equal(summary->minimum, 4);
    zassert_equal(summary->maximum, 12);
    zassert_within(summary->mean, 8.0, 0.001);
    auto maximum = solar::metrics::get_view<fixture::Samples, solar::metrics::view::Maximum>();
    zassert_true(maximum.has_value());
    zassert_equal(*maximum, 12);
    auto maximum_record =
        solar::metrics::records::view<fixture::Samples, solar::metrics::view::Maximum>();
    zassert_true(maximum_record.has_value());
    zassert_equal(std::get<std::int64_t>(maximum_record->value), 12);

    for (auto value : {2U, 4U, 8U, 10U}) {
        zassert_true(solar::metrics::observe<fixture::Recent>(value).has_value());
    }
    auto recent = solar::metrics::get<fixture::Recent>();
    zassert_equal(recent->count, 3);
    zassert_equal(recent->latest, 10);
    zassert_within(recent->mean, 22.0 / 3.0, 0.001);

    zassert_true(solar::metrics::observe<fixture::Smoothed>(2.0F).has_value());
    zassert_true(solar::metrics::observe<fixture::Smoothed>(6.0F).has_value());
    zassert_within(solar::metrics::get<fixture::Smoothed>()->mean, 4.0, 0.001);

    for (auto value : {5U, 10U, 11U, 21U}) {
        zassert_true(solar::metrics::observe<fixture::Buckets>(value).has_value());
    }
    auto histogram = solar::metrics::get<fixture::Buckets>();
    zassert_equal(histogram->buckets[0], 2);
    zassert_equal(histogram->buckets[1], 1);
    zassert_equal(histogram->buckets[2], 1);

    zassert_true(
        solar::metrics::record<fixture::Latency>(std::chrono::microseconds{25}).has_value());
    zassert_true(solar::metrics::record<fixture::Latency>(std::uint64_t{5}).has_value());
    auto latency = solar::metrics::get<fixture::Latency>();
    zassert_equal(latency->count, 2);
    zassert_equal(latency->sum, 30);

    fixture::ManualClock::current = fixture::ManualClock::time_point{};
    auto timer = solar::metrics::scoped<fixture::Latency, fixture::ManualClock>();
    fixture::ManualClock::current += std::chrono::microseconds{10};
    zassert_true(timer.finish().has_value());
    latency = solar::metrics::get<fixture::Latency>();
    zassert_equal(latency->count, 3);
    zassert_equal(latency->sum, 40);

    zassert_true(solar::metrics::add<fixture::Resettable>(7).has_value());
    auto reset = solar::metrics::reset<fixture::Resettable>();
    zassert_true(reset.has_value());
    zassert_equal(reset->epoch, 1);
    auto reset_reading = solar::metrics::get<fixture::Resettable>();
    zassert_equal(reset_reading->value, 0);
    zassert_equal(reset_reading->revision, 0);
}

ZTEST(solar_metrics, test_overflow_isr_and_records)
{
    zassert_true(solar::metrics::add<fixture::Saturating>(250).has_value());
    auto saturated = solar::metrics::add<fixture::Saturating>(10);
    zassert_true(saturated.has_value());
    zassert_equal(saturated->disposition, solar::metrics::UpdateDisposition::Saturated);
    zassert_equal(solar::metrics::get<fixture::Saturating>()->value, 255);

    zassert_true(solar::metrics::add<fixture::Rejecting>(250).has_value());
    auto rejected = solar::metrics::add<fixture::Rejecting>(10);
    zassert_false(rejected.has_value());
    zassert_equal(rejected.error().reason, solar::metrics::Reason::Overflow);
    zassert_equal(solar::metrics::get<fixture::Rejecting>()->value, 250);

    zassert_true(solar::metrics::add<fixture::Wrapping>(250).has_value());
    auto wrapped = solar::metrics::add<fixture::Wrapping>(10);
    zassert_true(wrapped.has_value());
    zassert_equal(wrapped->disposition, solar::metrics::UpdateDisposition::Wrapped);
    zassert_equal(solar::metrics::get<fixture::Wrapping>()->value, 4);

    fixture::isr_succeeded = false;
    fixture::ordinary_isr_rejected = false;
    irq_offload(fixture::update_from_isr, nullptr);
    zassert_true(fixture::isr_succeeded);
    zassert_true(fixture::ordinary_isr_rejected);
    zassert_equal(solar::metrics::get<fixture::Interrupts>()->value, 1);

    auto descriptor = solar::metrics::catalog::descriptor<fixture::Interrupts>();
    zassert_true(descriptor.has_value());
    zassert_equal(descriptor->get().instrument, solar::metrics::InstrumentKind::Counter);
    zassert_equal(descriptor->get().concurrency, solar::metrics::ConcurrencyKind::Atomic);
    zassert_equal(solar::metrics::catalog::descriptors().size(), 29);

    auto record = solar::metrics::records::metric<fixture::Rejecting>();
    zassert_true(record.has_value());
    zassert_equal(record->rejected, 1);
    auto facility = solar::metrics::records::facility();
    zassert_true(facility.has_value());
    zassert_true(facility->updates > 0);
    zassert_true(facility->rejected > 0);

    std::array<solar::metrics::MetricViewRecord, 5> page_storage{};
    auto page = solar::metrics::records::read({}, page_storage);
    zassert_true(page.has_value());
    zassert_equal(page->written, page_storage.size());
    zassert_true(page->available > page->written);
    auto next = solar::metrics::records::read(page->next, page_storage);
    zassert_true(next.has_value());
    zassert_true(next->written > 0);
}

ZTEST(solar_metrics, test_distribution_overflow_try_contention_and_stale_cursor)
{
    const auto maximum = std::numeric_limits<std::uint64_t>::max();

    zassert_true(solar::metrics::observe<fixture::SaturatingSummary>(maximum).has_value());
    auto saturated = solar::metrics::observe<fixture::SaturatingSummary>(1);
    zassert_true(saturated.has_value());
    zassert_equal(saturated->disposition, solar::metrics::UpdateDisposition::Saturated);
    auto saturated_reading = solar::metrics::get<fixture::SaturatingSummary>();
    zassert_equal(saturated_reading->sum, maximum);
    zassert_equal(saturated_reading->count, 2);
    zassert_true(saturated_reading->degraded);

    zassert_true(solar::metrics::observe<fixture::RejectingSummary>(maximum).has_value());
    auto rejected = solar::metrics::observe<fixture::RejectingSummary>(1);
    zassert_false(rejected.has_value());
    auto rejected_reading = solar::metrics::get<fixture::RejectingSummary>();
    zassert_equal(rejected_reading->sum, maximum);
    zassert_equal(rejected_reading->count, 1);

    zassert_true(solar::metrics::observe<fixture::WrappingSummary>(maximum).has_value());
    auto wrapped = solar::metrics::observe<fixture::WrappingSummary>(1);
    zassert_true(wrapped.has_value());
    zassert_equal(wrapped->disposition, solar::metrics::UpdateDisposition::Wrapped);
    auto wrapped_reading = solar::metrics::get<fixture::WrappingSummary>();
    zassert_equal(wrapped_reading->sum, 0);
    zassert_equal(wrapped_reading->count, 2);

    const auto held =
        fixture::System::MetricFacility::slot<fixture::ThreadSummary>.with_lock_for_test([] {
            auto update = solar::metrics::try_observe<fixture::ThreadSummary>(4);
            zassert_false(update.has_value());
            zassert_equal(update.error().reason, solar::metrics::Reason::WouldBlock);
            auto read = solar::metrics::try_get<fixture::ThreadSummary>();
            zassert_false(read.has_value());
            zassert_equal(read.error().reason, solar::metrics::Reason::WouldBlock);
        });
    zassert_true(held);
    auto contended = solar::metrics::records::metric<fixture::ThreadSummary>();
    zassert_true(contended.has_value());
    zassert_equal(contended->contention, 2);
    zassert_equal(contended->last_failure, solar::metrics::Reason::WouldBlock);

    zassert_true(solar::metrics::observe<fixture::ResettableSummary>(5).has_value());
    constexpr auto reset_id =
        fixture::System::MetricCatalog::Entry<fixture::ResettableSummary>::local_id.value;
    auto before_reset = solar::metrics::get<fixture::ResettableSummary>();
    solar::metrics::Cursor cursor{
        .metric = reset_id, .view = 1, .epoch = before_reset->epoch, .epoch_valid = true};
    zassert_true(solar::metrics::reset<fixture::ResettableSummary>().has_value());
    std::array<solar::metrics::MetricViewRecord, 2> storage{};
    auto page = solar::metrics::records::read(cursor, storage);
    zassert_true(page.has_value());
    zassert_true(page->stale);
    zassert_equal(storage[0].metric.value, reset_id);
    zassert_equal(storage[0].view, solar::metrics::ViewKind::Count);
    zassert_equal(storage[0].epoch, before_reset->epoch + 1);
}

ZTEST(solar_metrics, test_concurrent_metrics_remain_independent_and_coherent)
{
    fixture::concurrent_done.store(0, std::memory_order_relaxed);
    for (std::size_t index = 0; index < fixture::concurrent_threads.size(); ++index) {
        k_thread_create(&fixture::concurrent_threads[index], fixture::concurrent_stacks[index],
                        K_THREAD_STACK_SIZEOF(fixture::concurrent_stacks[index]),
                        fixture::concurrent_producer, nullptr, nullptr, nullptr, K_PRIO_PREEMPT(2),
                        0, K_NO_WAIT);
    }

    while (fixture::concurrent_done.load(std::memory_order_acquire) !=
           fixture::concurrent_threads.size()) {
        auto counter = solar::metrics::get<fixture::ConcurrentCount>();
        auto spin = solar::metrics::get<fixture::ConcurrentSpin>();
        auto mutex = solar::metrics::get<fixture::ConcurrentMutex>();
        zassert_true(counter.has_value());
        zassert_true(spin.has_value());
        zassert_true(mutex.has_value());
        zassert_equal(counter->value, counter->updates);
        zassert_equal(spin->sum, spin->count);
        zassert_equal(mutex->sum, mutex->count);
        k_sleep(K_MSEC(1));
    }

    for (auto& thread : fixture::concurrent_threads) {
        zassert_equal(k_thread_join(&thread, K_FOREVER), 0);
    }
    auto counter = solar::metrics::get<fixture::ConcurrentCount>();
    auto spin = solar::metrics::get<fixture::ConcurrentSpin>();
    auto mutex = solar::metrics::get<fixture::ConcurrentMutex>();
    zassert_equal(counter->value, 800);
    zassert_equal(counter->updates, 800);
    zassert_equal(spin->count, 800);
    zassert_equal(spin->sum, 800);
    zassert_equal(mutex->count, 800);
    zassert_equal(mutex->sum, 800);
}

ZTEST(solar_metrics, test_hot_path_benchmark)
{
    constexpr std::uint64_t iterations = 200'000;
    constexpr std::uint64_t rounds = 5;
    std::uint64_t best = std::numeric_limits<std::uint64_t>::max();
    for (std::uint64_t round{}; round < rounds; ++round) {
        const auto started = fixture::benchmark_now_ns();
        for (std::uint64_t index{}; index < iterations; ++index) {
            (void)solar::metrics::inc<fixture::BenchmarkCounter>();
        }
        best = std::min(best, fixture::benchmark_now_ns() - started);
    }
#if defined(CONFIG_SOLAR_STRICT_CATALOG_BINDING)
    constexpr auto mode = "strict";
#else
    constexpr auto mode = "relaxed";
#endif
    std::size_t slot_bytes{};
    for (const auto& descriptor : solar::metrics::catalog::descriptors()) {
        slot_bytes += descriptor.state_size;
    }
    printk("SOLAR_METRICS_BENCH mode=%s rounds=%llu iterations=%llu best_ns=%llu ns_per_op=%llu\n",
           mode, rounds, iterations, best, best / iterations);
    printk("SOLAR_METRICS_RESOURCE metrics=%zu slot_bytes=%zu facility_record_bytes=%zu\n",
           solar::metrics::catalog::descriptors().size(), slot_bytes,
           sizeof(solar::metrics::FacilityRecord));
    zassert_equal(solar::metrics::get<fixture::BenchmarkCounter>()->value, iterations * rounds);
}

ZTEST(solar_metrics, test_event_adapters_are_deferred_and_composition_owned)
{
    auto observed = solar::events::observe<fixture::FrameDropped>(
        {.bytes = 48, .latency = std::chrono::microseconds{7}});
    zassert_true(observed.has_value());

    for (int attempt = 0; attempt < 50; ++attempt) {
        auto count = solar::metrics::get<fixture::EventCount>();
        if (count && count->value == 1) {
            break;
        }
        k_sleep(K_MSEC(1));
    }

    zassert_equal(solar::metrics::get<fixture::EventCount>()->value, 1);
    zassert_equal(solar::metrics::get<fixture::EventBytes>()->value, 48);
    zassert_equal(solar::metrics::get<fixture::LastEventBytes>()->value, 48);
    auto summary = solar::metrics::get<fixture::EventByteSummary>();
    zassert_equal(summary->count, 1);
    zassert_equal(summary->sum, 48);
    auto event_latency = solar::metrics::get<fixture::EventLatency>();
    zassert_equal(event_latency->count, 1);
    zassert_equal(event_latency->sum, 7);

    zassert_false(solar::events::observe<fixture::Burst>(1)->materialized);
    zassert_false(solar::events::observe<fixture::Burst>(2)->materialized);
    k_sleep(K_MSEC(6));
    auto burst = solar::events::observe<fixture::Burst>(3);
    zassert_true(burst->materialized);
    zassert_equal(burst->occurrence_count, 3);
    for (int attempt = 0; attempt < 50; ++attempt) {
        if (solar::metrics::get<fixture::BurstCount>()->value == 3) {
            break;
        }
        k_sleep(K_MSEC(1));
    }
    zassert_equal(solar::metrics::get<fixture::BurstCount>()->value, 3);

    zassert_true(solar::events::observe<fixture::NarrowEvent>({.value = 300}).has_value());
    for (int attempt = 0; attempt < 50; ++attempt) {
        auto processor =
            solar::events::processor_record<fixture::NarrowAdapter, fixture::NarrowEvent>();
        if (processor && processor->failed == 1) {
            break;
        }
        k_sleep(K_MSEC(1));
    }
    auto processor =
        solar::events::processor_record<fixture::NarrowAdapter, fixture::NarrowEvent>();
    zassert_true(processor.has_value());
    zassert_equal(processor->failed, 1);
    auto narrow = solar::metrics::get<fixture::NarrowGauge>();
    zassert_false(narrow->initialized);
    auto narrow_record = solar::metrics::records::metric<fixture::NarrowGauge>();
    zassert_equal(narrow_record->last_failure, solar::metrics::Reason::ConversionOverflow);
    zassert_equal(narrow_record->rejected, 1);
    zassert_true(narrow_record->failure_at > 0);
}

namespace
{

void* setup()
{
    auto boot = fixture::System::boot();
    zassert_true(boot.has_value());
    return nullptr;
}

void teardown(void*)
{
    auto stopped = fixture::System::stop();
    zassert_true(stopped.has_value());
}

} // namespace

ZTEST_SUITE(solar_metrics, nullptr, setup, nullptr, nullptr, teardown);
