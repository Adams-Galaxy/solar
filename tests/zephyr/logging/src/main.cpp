#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <string_view>

#include <zephyr/irq_offload.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include <solar/solar.hpp>

LOG_MODULE_REGISTER(solar_logging_fixture, LOG_LEVEL_DBG);

namespace fixture
{

struct ParserSource
{
    static constexpr solar::log::SourceDescriptor descriptor{
        .name = "fixture.parser",
    };
};

struct LinkLost
{
    using Payload = std::uint32_t;
    static constexpr solar::events::Descriptor descriptor{.name = "link-lost"};
};

struct Producer
{
    static constexpr solar::component::Descriptor descriptor{
        .name = "fixture.producer",
    };
    using LogSources = solar::log::Sources<ParserSource>;
    using Events = solar::events::Events<LinkLost>;
    using EventLogs = solar::events::log::Adapters<
        solar::events::log::On<LinkLost, Producer, solar::log::Level::Warning>>;
};

struct MemorySink
{
    static constexpr solar::log::SinkDescriptor descriptor{.name = "fixture.memory-sink"};
    inline static std::array<std::array<char, 256>, 512> messages{};
    inline static std::array<solar::log::RecordHeader, 512> headers{};
    inline static std::atomic_size_t count{};

    static solar::Result<void> init() noexcept
    {
        count.store(0, std::memory_order_release);
        return {};
    }

    static solar::Result<void> consume(solar::log::RecordView record,
                                       std::string_view rendered) noexcept
    {
        const auto index = count.fetch_add(1, std::memory_order_acq_rel);
        if (index >= messages.size()) {
            return solar::fail<solar::Error>({.status = solar::Status::NoSpace});
        }
        headers[index] = record.header;
        const auto copied = std::min(rendered.size(), messages[index].size() - 1);
        std::memcpy(messages[index].data(), rendered.data(), copied);
        messages[index][copied] = '\0';
        return {};
    }
};

struct FailingSink
{
    static constexpr solar::log::SinkDescriptor descriptor{.name = "fixture.failing-sink"};
    inline static std::atomic_size_t attempts{};

    static solar::Result<void> consume(solar::log::RecordView, std::string_view) noexcept
    {
        attempts.fetch_add(1, std::memory_order_relaxed);
        return solar::fail<solar::Error>({.status = solar::Status::Error});
    }
};

struct CapturingConsoleRenderer
{
    inline static std::array<char, 256> message{};
    inline static std::atomic_size_t count{};

    static solar::Result<void> render(solar::log::RecordView, std::string_view rendered) noexcept
    {
        const auto copied = std::min(rendered.size(), message.size() - 1);
        std::memcpy(message.data(), rendered.data(), copied);
        message[copied] = '\0';
        count.fetch_add(1, std::memory_order_release);
        return {};
    }
};

using CapturingConsole = solar::log::ZephyrConsole<CapturingConsoleRenderer>;

using System = solar::System<solar::Blueprint<
    solar::Facilities<Producer>,
    solar::log::Configuration<solar::log::Sinks<
        solar::log::To<MemorySink, solar::log::MinimumLevel<solar::log::Level::Debug>,
                       solar::log::panic::Safe>,
        solar::log::To<CapturingConsole, solar::log::MinimumLevel<solar::log::Level::Debug>>,
        solar::log::To<solar::log::RetainedHistory,
                       solar::log::MinimumLevel<solar::log::Level::Notice>>,
        solar::log::To<FailingSink, solar::log::MinimumLevel<solar::log::Level::Error>>>>>>;

inline bool isr_captured{};
inline bool ordinary_rejected{};
inline std::atomic_uint producers_done{};
K_THREAD_STACK_ARRAY_DEFINE(producer_stacks, 4, 4096);
inline std::array<k_thread, 4> producer_threads{};

void log_from_isr(const void*)
{
    isr_captured = solar::log::try_warn_isr<Producer>("IRQ count {}", 7U).has_value();
    auto ordinary = solar::log::warn<Producer>("wrong context");
    ordinary_rejected = !ordinary && ordinary.error().reason == solar::log::Reason::InvalidContext;
}

void concurrent_producer(void* first, void*, void*)
{
    const auto producer = reinterpret_cast<std::uintptr_t>(first);
    for (std::uint32_t index{}; index < 50; ++index) {
        while (!solar::log::info<Producer>("producer {} value {}", producer, index)) {
            k_yield();
        }
    }
    producers_done.fetch_add(1, std::memory_order_release);
}

} // namespace fixture

SOLAR_BIND_SYSTEM(fixture::System);

static void* setup_suite()
{
    solar::log::platform::reset_for_test();
    LOG_INF("early platform %d", 12);
    auto boot = fixture::System::boot();
    zassert_true(boot.has_value());
    zassert_true(solar::log::flush().has_value());
    return nullptr;
}

ZTEST(solar_logging, test_sources_formatting_and_deferred_sink)
{
    static_assert(fixture::System::LogSourceCatalog::contains<fixture::Producer>);
    static_assert(fixture::System::LogSourceCatalog::contains<fixture::ParserSource>);
    static_assert(fixture::System::LogDomainCatalog::contains<solar::log::domain::Transport>);

    const auto before = fixture::MemorySink::count.load(std::memory_order_acquire);
    const auto console_before =
        fixture::CapturingConsoleRenderer::count.load(std::memory_order_acquire);
    auto first = solar::log::notice<fixture::Producer>(solar::log::correlated(77),
                                                       "value {} hex {:#x} ok {}", -4, 42U, true);
    auto second = solar::log::info<fixture::ParserSource, solar::log::domain::Transport>(
        "peer {} gain {:.2f}", std::string_view{"host"}, 1.25);
    zassert_true(first.has_value());
    zassert_true(second.has_value());
    zassert_equal(first->disposition, solar::log::Disposition::Captured);
    zassert_true(solar::log::flush().has_value());

    const auto after = fixture::MemorySink::count.load(std::memory_order_acquire);
    zassert_true(after >= before + 2);
    zassert_true(std::string_view{fixture::MemorySink::messages[after - 2].data()}.find(
                     "value -4 hex 0x2a ok true") != std::string_view::npos);
    zassert_true(std::string_view{fixture::MemorySink::messages[after - 1].data()}.find(
                     "peer host gain 1.25") != std::string_view::npos);
    zassert_equal(fixture::MemorySink::headers[after - 2].correlation, 77);
    zassert_equal(solar::log::detail::source_name<fixture::System>(
                      {.header = fixture::MemorySink::headers[after - 1]}),
                  "fixture.parser");
    zassert_true(std::string_view{fixture::CapturingConsoleRenderer::message.data()}.find(
                     "peer host gain 1.25") != std::string_view::npos);
    zassert_true(fixture::CapturingConsoleRenderer::count.load(std::memory_order_acquire) >=
                 console_before + 2);
    zassert_true(solar::log::source_record<fixture::Producer>().captured > 0);
    auto sink = solar::log::sink_record<fixture::MemorySink>();
    zassert_true(sink.has_value());
    zassert_true(sink->accepted > 0);
}

ZTEST(solar_logging, test_copied_text_and_hexdump_rendering)
{
    const auto before = fixture::MemorySink::count.load(std::memory_order_acquire);
    auto copied = solar::log::text<fixture::Producer>(solar::log::Level::Info, "dynamic text");
    const std::array<std::byte, 3> bytes{std::byte{0x01}, std::byte{0xab}, std::byte{0xff}};
    auto dumped = solar::log::hexdump<fixture::Producer>(solar::log::Level::Info, "frame", bytes);
    zassert_true(copied.has_value());
    zassert_true(dumped.has_value());
    zassert_true(solar::log::flush().has_value());
    const auto after = fixture::MemorySink::count.load(std::memory_order_acquire);
    zassert_true(after >= before + 2);
    zassert_equal(std::string_view{fixture::MemorySink::messages[after - 2].data()},
                  "dynamic text");
    zassert_equal(std::string_view{fixture::MemorySink::messages[after - 1].data()},
                  "frame: 01 ab ff");
}

ZTEST(solar_logging, test_history_threshold_and_paged_query)
{
    (void)solar::log::debug<fixture::Producer>("debug only");
    (void)solar::log::notice<fixture::Producer>("retained {}", 9);
    zassert_true(solar::log::flush().has_value());

    std::array<solar::log::Record, 16> records{};
    auto page = solar::log::history({}, records);
    zassert_true(page.written > 0);
    for (std::size_t index{}; index < page.written; ++index) {
        zassert_true(solar::log::at_least(records[index].header.level, solar::log::Level::Notice));
    }
    auto latest = solar::log::latest();
    zassert_true(latest.has_value());
    zassert_equal(latest->header.level, solar::log::Level::Notice);
}

ZTEST(solar_logging, test_history_replay_reports_eviction_without_storing_rendered_strings)
{
    for (std::uint32_t index{}; index < 24; ++index) {
        zassert_true(solar::log::notice<fixture::Producer>("bring-up {}", index).has_value());
        zassert_true(solar::log::flush().has_value());
    }

    const auto accounting = solar::log::record();
    zassert_true(accounting.history_evicted > 0);
    zassert_equal(accounting.history_unstored, 0);

    std::array<solar::log::Record, 2> scratch{};
    const auto before = fixture::MemorySink::count.load(std::memory_order_acquire);
    auto replayed = solar::log::replay<fixture::MemorySink>({}, scratch);
    zassert_true(replayed.has_value());
    zassert_true(replayed->stale);
    zassert_true(replayed->evicted_before > 0);
    zassert_equal(replayed->written, scratch.size());
    zassert_equal(fixture::MemorySink::count.load(std::memory_order_acquire),
                  before + scratch.size());
}

ZTEST(solar_logging, test_event_adapter_preserves_event_origin)
{
    const auto before = fixture::MemorySink::count.load(std::memory_order_acquire);
    auto observed = solar::events::observe<fixture::LinkLost>(5U);
    zassert_true(observed.has_value());
    for (int attempt = 0;
         attempt < 20 && fixture::MemorySink::count.load(std::memory_order_acquire) == before;
         ++attempt) {
        k_sleep(K_MSEC(1));
    }
    zassert_true(solar::log::flush().has_value());
    const auto after = fixture::MemorySink::count.load(std::memory_order_acquire);
    zassert_true(after > before);
    zassert_equal(fixture::MemorySink::headers[after - 1].origin, solar::log::Origin::Event);
}

ZTEST(solar_logging, test_concurrent_mpsc_capture)
{
    fixture::producers_done.store(0, std::memory_order_release);
    const auto before = solar::log::record().captured;
    for (std::size_t index{}; index < fixture::producer_threads.size(); ++index) {
        k_thread_create(&fixture::producer_threads[index], fixture::producer_stacks[index],
                        K_THREAD_STACK_SIZEOF(fixture::producer_stacks[index]),
                        fixture::concurrent_producer,
                        reinterpret_cast<void*>(static_cast<std::uintptr_t>(index + 1)), nullptr,
                        nullptr, K_PRIO_PREEMPT(2), 0, K_NO_WAIT);
    }
    for (auto& thread : fixture::producer_threads) {
        zassert_equal(k_thread_join(&thread, K_SECONDS(1)), 0);
    }
    zassert_equal(fixture::producers_done.load(std::memory_order_acquire), 4);
    zassert_true(solar::log::flush().has_value());
    zassert_true(solar::log::record().captured - before >= 200);
}

ZTEST(solar_logging, test_isr_capture_and_context_enforcement)
{
    fixture::isr_captured = false;
    fixture::ordinary_rejected = false;
    irq_offload(fixture::log_from_isr, nullptr);
    zassert_true(fixture::isr_captured);
    zassert_true(fixture::ordinary_rejected);
    zassert_true(solar::log::flush().has_value());
}

ZTEST(solar_logging, test_zephyr_frontend_and_sink_isolation)
{
    const auto before = fixture::MemorySink::count.load(std::memory_order_acquire);
    LOG_ERR("zephyr failure %u", 33U);
    auto flushed = solar::log::flush();
    zassert_false(flushed.has_value());
    const auto after = fixture::MemorySink::count.load(std::memory_order_acquire);
    zassert_true(after > before);
    zassert_equal(fixture::MemorySink::headers[after - 1].origin, solar::log::Origin::Zephyr);
    zassert_true(fixture::FailingSink::attempts.load(std::memory_order_acquire) > 0);
    zassert_true(solar::log::record().sink_failures > 0);
}

ZTEST(solar_logging, test_priority_reservation_preserves_warning_capacity)
{
    using Facility = fixture::System::LogFacility;
    Facility::test_hold_processor.store(true, std::memory_order_release);
    std::size_t ordinary{};
    while (solar::log::try_info<fixture::Producer>("fill {}", ordinary).has_value()) {
        ++ordinary;
        if (ordinary > 128) {
            break;
        }
    }
    auto warning = solar::log::try_warn<fixture::Producer>("reserved warning");
    zassert_true(warning.has_value());
    zassert_equal(warning->disposition, solar::log::Disposition::Elevated);
    Facility::test_hold_processor.store(false, std::memory_order_release);
    zassert_true(solar::log::flush().has_value());
    zassert_true(solar::log::record().dropped > 0);
}

ZTEST(solar_logging, test_runtime_source_domain_and_sink_filters)
{
    zassert_true(
        solar::log::set_source_level<fixture::Producer>(solar::log::Level::Warning).has_value());
    auto filtered = solar::log::info<fixture::Producer>("filtered source");
    zassert_true(filtered.has_value());
    zassert_equal(filtered->disposition, solar::log::Disposition::RuntimeFiltered);
    auto accepted = solar::log::warn<fixture::Producer>("accepted source");
    zassert_true(accepted.has_value());
    zassert_true(
        solar::log::set_source_level<fixture::Producer>(solar::log::Level::Trace).has_value());

    zassert_true(
        solar::log::set_domain_level<solar::log::domain::Transport>(solar::log::Level::Error)
            .has_value());
    auto domain_filtered =
        solar::log::notice<fixture::Producer, solar::log::domain::Transport>("filtered domain");
    zassert_true(domain_filtered.has_value());
    zassert_equal(domain_filtered->disposition, solar::log::Disposition::RuntimeFiltered);
    zassert_true(
        solar::log::set_domain_level<solar::log::domain::Transport>(solar::log::Level::Trace)
            .has_value());

    zassert_true(
        solar::log::set_sink_level<fixture::MemorySink>(solar::log::Level::Error).has_value());
    const auto before = fixture::MemorySink::count.load(std::memory_order_acquire);
    (void)solar::log::notice<fixture::Producer>("history not memory");
    zassert_true(solar::log::flush().has_value());
    zassert_equal(fixture::MemorySink::count.load(std::memory_order_acquire), before);
    zassert_true(
        solar::log::set_sink_level<fixture::MemorySink>(solar::log::Level::Debug).has_value());
}

ZTEST(solar_logging, test_zz_panic_drains_only_panic_safe_routes)
{
    using Facility = fixture::System::LogFacility;
    Facility::test_hold_processor.store(true, std::memory_order_release);
    const auto safe_before = fixture::MemorySink::count.load(std::memory_order_acquire);
    const auto unsafe_before = fixture::FailingSink::attempts.load(std::memory_order_acquire);
    (void)solar::log::error<fixture::Producer>("panic pending");
    Facility::test_hold_processor.store(false, std::memory_order_release);
    solar::log::platform::panic_for_test();
    zassert_true(solar::log::record().panic);
    zassert_true(fixture::MemorySink::count.load(std::memory_order_acquire) > safe_before);
    zassert_equal(fixture::FailingSink::attempts.load(std::memory_order_acquire), unsafe_before);
}

ZTEST_SUITE(solar_logging, nullptr, setup_suite, nullptr, nullptr, nullptr);
