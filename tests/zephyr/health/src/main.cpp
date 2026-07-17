#include <array>
#include <atomic>
#include <chrono>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <solar/remote/testing/in_memory_link.hpp>
#include <solar/solar.hpp>

using namespace std::chrono_literals;
using namespace solar::literals;

namespace fixture
{

inline std::atomic_bool connected{true};
inline std::atomic_bool assessment_available{true};

struct Connection
{
    static constexpr solar::health::CheckDescriptor descriptor{
        .name = "connection",
        .description = "IMU transport connection",
        .required = true,
    };
    static constexpr bool required = true;

    static solar::Result<solar::health::Observation> check()
    {
        return solar::health::Observation{
            .assessment = connected.load() ? solar::health::nominal() : solar::health::faulted(),
            .quality = solar::health::EvidenceQuality::Direct,
            .availability = solar::health::SourceAvailability::Available,
            .required = true,
        };
    }
};

struct Imu
{
    static constexpr solar::component::Descriptor descriptor{.name = "imu"};

    struct Health
    {
        using Checks = solar::health::Checks<Connection, solar::health::Progress<20_ms>>;

        static solar::Result<solar::health::Assessment> assess()
        {
            if (!assessment_available.load()) {
                return solar::fail<solar::Error>({.status = solar::Status::Busy});
            }
            return connected.load() ? solar::health::nominal() : solar::health::degraded();
        }
    };
};

struct ControlService
{
    static constexpr solar::component::Descriptor descriptor{.name = "control"};
    using Execution =
        solar::execution::Service<solar::execution::StackSize<2048>, solar::execution::Priority<2>>;

    struct Health
    {
        using Checks =
            solar::health::Checks<solar::health::Progress<20_ms>, solar::health::StackMargin<2048>>;
    };

    static solar::Result<void> run(solar::StopToken stop)
    {
        while (!stop.stop_requested()) {
            k_sleep(K_MSEC(1));
        }
        return {};
    }
};

struct TestLink : solar::remote::testing::InMemoryLink<TestLink, 256, 256>
{
    static constexpr solar::remote::LinkDescriptor descriptor{
        .id = solar::remote::LinkId{0x7801},
        .name = "health.memory",
    };
};

struct Root
{
    static constexpr solar::component::Descriptor descriptor{.name = "root"};
    using RemoteLinks = solar::remote::ContributeLinks<TestLink>;
};

using Blueprint =
    solar::Blueprint<solar::Devices<Imu>, solar::Facilities<Root>, solar::Services<ControlService>>;
using System = solar::System<Blueprint>;
using RemoteService = typename System::RemoteService;
using LinkState = solar::remote::detail::LinkState<RemoteService, TestLink, 0>;

static void concurrent_reporter(void* argument) noexcept
{
    const bool nominal = argument != nullptr;
    for (std::size_t index = 0; index < 64; ++index) {
        auto result = solar::health::report<Imu>(nominal ? solar::health::nominal()
                                                         : solar::health::degraded());
        zassert_true(result.has_value());
    }
}

} // namespace fixture

SOLAR_BIND_SYSTEM(fixture::System);

static void* setup_health()
{
    auto boot = fixture::System::boot();
    zassert_true(boot.has_value());
    return nullptr;
}

static void teardown_health(void*)
{
    auto stopped = fixture::System::stop();
    zassert_true(stopped.has_value());
}

ZTEST(solar_health, test_component_contract_and_catalog)
{
    fixture::connected.store(true);
    fixture::assessment_available.store(true);

    auto assessed = solar::health::assess<fixture::Imu>();
    zassert_true(assessed.has_value());

    auto checked = solar::health::check<fixture::Imu, fixture::Connection>();
    zassert_true(checked.has_value());

    auto progressed = solar::health::progress<fixture::Imu>();
    zassert_true(progressed.has_value());

    auto refreshed = solar::health::refresh();
    zassert_true(refreshed.has_value());

    auto record = solar::health::record<fixture::Imu>();
    zassert_true(record.has_value());
    zassert_equal(record->condition, solar::health::Condition::Nominal);
}

ZTEST(solar_health, test_imu_failure_recovery_and_history)
{
    fixture::connected.store(false);
    auto assessed = solar::health::assess<fixture::Imu>();
    zassert_true(assessed.has_value());
    auto checked = solar::health::check<fixture::Imu, fixture::Connection>();
    zassert_true(checked.has_value());

    auto failed = solar::health::record<fixture::Imu>();
    zassert_true(failed.has_value());
    zassert_equal(failed->condition, solar::health::Condition::Faulted);
    zassert_true(failed->primary_evidence.has_value());

    fixture::assessment_available.store(false);
    auto unavailable = solar::health::assess<fixture::Imu>();
    zassert_false(unavailable.has_value());
    zassert_equal(unavailable.error().reason, solar::health::Reason::SourceFailed);

    fixture::assessment_available.store(true);
    fixture::connected.store(true);
    zassert_true(solar::health::assess<fixture::Imu>().has_value());
    auto recovered_check = solar::health::check<fixture::Imu, fixture::Connection>();
    zassert_true(recovered_check.has_value());
    zassert_true(solar::health::report<fixture::Imu>(solar::health::nominal()).has_value());

    auto recovered = solar::health::record<fixture::Imu>();
    zassert_true(recovered.has_value());
    zassert_equal(recovered->condition, solar::health::Condition::Nominal);

    fixture::assessment_available.store(false);
    zassert_false(solar::health::assess<fixture::Imu>().has_value());
    auto unknown = solar::health::record<fixture::Imu>();
    zassert_true(unknown.has_value());
    zassert_equal(unknown->condition, solar::health::Condition::Unknown);
    fixture::assessment_available.store(true);
    zassert_true(solar::health::assess<fixture::Imu>().has_value());

    std::array<solar::health::TransitionRecord, 8> entries{};
    auto page = solar::health::history::read({}, entries);
    zassert_true(page.has_value());
    zassert_true(page->written >= 2);
}

ZTEST(solar_health, test_concurrent_reports_and_bounded_history)
{
    solar::kernel::Thread<2048> nominal_thread;
    solar::kernel::Thread<2048> degraded_thread;
    zassert_true(nominal_thread
                     .launch(&fixture::concurrent_reporter, &fixture::connected,
                             {.priority = solar::kernel::Priority::preemptive<2>(),
                              .name = "health-nominal"})
                     .has_value());
    zassert_true(degraded_thread
                     .launch(&fixture::concurrent_reporter, nullptr,
                             {.priority = solar::kernel::Priority::preemptive<2>(),
                              .name = "health-degraded"})
                     .has_value());
    zassert_true(nominal_thread.join(solar::kernel::Timeout::after(500ms)).has_value());
    zassert_true(degraded_thread.join(solar::kernel::Timeout::after(500ms)).has_value());

    zassert_true(solar::health::report<fixture::Imu>(solar::health::nominal()).has_value());
    auto coherent = solar::health::record<fixture::Imu>();
    zassert_true(coherent.has_value());
    zassert_true(coherent->report_count >= 129);

    for (std::size_t index = 0; index < 12; ++index) {
        auto assessment = (index % 2) == 0 ? solar::health::degraded() : solar::health::nominal();
        zassert_true(solar::health::report<fixture::Imu>(assessment).has_value());
    }

    std::array<solar::health::TransitionRecord, 8> entries{};
    auto page = solar::health::history::read({}, entries);
    zassert_true(page.has_value());
    zassert_true(page->overwritten > 0);
    zassert_equal(page->written, entries.size());
}

ZTEST(solar_health, test_progress_stall_and_stack_warning)
{
    zassert_true(solar::health::progress<fixture::ControlService>().has_value());
    zassert_true(solar::health::refresh().has_value());

    auto monitors = solar::health::monitors<fixture::ControlService>();
    bool saw_stack_warning{};
    for (const auto& monitor : monitors) {
        if (monitor.descriptor.descriptor.kind == solar::health::MonitorKind::StackMargin) {
            saw_stack_warning =
                monitor.has_observation &&
                monitor.observation.assessment.condition == solar::health::Condition::Degraded;
        }
    }
    zassert_true(saw_stack_warning);

    k_sleep(K_MSEC(45));
    zassert_true(solar::health::refresh().has_value());
    auto stalled = solar::health::record<fixture::ControlService>();
    zassert_true(stalled.has_value());
    zassert_equal(stalled->condition, solar::health::Condition::Faulted);
    zassert_equal(stalled->liveness, solar::health::Liveness::Stalled);
}

ZTEST(solar_health, test_remote_degradation_and_isr_ingress)
{
    fixture::LinkState::protocol_errors.store(3, std::memory_order_release);
    zassert_true(solar::health::refresh().has_value());
    auto remote = solar::health::record<fixture::RemoteService>();
    zassert_true(remote.has_value());
    zassert_equal(remote->condition, solar::health::Condition::Degraded);
    zassert_true(remote->primary_evidence.has_value());
    zassert_equal(remote->primary_evidence->source_kind, solar::health::SourceKind::Remote);

    solar::health::CompactObservation compact{
        .condition = solar::health::Condition::Degraded,
        .status = solar::Status::Error,
    };
    zassert_true(solar::health::try_report_isr_from<fixture::Imu>(compact).has_value());
    zassert_true(solar::health::try_report_isr_from<fixture::Imu>(compact).has_value());
    auto overflow = solar::health::try_report_isr_from<fixture::Imu>(compact);
    zassert_false(overflow.has_value());
    zassert_equal(overflow.error().reason, solar::health::Reason::IngressFull);

    zassert_true(solar::health::refresh().has_value());
    auto state = solar::health::state();
    zassert_true(state.has_value());
    zassert_equal(state->isr_reports_admitted, 2);
    zassert_equal(state->isr_reports_dropped, 1);
}

ZTEST_SUITE(solar_health, nullptr, setup_health, nullptr, nullptr, teardown_health);
