#include <array>
#include <atomic>
#include <chrono>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <solar/solar.hpp>

using namespace std::chrono_literals;
using namespace solar::literals;

namespace fixture
{

inline std::atomic_bool imu_connected{true};
inline std::atomic_bool recovery_succeeds{};
inline std::atomic_uint32_t recovery_attempts{};
inline std::atomic_uint32_t safe_state_entries{};
inline std::atomic_uint32_t watchdog_starts{};
inline std::atomic_uint32_t watchdog_feeds{};
inline std::atomic_uint32_t watchdog_stops{};

struct Imu
{
    static constexpr solar::component::Descriptor descriptor{.name = "imu"};

    struct Health
    {
        static solar::Result<solar::health::Assessment> assess()
        {
            return imu_connected.load() ? solar::health::nominal() : solar::health::faulted();
        }

        static solar::Result<void> recover()
        {
            recovery_attempts.fetch_add(1);
            if (!recovery_succeeds.load()) {
                return solar::fail<solar::Error>({.status = solar::Status::Error});
            }
            imu_connected.store(true);
            auto reported = solar::health::report<Imu>(solar::health::nominal());
            return reported ? solar::Result<void>{}
                            : solar::Result<void>{
                                  solar::fail<solar::Error>({.status = reported.error().status})};
        }
    };
};

struct ControlService
{
    static constexpr solar::component::Descriptor descriptor{.name = "control"};
    using Execution = solar::execution::Service<solar::execution::StackSize<2048>>;

    struct Health
    {
        using Checks = solar::health::Checks<solar::health::Progress<10_ms>>;
    };

    static solar::Result<void> run(solar::StopToken stop)
    {
        while (!stop.stop_requested()) {
            k_sleep(K_MSEC(1));
        }
        return {};
    }
};

struct NavigationSafe
{
    static solar::Result<void> enter()
    {
        safe_state_entries.fetch_add(1);
        return {};
    }
};

struct FakeWatchdog
{
    static solar::Result<void> start()
    {
        watchdog_starts.fetch_add(1);
        return {};
    }

    static solar::Result<void> feed()
    {
        watchdog_feeds.fetch_add(1);
        return {};
    }

    static solar::Result<void> stop()
    {
        watchdog_stops.fetch_add(1);
        return {};
    }
};

using Supervision = solar::supervisor::Policy<
    solar::supervisor::OnFault<Imu, solar::supervisor::Warn, solar::supervisor::TryRecover<Imu>>,
    solar::supervisor::OnRecoveryFailure<Imu, solar::supervisor::EnterSafeState<NavigationSafe>>,
    solar::supervisor::OnStall<ControlService, solar::supervisor::Latch,
                               solar::supervisor::StopFeedingWatchdog>>;

using Blueprint = solar::Blueprint<
    solar::Devices<Imu>, solar::Services<ControlService>,
    solar::supervisor::Configuration<Supervision, solar::supervisor::Watchdog<FakeWatchdog>>>;
using System = solar::System<Blueprint>;
using Supervisor = typename System::SupervisorService;

} // namespace fixture

SOLAR_BIND_SYSTEM(fixture::System);

static void* setup_supervisor()
{
    auto boot = fixture::System::boot();
    zassert_true(boot.has_value());
    for (std::size_t attempt = 0; attempt < 100; ++attempt) {
        auto state = solar::supervisor::state();
        if (state && state->cycles > 0) {
            break;
        }
        k_sleep(K_MSEC(1));
    }
    auto state = solar::supervisor::state();
    zassert_true(state.has_value());
    zassert_true(state->cycles > 0);
    return nullptr;
}

static void teardown_supervisor(void*)
{
    auto stopped = fixture::System::stop();
    zassert_true(stopped.has_value());
    zassert_equal(fixture::watchdog_stops.load(), 1);
}

ZTEST(solar_supervisor, test_policy_recovery_escalation_stall_and_watchdog_gate)
{
    zassert_equal(fixture::watchdog_starts.load(), 1);
    const auto initial_feeds = fixture::watchdog_feeds.load();
    zassert_true(initial_feeds > 0);

    fixture::imu_connected.store(false);
    fixture::recovery_succeeds.store(false);
    const auto first_time = solar::kernel::now_ticks();
    auto failed_recovery =
        solar::supervisor::detail::cycle<fixture::Supervisor, fixture::System>(first_time);
    zassert_true(failed_recovery.has_value());
    zassert_equal(fixture::recovery_attempts.load(), 1);
    zassert_equal(fixture::safe_state_entries.load(), 1);

    auto imu_response = solar::supervisor::record<fixture::Imu>();
    zassert_true(imu_response.has_value());
    zassert_equal(imu_response->last_action, solar::supervisor::Action::EnterSafeState);
    zassert_equal(imu_response->last_outcome, solar::supervisor::Outcome::Succeeded);

    fixture::recovery_succeeds.store(true);
    const auto retry_time = first_time + solar::kernel::to_ticks_ceil(20ms);
    auto recovered =
        solar::supervisor::detail::cycle<fixture::Supervisor, fixture::System>(retry_time);
    zassert_true(recovered.has_value());
    zassert_equal(fixture::recovery_attempts.load(), 2);
    zassert_true(fixture::imu_connected.load());

    k_sleep(K_MSEC(30));
    const auto before_withhold = fixture::watchdog_feeds.load();
    auto stalled = solar::supervisor::detail::cycle<fixture::Supervisor, fixture::System>(
        solar::kernel::now_ticks());
    zassert_true(stalled.has_value());
    auto control_response = solar::supervisor::record<fixture::ControlService>();
    zassert_true(control_response.has_value());
    zassert_true(control_response->latched);
    zassert_equal(control_response->last_action, solar::supervisor::Action::StopFeedingWatchdog);
    zassert_equal(fixture::watchdog_feeds.load(), before_withhold);
    auto watchdog = solar::supervisor::watchdog();
    zassert_true(watchdog.has_value());
    zassert_true(watchdog->deliberately_withheld);

    std::array<solar::supervisor::ResponseRecord, 8> responses{};
    auto page = solar::supervisor::responses({}, responses);
    zassert_true(page.has_value());
    zassert_true(page->written >= 5);

    const auto starved_feeds = fixture::watchdog_feeds.load();
    k_sleep(K_MSEC(20));
    zassert_equal(fixture::watchdog_feeds.load(), starved_feeds);

    auto before_wake = solar::supervisor::state();
    zassert_true(before_wake.has_value());
    zassert_true(solar::health::report<fixture::Imu>(solar::health::nominal()).has_value());
    for (std::size_t attempt = 0; attempt < 100; ++attempt) {
        auto current = solar::supervisor::state();
        if (current && current->cycles > before_wake->cycles) {
            break;
        }
        k_sleep(K_MSEC(1));
    }
    auto after_wake = solar::supervisor::state();
    zassert_true(after_wake.has_value());
    zassert_true(after_wake->cycles > before_wake->cycles);
}

ZTEST_SUITE(solar_supervisor, nullptr, setup_supervisor, nullptr, nullptr, teardown_supervisor);
