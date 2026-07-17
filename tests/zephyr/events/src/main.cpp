#include <algorithm>
#include <array>
#include <atomic>

#include <zephyr/irq_offload.h>
#include <zephyr/ztest.h>

#include <solar/solar.hpp>

namespace fixture
{

struct FrameDropped
{
    struct Payload
    {
        std::uint32_t bytes{};
        std::uint16_t reason{};
    };

    static constexpr solar::events::Descriptor descriptor{
        .name = "remote.frame-dropped",
        .severity = solar::events::Severity::Warning,
        .domain = solar::events::domain::Communication,
    };
};

struct ControlStarted
{
    using Payload = void;
    static constexpr solar::events::Descriptor descriptor{
        .name = "control.started",
        .severity = solar::events::Severity::Informational,
        .domain = solar::events::domain::Lifecycle,
    };
};

struct IsrPulse
{
    using Payload = std::uint32_t;
    static constexpr solar::events::Descriptor descriptor{
        .name = "device.isr-pulse",
        .severity = solar::events::Severity::Trace,
        .domain = solar::events::domain::Device,
    };
};

struct Sampled
{
    using Payload = std::uint32_t;
    static constexpr solar::events::Descriptor descriptor{
        .name = "resource.sampled",
        .domain = solar::events::domain::Resource,
    };
    using Capture = solar::events::capture::SampleEvery<2>;
};

struct RateLimited
{
    using Payload = std::uint32_t;
    static constexpr solar::events::Descriptor descriptor{
        .name = "communication.rate-limited",
        .domain = solar::events::domain::Communication,
    };
    using Capture = solar::events::capture::RateLimited<solar::events::interval::Milliseconds<20>>;
};

struct Aggregated
{
    using Payload = std::uint32_t;
    static constexpr solar::events::Descriptor descriptor{
        .name = "resource.aggregated",
        .domain = solar::events::domain::Resource,
    };
    using Capture =
        solar::events::capture::AggregateCount<solar::events::interval::Milliseconds<20>>;
};

struct LinkLost
{
    using Payload = std::uint32_t;
    static constexpr solar::events::Descriptor descriptor{
        .name = "communication.link-lost",
        .severity = solar::events::Severity::Error,
        .domain = solar::events::domain::Communication,
    };
};

struct LinkRecovered
{
    using Payload = std::uint32_t;
    using Resolves = LinkLost;
    static constexpr solar::events::Descriptor descriptor{
        .name = "communication.link-recovered",
        .domain = solar::events::domain::Communication,
    };
};

inline std::atomic_uint processed{};
inline std::atomic_uint last_bytes{};
inline std::atomic_bool isr_succeeded{};
inline std::atomic_uint secondary_processed{};
inline std::atomic_uint concurrent_accepted{};
inline std::atomic_uint concurrent_rejected{};
inline std::atomic_uint concurrent_sequence_count{};
inline std::array<solar::events::Sequence, 200> concurrent_sequences{};
inline std::atomic<solar::events::Reason> isr_failure_reason{solar::events::Reason::None};
inline std::atomic<solar::events::Sequence> ordered_isr_sequence{};
inline std::atomic_bool ordered_isr_succeeded{};

K_THREAD_STACK_ARRAY_DEFINE(producer_stacks, 4, 2048);
inline std::array<k_thread, 4> producer_threads{};

struct KeyedAggregate
{
    struct Payload
    {
        std::uint8_t link{};
        std::uint32_t bytes{};
    };

    struct LinkKey
    {
        using Value = std::uint8_t;
        static constexpr Value get(const Payload& payload) noexcept
        {
            return payload.link;
        }
    };

    static constexpr solar::events::Descriptor descriptor{
        .name = "communication.keyed-aggregate",
        .domain = solar::events::domain::Communication,
    };
    using Capture =
        solar::events::capture::AggregateCount<solar::events::interval::Milliseconds<20>, LinkKey,
                                               2>;
};

struct CriticalFault
{
    using Payload = std::uint32_t;
    static constexpr solar::events::Descriptor descriptor{
        .name = "safety.critical-fault",
        .severity = solar::events::Severity::Critical,
        .domain = solar::events::domain::Safety,
    };
    using Retention = solar::events::retention::Critical<1>;
};

struct FakeStore
{
    inline static std::atomic_uint initialized{};
    inline static std::atomic_uint written{};
    inline static solar::events::RecordHeader last{};

    static solar::Result<void> initialize()
    {
        initialized.fetch_add(1, std::memory_order_relaxed);
        return {};
    }

    static solar::Result<void> write(solar::events::RecordView record)
    {
        last = record.header;
        written.fetch_add(1, std::memory_order_release);
        return {};
    }
};

struct PersistentA
{
    using Payload = std::uint32_t;
    static constexpr solar::events::Descriptor descriptor{
        .name = "storage.persistent-a",
        .domain = solar::events::domain::Storage,
        .stable_id = solar::events::Id{0x5001},
    };
    using Retention = solar::events::retention::Persistent<FakeStore>;
};

struct PersistentB
{
    using Payload = std::uint32_t;
    static constexpr solar::events::Descriptor descriptor{
        .name = "storage.persistent-b",
        .domain = solar::events::domain::Storage,
        .stable_id = solar::events::Id{0x5002},
    };
    using Retention = solar::events::retention::Persistent<FakeStore>;
};

struct ProcessorProbe
{
    using Payload = std::uint32_t;
    static constexpr solar::events::Descriptor descriptor{
        .name = "resource.processor-probe",
        .domain = solar::events::domain::Resource,
    };
};

struct ConcurrentPulse
{
    using Payload = std::uint32_t;
    static constexpr solar::events::Descriptor descriptor{
        .name = "resource.concurrent-pulse",
        .domain = solar::events::domain::Resource,
    };
    using Retention = solar::events::retention::Transient;
};

struct OrderedThread
{
    using Payload = std::uint32_t;
    static constexpr solar::events::Descriptor descriptor{.name = "resource.ordered-thread"};
};

struct OrderedIsr
{
    using Payload = std::uint32_t;
    static constexpr solar::events::Descriptor descriptor{.name = "resource.ordered-isr"};
};

struct FailingProcessor
{
    static constexpr solar::component::Descriptor descriptor{.name = "failing-event-processor"};
    using EventRole = solar::events::InfrastructureObserver;
    using EventProcessors =
        solar::events::Processors<solar::events::Process<ProcessorProbe, FailingProcessor>>;
    static solar::Result<void> process(solar::events::RecordView)
    {
        return solar::fail<solar::Error>({.status = solar::Status::Error});
    }
};

struct SecondaryProcessor
{
    static constexpr solar::component::Descriptor descriptor{.name = "secondary-event-processor"};
    using EventRole = solar::events::InfrastructureObserver;
    using EventProcessors =
        solar::events::Processors<solar::events::Process<ProcessorProbe, SecondaryProcessor>>;
    static void process(solar::events::RecordView)
    {
        secondary_processed.fetch_add(1, std::memory_order_release);
    }
};

struct Diagnostics
{
    static constexpr solar::component::Descriptor descriptor{.name = "diagnostics"};
    using EventRole = solar::events::InfrastructureObserver;
    using Events =
        solar::events::Events<FrameDropped, ControlStarted, IsrPulse, Sampled, RateLimited,
                              Aggregated, LinkLost, LinkRecovered, KeyedAggregate, CriticalFault,
                              PersistentA, PersistentB, ProcessorProbe, ConcurrentPulse,
                              OrderedThread, OrderedIsr>;
    using EventProcessors =
        solar::events::Processors<solar::events::Process<FrameDropped, Diagnostics>>;

    static solar::Result<void> process(solar::events::RecordView record)
    {
        auto payload = solar::events::decode<FrameDropped>(record);
        if (!payload) {
            return solar::fail<solar::Error>({.status = payload.error().status});
        }
        last_bytes.store(payload->bytes, std::memory_order_release);
        processed.fetch_add(1, std::memory_order_release);
        return {};
    }
};

using Blueprint =
    solar::Blueprint<solar::Facilities<Diagnostics, FailingProcessor, SecondaryProcessor>>;
using System = solar::System<Blueprint>;

static_assert(System::EventCatalog::size == 16);
static_assert(System::EventProcessorCatalog::size == 3);
static_assert(solar::contains_v<System::EventFacility, typename System::Builtins>);
static_assert(System::ExecutionCatalog::template contains<
              typename System::EventFacility::ProcessorRegistration>);
static_assert(solar::contains_v<System::EventFacility, System::Graph::DependenciesOf<Diagnostics>>);
static_assert(sizeof(decltype(System::EventFacility::event_state<KeyedAggregate>)) >
              sizeof(decltype(System::EventFacility::event_state<ControlStarted>)));
static_assert(sizeof(decltype(System::EventFacility::event_state<CriticalFault>)) >
              sizeof(decltype(System::EventFacility::event_state<ControlStarted>)));

void observe_from_isr(const void*)
{
    auto result = solar::events::try_observe_isr_from<Diagnostics, IsrPulse>(7);
    isr_succeeded.store(result.has_value(), std::memory_order_release);
    isr_failure_reason.store(result ? solar::events::Reason::None : result.error().reason,
                             std::memory_order_release);
}

void produce_concurrently(void* first, void*, void*)
{
    const auto producer = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(first));
    for (std::uint32_t iteration{}; iteration < 50; ++iteration) {
        auto result = solar::events::try_observe<ConcurrentPulse>(producer * 100 + iteration);
        if (result) {
            const auto index = concurrent_sequence_count.fetch_add(1, std::memory_order_relaxed);
            concurrent_sequences[index] = result->sequence;
            concurrent_accepted.fetch_add(1, std::memory_order_relaxed);
        } else {
            zassert_true(result.error().reason == solar::events::Reason::WouldBlock ||
                         result.error().reason == solar::events::Reason::CaptureFull);
            concurrent_rejected.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

void observe_ordered_from_isr(const void*)
{
    auto result = solar::events::try_observe_isr<OrderedIsr>(2);
    ordered_isr_succeeded.store(result.has_value(), std::memory_order_release);
    ordered_isr_sequence.store(result ? result->sequence : 0, std::memory_order_release);
}

} // namespace fixture

SOLAR_BIND_SYSTEM(fixture::System);

static void* setup()
{
    auto booted = fixture::System::boot();
    zassert_true(booted.has_value());
    return nullptr;
}

ZTEST_SUITE(solar_events, nullptr, setup, nullptr, nullptr, nullptr);

ZTEST(solar_events, test_observe_process_and_query_history)
{
    auto observed = solar::events::observe<fixture::FrameDropped>({.bytes = 128, .reason = 2});
    zassert_true(observed.has_value());
    zassert_true(observed->materialized);
    zassert_equal(observed->disposition, solar::events::CaptureDisposition::Captured);

    k_sleep(K_MSEC(10));
    zassert_equal(fixture::processed.load(std::memory_order_acquire), 1);
    zassert_equal(fixture::last_bytes.load(std::memory_order_acquire), 128);

    auto record = solar::events::record<fixture::FrameDropped>();
    zassert_true(record.has_value());
    zassert_equal(record->attempts, 1);
    zassert_equal(record->captured, 1);
    zassert_equal(record->retained, 1);

    auto latest = solar::events::history::latest<fixture::FrameDropped>();
    zassert_true(latest.has_value());
    auto payload = solar::events::decode<fixture::FrameDropped>(latest->view());
    zassert_true(payload.has_value());
    zassert_equal(payload->bytes, 128);

    auto wrong_event = solar::events::decode<fixture::KeyedAggregate>(latest->view());
    zassert_false(wrong_event.has_value());
    zassert_equal(wrong_event.error().reason, solar::events::Reason::DecodeMismatch);

    auto processor = solar::events::processor_record<fixture::Diagnostics, fixture::FrameDropped>();
    zassert_true(processor.has_value());
    zassert_equal(processor->accepted, 1);
}

ZTEST(solar_events, test_payload_free_and_isr_capture)
{
    auto started = solar::events::observe<fixture::ControlStarted>();
    zassert_true(started.has_value());

    irq_offload(fixture::observe_from_isr, nullptr);
    zassert_true(fixture::isr_succeeded.load(std::memory_order_acquire));

    k_sleep(K_MSEC(10));
    auto isr_record = solar::events::record<fixture::IsrPulse>();
    zassert_true(isr_record.has_value());
    zassert_equal(isr_record->captured, 1);
    auto latest = solar::events::history::latest<fixture::IsrPulse>();
    zassert_true(latest.has_value());
    zassert_equal(latest->header.source.kind, solar::events::SourceKind::Component);
}

ZTEST(solar_events, test_policy_capacity_critical_persistence_and_processor_isolation)
{
    zassert_equal(fixture::FakeStore::initialized.load(std::memory_order_acquire), 1);
    zassert_true(solar::events::observe<fixture::PersistentA>(11).has_value());
    zassert_true(solar::events::observe<fixture::PersistentB>(12).has_value());
    zassert_true(solar::events::observe<fixture::ProcessorProbe>(13).has_value());
    k_sleep(K_MSEC(10));
    zassert_equal(fixture::FakeStore::written.load(std::memory_order_acquire), 2);
    zassert_equal(fixture::secondary_processed.load(std::memory_order_acquire), 1);

    auto failed =
        solar::events::processor_record<fixture::FailingProcessor, fixture::ProcessorProbe>();
    auto succeeded =
        solar::events::processor_record<fixture::SecondaryProcessor, fixture::ProcessorProbe>();
    zassert_equal(failed->failed, 1);
    zassert_equal(succeeded->accepted, 1);

    zassert_true(solar::events::observe<fixture::CriticalFault>(21).has_value());
    k_sleep(K_MSEC(10));
    auto critical = solar::events::history::latest<fixture::CriticalFault>();
    zassert_true(critical.has_value());
    zassert_equal(*solar::events::decode<fixture::CriticalFault>(critical->view()), 21);
    auto exhausted = solar::events::try_observe<fixture::CriticalFault>(22);
    zassert_false(exhausted.has_value());
    zassert_equal(exhausted.error().reason, solar::events::Reason::RequiredCaptureExhausted);

    zassert_false(
        solar::events::observe<fixture::KeyedAggregate>({.link = 1, .bytes = 10})->materialized);
    zassert_false(
        solar::events::observe<fixture::KeyedAggregate>({.link = 2, .bytes = 20})->materialized);
    auto key_overflow = solar::events::observe<fixture::KeyedAggregate>({.link = 3, .bytes = 30});
    zassert_false(key_overflow.has_value());
    zassert_equal(key_overflow.error().reason, solar::events::Reason::AggregationKeysFull);
    k_sleep(K_MSEC(25));
    auto materialized = solar::events::observe<fixture::KeyedAggregate>({.link = 1, .bytes = 40});
    zassert_true(materialized->materialized);
    zassert_equal(materialized->occurrence_count, 2);
}

ZTEST(solar_events, test_source_correlation_and_history_eviction)
{
    constexpr solar::events::CorrelationId correlation = 0xA55A;
    auto attributed = solar::events::observe_from<fixture::Diagnostics, fixture::FrameDropped>(
        {.bytes = 200, .reason = 3},
        {.correlation = correlation, .log_intent = solar::events::LogIntent::Force});
    zassert_true(attributed.has_value());
    k_sleep(K_MSEC(5));
    auto latest = solar::events::history::latest<fixture::FrameDropped>();
    zassert_true(latest.has_value());
    zassert_equal(latest->header.source.kind, solar::events::SourceKind::Component);
    zassert_equal(latest->header.correlation, correlation);
    zassert_equal(latest->header.log_intent, solar::events::LogIntent::Force);

    for (std::uint32_t index{}; index < 12; ++index) {
        auto observed =
            solar::events::observe<fixture::FrameDropped>({.bytes = 300 + index, .reason = 4});
        zassert_true(observed.has_value());
        k_sleep(K_MSEC(2));
    }
    k_sleep(K_MSEC(10));
    std::array<solar::events::Record, 3> storage{};
    auto page = solar::events::history::read({.next_sequence = 1}, storage);
    zassert_true(page.stale);
    zassert_true(page.evicted_before > 0);
    zassert_equal(page.written, storage.size());
    zassert_true(page.available >= page.written);
}

ZTEST(solar_events, test_split_ingress_is_merged_by_material_sequence)
{
    k_sched_lock();
    auto first = solar::events::try_observe<fixture::OrderedThread>(1);
    irq_offload(fixture::observe_ordered_from_isr, nullptr);
    auto third = solar::events::try_observe<fixture::OrderedThread>(3);
    k_sched_unlock();

    zassert_true(first.has_value());
    zassert_true(fixture::ordered_isr_succeeded.load(std::memory_order_acquire));
    zassert_true(third.has_value());
    const auto second_sequence = fixture::ordered_isr_sequence.load(std::memory_order_acquire);
    zassert_true(first->sequence < second_sequence);
    zassert_true(second_sequence < third->sequence);

    k_sleep(K_MSEC(10));
    std::array<solar::events::Record, 3> records{};
    auto page = solar::events::history::read({.next_sequence = first->sequence}, records);
    zassert_true(page.written >= 3);
    zassert_equal(records[0].header.sequence, first->sequence);
    zassert_equal(records[1].header.sequence, second_sequence);
    zassert_equal(records[2].header.sequence, third->sequence);
    zassert_equal(records[0].header.context, solar::events::ContextKind::Thread);
    zassert_equal(records[1].header.context, solar::events::ContextKind::Isr);
}

ZTEST(solar_events, test_try_and_concurrent_pressure)
{
    const auto before = solar::events::record<fixture::ConcurrentPulse>();
#if defined(CONFIG_SMP)
    {
        auto guard = fixture::System::EventFacility::lock.acquire();
        auto contended = solar::events::try_observe<fixture::ConcurrentPulse>(1);
        zassert_false(contended.has_value());
        zassert_equal(contended.error().reason, solar::events::Reason::WouldBlock);
    }
#endif

    std::uint32_t pressure_accepted{};
    std::uint32_t pressure_rejected{};
    k_sched_lock();
    for (std::uint32_t index{}; index < CONFIG_SOLAR_EVENTS_INGRESS_DEPTH + 1; ++index) {
        auto pressured = solar::events::try_observe<fixture::ConcurrentPulse>(index);
        pressure_accepted += pressured.has_value();
        pressure_rejected += !pressured.has_value();
    }
    k_sched_unlock();
    zassert_equal(pressure_accepted, CONFIG_SOLAR_EVENTS_INGRESS_DEPTH);
    zassert_equal(pressure_rejected, 1);
    k_sleep(K_MSEC(10));

    fixture::concurrent_accepted.store(0, std::memory_order_relaxed);
    fixture::concurrent_rejected.store(0, std::memory_order_relaxed);
    fixture::concurrent_sequence_count.store(0, std::memory_order_relaxed);
    for (std::size_t index{}; index < fixture::producer_threads.size(); ++index) {
        k_thread_create(&fixture::producer_threads[index], fixture::producer_stacks[index],
                        K_THREAD_STACK_SIZEOF(fixture::producer_stacks[index]),
                        fixture::produce_concurrently,
                        reinterpret_cast<void*>(static_cast<std::uintptr_t>(index)), nullptr,
                        nullptr, 1, 0, K_NO_WAIT);
    }
    for (auto& thread : fixture::producer_threads) {
        zassert_equal(k_thread_join(&thread, K_SECONDS(2)), 0);
    }

    const auto accepted = fixture::concurrent_accepted.load(std::memory_order_acquire);
    const auto rejected = fixture::concurrent_rejected.load(std::memory_order_acquire);
    zassert_equal(accepted + rejected, 200);

    auto sequences = std::span{fixture::concurrent_sequences}.first(accepted);
    std::ranges::sort(sequences);
    for (std::size_t index = 1; index < sequences.size(); ++index) {
        zassert_true(sequences[index] > sequences[index - 1]);
    }

    k_sleep(K_MSEC(20));
    auto record = solar::events::record<fixture::ConcurrentPulse>();
    zassert_equal(record->captured - before->captured, accepted + pressure_accepted);
    zassert_equal(record->ingress_rejected - before->ingress_rejected, pressure_rejected);
    zassert_equal(record->captured + record->ingress_rejected, record->attempts);
}

ZTEST(solar_events, test_zz_shutdown_flushes_pending_aggregates_and_closes_capture)
{
    auto pending = solar::events::observe<fixture::Aggregated>(99);
    zassert_false(pending->materialized);

    auto stopped = fixture::System::stop();
    zassert_true(stopped.has_value());
    auto latest = solar::events::history::latest<fixture::Aggregated>();
    zassert_true(latest.has_value());
    zassert_equal(latest->header.occurrence_count, 1);
    zassert_true((static_cast<std::uint16_t>(latest->header.flags) &
                  static_cast<std::uint16_t>(solar::events::RecordFlag::Aggregated)) != 0);

    auto closed = solar::events::observe<fixture::ControlStarted>();
    zassert_false(closed.has_value());
    zassert_equal(closed.error().reason, solar::events::Reason::NotReady);
}

ZTEST(solar_events, test_capture_policies_recovery_descriptors_and_history_pages)
{
    auto sampled_first = solar::events::observe<fixture::Sampled>(1);
    auto sampled_second = solar::events::observe<fixture::Sampled>(2);
    auto sampled_third = solar::events::observe<fixture::Sampled>(3);
    zassert_equal(sampled_first->disposition, solar::events::CaptureDisposition::Captured);
    zassert_equal(sampled_second->disposition, solar::events::CaptureDisposition::SampledOut);
    zassert_equal(sampled_third->disposition, solar::events::CaptureDisposition::Captured);

    auto rate_first = solar::events::observe<fixture::RateLimited>(1);
    auto rate_second = solar::events::observe<fixture::RateLimited>(2);
    zassert_equal(rate_first->disposition, solar::events::CaptureDisposition::Captured);
    zassert_equal(rate_second->disposition, solar::events::CaptureDisposition::RateLimited);
    k_sleep(K_MSEC(25));
    auto rate_third = solar::events::observe<fixture::RateLimited>(3);
    zassert_equal(rate_third->disposition, solar::events::CaptureDisposition::Captured);

    auto aggregate_first = solar::events::observe<fixture::Aggregated>(1);
    auto aggregate_second = solar::events::observe<fixture::Aggregated>(2);
    zassert_equal(aggregate_first->disposition, solar::events::CaptureDisposition::Aggregated);
    zassert_equal(aggregate_second->occurrence_count, 2);
    k_sleep(K_MSEC(25));
    auto aggregate_third = solar::events::observe<fixture::Aggregated>(3);
    zassert_true(aggregate_third->materialized);
    zassert_equal(aggregate_third->occurrence_count, 3);

    zassert_true(solar::events::observe<fixture::LinkLost>(7).has_value());
    auto secondary_lost =
        solar::events::observe_from<fixture::SecondaryProcessor, fixture::LinkLost>(8);
    zassert_true(secondary_lost.has_value());
    k_sleep(K_MSEC(5));
    auto lost = solar::events::record<fixture::LinkLost>();
    zassert_true(lost->condition_active);
    constexpr auto default_source = solar::events::SourceId{
        .kind = solar::events::SourceKind::Component,
        .component = fixture::System::Catalogs::template Of<solar::component::Tag>::template Entry<
            fixture::Diagnostics>::local_id,
    };
    constexpr auto secondary_source = solar::events::SourceId{
        .kind = solar::events::SourceKind::Component,
        .component = fixture::System::Catalogs::template Of<solar::component::Tag>::template Entry<
            fixture::SecondaryProcessor>::local_id,
    };
    zassert_true(solar::events::condition<fixture::LinkLost>(default_source)->active);
    zassert_true(solar::events::condition<fixture::LinkLost>(secondary_source)->active);
    zassert_true(solar::events::observe<fixture::LinkRecovered>(7).has_value());
    k_sleep(K_MSEC(5));
    auto recovery = solar::events::history::latest<fixture::LinkRecovered>();
    zassert_true((static_cast<std::uint16_t>(recovery->header.flags) &
                  static_cast<std::uint16_t>(solar::events::RecordFlag::Recovery)) != 0);
    lost = solar::events::record<fixture::LinkLost>();
    zassert_true(lost->condition_active);
    zassert_false(solar::events::condition<fixture::LinkLost>(default_source)->active);
    zassert_true(solar::events::condition<fixture::LinkLost>(secondary_source)->active);
    auto secondary_recovered =
        solar::events::observe_from<fixture::SecondaryProcessor, fixture::LinkRecovered>(8);
    zassert_true(secondary_recovered.has_value());
    k_sleep(K_MSEC(5));
    lost = solar::events::record<fixture::LinkLost>();
    zassert_false(lost->condition_active);
    zassert_equal(lost->consecutive, 0);

    auto descriptors = solar::events::descriptors();
    zassert_equal(descriptors.size(), 16);
    auto descriptor = solar::events::descriptor<fixture::Aggregated>();
    zassert_true(descriptor.has_value());
    zassert_equal(descriptor->get().capture, solar::events::CaptureKind::AggregateCount);

    std::array<solar::events::Record, 4> page_storage{};
    auto page = solar::events::history::read({}, page_storage);
    zassert_true(page.written > 0);
    zassert_true(page.next.next_sequence > 1);
}
