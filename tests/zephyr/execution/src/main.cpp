#include <atomic>
#include <chrono>

#include <zephyr/irq_offload.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <solar/solar.hpp>

using namespace std::chrono_literals;
using namespace solar::literals;

namespace fixture
{

inline std::atomic_uint system_runs{};
inline std::atomic_uint owned_runs{};
inline std::atomic_uint counted_runs{};
inline std::atomic_uint delayed_runs{};
inline std::atomic_uint periodic_runs{};
inline std::atomic_uint poll_runs{};
inline std::atomic_uint blocking_runs{};
inline std::atomic_uint token_runs{};
inline std::atomic_bool service_started{};
inline std::atomic_bool service_saw_running{};
inline std::atomic_bool blocking_started{};
inline std::atomic_bool blocking_release{};
inline std::atomic_bool isr_submit_ok{};
inline std::atomic_bool isr_receipt_marked{};
inline std::atomic_bool token_stop_possible{};
inline std::atomic<k_tid_t> owned_thread{};

struct Resource
{
    static constexpr solar::component::Descriptor descriptor{.name = "resource"};
};

struct SystemBehavior
{
    static void execute()
    {
        ++system_runs;
    }
};

struct OwnedBehavior
{
    static solar::Status execute()
    {
        owned_thread.store(k_current_get(), std::memory_order_release);
        ++owned_runs;
        return solar::Status::Ok;
    }
};

struct CountedBehavior
{
    static solar::Result<void> execute()
    {
        ++counted_runs;
        return {};
    }
};

struct DelayedBehavior
{
    static void execute()
    {
        ++delayed_runs;
    }
};

struct BlockingBehavior
{
    static void execute()
    {
        blocking_started.store(true, std::memory_order_release);
        while (!blocking_release.load(std::memory_order_acquire)) {
            k_sleep(K_MSEC(1));
        }
        ++blocking_runs;
    }
};

struct TokenBehavior
{
    static void execute(solar::StopToken stop)
    {
        token_stop_possible.store(stop.stop_possible(), std::memory_order_release);
        ++token_runs;
    }
};

struct FailingBehavior
{
    static solar::Status execute()
    {
        return solar::Status::Error;
    }
};

struct PeriodicBehavior
{
    static void execute()
    {
        ++periodic_runs;
    }
};

struct PollInput
{
    static solar::kernel::PollSignal& signal()
    {
        static solar::kernel::PollSignal value;
        return value;
    }

    static solar::kernel::PollSet<1>& events()
    {
        static solar::kernel::PollSet<1> value;
        return value;
    }

    static void configure()
    {
        zassert_equal(events().add(signal()), solar::Status::Ok);
    }
};

struct PollBehavior
{
    static void execute()
    {
        ++poll_runs;
        PollInput::signal().reset();
    }
};

using ControlQueue = solar::execution::WorkQueue<"control-work", solar::execution::StackSize<2048>,
                                                 solar::execution::Priority<2>,
                                                 solar::execution::StopTimeout<200_ms>>;

using SystemTask =
    solar::execution::OnDemand<"system-task", SystemBehavior, solar::execution::SystemWorkQueue,
                               solar::execution::DependsOn<Resource>>;
using OwnedTask = solar::execution::OnDemand<"owned-task", OwnedBehavior, ControlQueue,
                                             solar::execution::DependsOn<Resource>>;
using CountedTask = solar::execution::OnDemand<"counted-task", CountedBehavior, ControlQueue,
                                               solar::execution::Counted<8>>;
using BlockingTask = solar::execution::OnDemand<"blocking-task", BlockingBehavior, ControlQueue>;
using TokenTask =
    solar::execution::OnDemand<"token-task", TokenBehavior, solar::execution::SystemWorkQueue>;
using FailingTask =
    solar::execution::OnDemand<"failing-task", FailingBehavior, solar::execution::SystemWorkQueue,
                               solar::execution::failure::Suspend>;
using DelayedTask =
    solar::execution::Delayable<"delayed-task", DelayedBehavior, solar::execution::SystemWorkQueue>;
using PeriodicTask = solar::execution::Periodic<"periodic-task", PeriodicBehavior, 10_ms,
                                                ControlQueue, solar::execution::StartImmediately>;
using PollTask = solar::execution::PollTriggered<"poll-task", PollBehavior, PollInput,
                                                 solar::execution::SystemWorkQueue,
                                                 solar::execution::poll::AutoRearm>;

struct TaskOwner
{
    static constexpr solar::component::Descriptor descriptor{.name = "task-owner"};
    using Dependencies = solar::Dependencies<Resource>;
    using Tasks = solar::execution::Tasks<OwnedTask, CountedTask, BlockingTask>;
};

struct CooperativeService
{
    static constexpr solar::component::Descriptor descriptor{.name = "cooperative-service"};
    using Dependencies = solar::Dependencies<Resource>;
    using Execution =
        solar::execution::Service<solar::execution::StackSize<2048>, solar::execution::Priority<2>,
                                  solar::execution::StopTimeout<200_ms>>;

    static solar::Status run(solar::StopToken stop);
};

using Blueprint = solar::Blueprint<
    solar::Devices<Resource>, solar::Facilities<TaskOwner>, solar::Services<CooperativeService>,
    solar::Executors<ControlQueue>,
    solar::Execution<SystemTask, FailingTask, DelayedTask, PeriodicTask, PollTask, TokenTask>>;
using System = solar::System<Blueprint>;

struct UnexpectedService
{
    static constexpr solar::component::Descriptor descriptor{.name = "unexpected-service"};
    using Execution = solar::execution::Service<solar::execution::StackSize<1024>>;

    static solar::Status run(solar::StopToken)
    {
        return solar::Status::Ok;
    }
};

using UnexpectedSystem = solar::System<solar::Blueprint<solar::Services<UnexpectedService>>>;

struct StuckService
{
    static constexpr solar::component::Descriptor descriptor{.name = "stuck-service"};
    using Execution = solar::execution::Service<solar::execution::StackSize<1024>,
                                                solar::execution::StopTimeout<5_ms>,
                                                solar::execution::AbortOnTimeout<true>>;

    static solar::Status run(solar::StopToken)
    {
        while (true) {
            k_sleep(K_MSEC(20));
        }
    }
};

using StuckSystem = solar::System<solar::Blueprint<solar::Services<StuckService>>>;

} // namespace fixture

SOLAR_BIND_SYSTEM(fixture::System);

solar::Status fixture::CooperativeService::run(solar::StopToken stop)
{
    service_saw_running.store(solar::lifecycle::state() == solar::lifecycle::SystemState::Running,
                              std::memory_order_release);
    service_started.store(true, std::memory_order_release);
    return stop.wait();
}

namespace
{

void submit_from_isr(const void*)
{
    const auto submitted = solar::execution::try_submit_isr<fixture::SystemTask>();
    fixture::isr_submit_ok.store(submitted.has_value(), std::memory_order_release);
    fixture::isr_receipt_marked.store(submitted && submitted->from_isr, std::memory_order_release);
}

void wait_for(const std::atomic_uint& value, unsigned minimum)
{
    for (int attempt = 0; attempt < 100 && value.load(std::memory_order_acquire) < minimum;
         ++attempt) {
        k_sleep(K_MSEC(2));
    }
    zassert_true(value.load(std::memory_order_acquire) >= minimum);
}

void* setup()
{
    fixture::PollInput::configure();
    auto boot = fixture::System::boot();
    zassert_true(boot.has_value());
    for (int attempt = 0;
         attempt < 100 && !fixture::service_started.load(std::memory_order_acquire); ++attempt) {
        k_sleep(K_MSEC(1));
    }
    zassert_true(fixture::service_started.load(std::memory_order_acquire));
    return nullptr;
}

void teardown(void*)
{
    auto stopped = solar::stop();
    zassert_true(stopped.has_value());
}

} // namespace

ZTEST_SUITE(solar_execution, nullptr, setup, nullptr, nullptr, teardown);

ZTEST(solar_execution, test_activation_barrier_and_service_record)
{
    zassert_true(fixture::service_saw_running.load(std::memory_order_acquire));
    const auto record = solar::execution::service<fixture::CooperativeService>();
    zassert_true(record.thread_created);
    zassert_true(record.thread_started);
    zassert_true(record.running);
    zassert_equal(record.containment, solar::execution::ContainmentState::Prepared);
}

ZTEST(solar_execution, test_system_and_owned_on_demand_work)
{
    zassert_true(solar::execution::submit<fixture::SystemTask>().has_value());
    zassert_true(solar::execution::submit<fixture::OwnedTask>().has_value());
    wait_for(fixture::system_runs, 1);
    wait_for(fixture::owned_runs, 1);

    const auto executor = solar::execution::executor<fixture::ControlQueue>();
    zassert_true(executor.started);
    zassert_equal(fixture::owned_thread.load(std::memory_order_acquire), executor.thread);
}

ZTEST(solar_execution, test_token_aware_behavior_gets_registration_stop_token)
{
    const auto before = fixture::token_runs.load(std::memory_order_acquire);
    fixture::token_stop_possible.store(false, std::memory_order_release);
    zassert_true(solar::execution::submit<fixture::TokenTask>().has_value());
    wait_for(fixture::token_runs, before + 1);
    zassert_true(fixture::token_stop_possible.load(std::memory_order_acquire));
}

ZTEST(solar_execution, test_isr_submission_uses_explicit_nonwaiting_path)
{
    const auto before = fixture::system_runs.load(std::memory_order_acquire);
    fixture::isr_submit_ok.store(false, std::memory_order_release);
    fixture::isr_receipt_marked.store(false, std::memory_order_release);
    irq_offload(&submit_from_isr, nullptr);
    zassert_true(fixture::isr_submit_ok.load(std::memory_order_acquire));
    zassert_true(fixture::isr_receipt_marked.load(std::memory_order_acquire));
    wait_for(fixture::system_runs, before + 1);
    const auto record = solar::execution::registration<fixture::SystemTask>();
    zassert_true(record.has_value());
    zassert_true(record->isr_submissions >= 1);
}

ZTEST(solar_execution, test_counted_admission_runs_each_occurrence)
{
    const auto before = fixture::counted_runs.load(std::memory_order_acquire);
    for (int index = 0; index < 5; ++index) {
        auto submitted = solar::execution::submit<fixture::CountedTask>();
        zassert_true(submitted.has_value());
        zassert_equal(submitted->disposition, solar::execution::SubmissionDisposition::Counted);
    }
    wait_for(fixture::counted_runs, before + 5);
    const auto record = solar::execution::registration<fixture::CountedTask>();
    zassert_true(record.has_value());
    zassert_true(record->counted >= 5);
}

ZTEST(solar_execution, test_delayable_periodic_and_poll_triggered_work)
{
    const auto delayed_before = fixture::delayed_runs.load(std::memory_order_acquire);
    zassert_true(solar::execution::schedule<fixture::DelayedTask>(20ms).has_value());
    zassert_true(solar::execution::reschedule<fixture::DelayedTask>(1ms).has_value());
    wait_for(fixture::delayed_runs, delayed_before + 1);

    wait_for(fixture::periodic_runs, 2);
    const auto poll_before = fixture::poll_runs.load(std::memory_order_acquire);
    zassert_equal(fixture::PollInput::signal().raise(), solar::Status::Ok);
    wait_for(fixture::poll_runs, poll_before + 1);
}

ZTEST(solar_execution, test_cancellation_before_and_after_queue_admission)
{
    const auto delayed_before = fixture::delayed_runs.load(std::memory_order_acquire);
    zassert_true(solar::execution::schedule<fixture::DelayedTask>(100ms).has_value());
    const auto delayed_cancel = solar::execution::cancel_sync<fixture::DelayedTask>();
    zassert_true(delayed_cancel.has_value());
    zassert_true(delayed_cancel->pending_cancelled);
    zassert_true(delayed_cancel->quiescent);
    k_sleep(K_MSEC(110));
    zassert_equal(fixture::delayed_runs.load(std::memory_order_acquire), delayed_before);

    fixture::blocking_started.store(false, std::memory_order_release);
    fixture::blocking_release.store(false, std::memory_order_release);
    const auto blocking_before = fixture::blocking_runs.load(std::memory_order_acquire);
    zassert_true(solar::execution::submit<fixture::BlockingTask>().has_value());
    for (int attempt = 0;
         attempt < 100 && !fixture::blocking_started.load(std::memory_order_acquire); ++attempt) {
        k_sleep(K_MSEC(1));
    }
    zassert_true(fixture::blocking_started.load(std::memory_order_acquire));
    const auto running_cancel = solar::execution::cancel<fixture::BlockingTask>();
    zassert_true(running_cancel.has_value());
    zassert_false(running_cancel->quiescent);
    fixture::blocking_release.store(true, std::memory_order_release);
    zassert_true(solar::execution::flush<fixture::BlockingTask>().has_value());
    wait_for(fixture::blocking_runs, blocking_before + 1);
}

ZTEST(solar_execution, test_failure_policy_can_suspend_future_releases)
{
    zassert_true(solar::execution::submit<fixture::FailingTask>().has_value());
    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto record = solar::execution::registration<fixture::FailingTask>();
        zassert_true(record.has_value());
        if (record->failed != 0) {
            break;
        }
        k_sleep(K_MSEC(1));
    }
    const auto record = solar::execution::registration<fixture::FailingTask>();
    zassert_true(record.has_value());
    zassert_equal(record->availability, solar::execution::Availability::Suspended);
    const auto rejected = solar::execution::submit<fixture::FailingTask>();
    zassert_false(rejected.has_value());
    zassert_equal(rejected.error().reason, solar::execution::ErrorReason::RegistrationSuspended);
}

ZTEST(solar_execution, test_focused_registration_and_target_queries)
{
    const auto registrations = solar::execution::registrations();
    zassert_equal(registrations.size(), 9);
    const auto services = solar::execution::services();
    zassert_equal(services.size(), 1);
    const auto executors = solar::execution::executors();
    zassert_equal(executors.size(), 1);
    zassert_equal(executors[0].registration_count, 4);
    zassert_equal(executors[0].active_registrations, 4);
    zassert_true(executors[0].submissions >= 1);
    zassert_true(executors[0].completed_items >= 1);
    const auto target = solar::execution::system_target();
    zassert_equal(target.registration_count, 5);

    std::array<solar::execution::RegistrationRecord, 2> page_storage{};
    const auto page = solar::execution::registration_page(page_storage, 1);
    zassert_equal(page.count, 2);
    zassert_equal(page.total, 9);
    zassert_true(page.has_more());
}

ZTEST(solar_execution, test_unexpected_service_exit_marks_system_failed)
{
    auto boot = fixture::UnexpectedSystem::boot();
    zassert_true(boot.has_value());
    for (int attempt = 0;
         attempt < 100 && solar::lifecycle::Engine<fixture::UnexpectedSystem>::state() !=
                              solar::lifecycle::SystemState::Failed;
         ++attempt) {
        k_sleep(K_MSEC(1));
    }
    zassert_equal(solar::lifecycle::Engine<fixture::UnexpectedSystem>::state(),
                  solar::lifecycle::SystemState::Failed);
    const auto record = solar::execution::detail::service_state<fixture::UnexpectedSystem,
                                                                fixture::UnexpectedService>()
                            .copy();
    zassert_equal(record.run_status, solar::Status::UnexpectedExit);
    auto stopped = fixture::UnexpectedSystem::stop();
    zassert_false(stopped.has_value());
}

ZTEST(solar_execution, test_service_timeout_forces_owned_thread_containment)
{
    auto boot = fixture::StuckSystem::boot();
    zassert_true(boot.has_value());
    k_sleep(K_MSEC(2));
    auto stopped = fixture::StuckSystem::stop();
    zassert_false(stopped.has_value());
    auto report = solar::lifecycle::Engine<fixture::StuckSystem>::stop_report();
    zassert_true(report.has_value());
    zassert_equal(report->forced_exits, 1);
    zassert_equal(report->uncontained_execution, 0);
}
