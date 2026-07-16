#include <array>
#include <atomic>
#include <cstddef>

#include <zephyr/ztest.h>

#include "fixture.hpp"

namespace fixture = lifecycle_fixture;
namespace lifecycle = solar::lifecycle;

constexpr auto graph_components = solar::graph::components();
constexpr auto service_dependencies = solar::graph::dependencies<fixture::SuccessService>();
static_assert(graph_components.size() == 5);
static_assert(service_dependencies.size() == 1);
static_assert(service_dependencies[0].descriptor.name == "success.sensor");

template <typename Trace, std::size_t Size> void expect_trace(const std::array<int, Size>& expected)
{
    zassert_equal(Trace::count, Size);
    for (std::size_t index = 0; index < Size; ++index) {
        zassert_equal(Trace::values[index], expected[index]);
    }
}

ZTEST(solar_lifecycle, test_success_order_records_activation_stop_and_reboot_policy)
{
    fixture::SuccessTrace::reset();
    fixture::activation_committed.store(false, std::memory_order_release);
    fixture::execution_released.store(false, std::memory_order_release);
    fixture::transition_states_observed.store(true, std::memory_order_release);

    const auto boot = solar::boot();
    zassert_true(boot.has_value());
    zassert_equal(boot->initial_state, lifecycle::SystemState::Dormant);
    zassert_equal(boot->final_state, lifecycle::SystemState::Running);
    zassert_true(boot->initialization_completed);
    zassert_true(boot->start_completed);
    zassert_equal(boot->initialized_components, 5);
    zassert_equal(boot->started_components, 5);
    zassert_true(fixture::activation_committed.load(std::memory_order_acquire));
    zassert_true(fixture::execution_released.load(std::memory_order_acquire));
    zassert_true(fixture::transition_states_observed.load(std::memory_order_acquire));

    expect_trace<fixture::SuccessTrace>(std::array{10, 20, 30, 40, 11, 21, 31, 32, 41, 33, 34});

    const auto passive = lifecycle::record<fixture::SuccessPassive>();
    zassert_true(passive.has_value());
    zassert_equal(passive->state, lifecycle::ComponentState::Running);
    zassert_equal(passive->init.outcome, lifecycle::HookOutcome::NotPresent);
    zassert_equal(passive->start.outcome, lifecycle::HookOutcome::NotPresent);
    zassert_equal(passive->stop.outcome, lifecycle::HookOutcome::NotPresent);
    zassert_equal(passive->deinit.outcome, lifecycle::HookOutcome::NotPresent);

    const auto service = lifecycle::record<fixture::SuccessService>();
    zassert_true(service.has_value());
    zassert_equal(service->category, lifecycle::ComponentCategory::Service);
    zassert_true(service->execution_prepared);
    zassert_false(service->execution_contained);

    const auto records = lifecycle::components();
    zassert_true(records.has_value());
    zassert_equal(records->size(), 5);

    std::array<lifecycle::ComponentRecord, 2> page_storage{};
    const auto first_page = lifecycle::component_page(page_storage);
    zassert_true(first_page.has_value());
    zassert_equal(first_page->offset, 0);
    zassert_equal(first_page->count, 2);
    zassert_equal(first_page->total, 5);
    zassert_true(first_page->has_more());
    const auto final_page = lifecycle::component_page(page_storage, 4);
    zassert_true(final_page.has_value());
    zassert_equal(final_page->count, 1);
    zassert_false(final_page->has_more());
    zassert_equal(page_storage[0].descriptor.descriptor.name, "success.executor");
    zassert_equal(lifecycle::component_page(page_storage, 6).error(), solar::Status::Invalid);

    const auto retained_boot = lifecycle::boot_report();
    zassert_true(retained_boot.has_value());
    const auto rejected_running = solar::boot();
    zassert_false(rejected_running.has_value());
    zassert_equal(rejected_running.error().reason, lifecycle::BootErrorReason::AlreadyRunning);
    const auto still_retained = lifecycle::boot_report();
    zassert_true(still_retained.has_value());
    zassert_equal(still_retained->final_state, retained_boot->final_state);

    const auto stop = fixture::SuccessSystem::stop();
    zassert_true(stop.has_value());
    zassert_equal(stop->final_state, lifecycle::SystemState::Stopped);
    zassert_equal(stop->clean_exits, 2);
    zassert_equal(stop->stopped_components, 5);
    zassert_equal(stop->deinitialized_components, 5);
    expect_trace<fixture::SuccessTrace>(std::array{10, 20, 30, 40, 11, 21, 31, 32, 41, 33, 34,
                                                   35, 36, 42, 37, 22, 12, 43, 38, 23, 13});

    const auto rejected_stopped = solar::boot();
    zassert_false(rejected_stopped.has_value());
    zassert_equal(rejected_stopped.error().reason, lifecycle::BootErrorReason::RebootUnsupported);
    zassert_equal(lifecycle::state(), lifecycle::SystemState::Stopped);
}

ZTEST(solar_lifecycle, test_init_failure_rolls_back_only_earned_initialization)
{
    fixture::InitFailureTrace::reset();
    const auto boot = solar::boot<fixture::InitFailureApplication>();
    zassert_false(boot.has_value());
    zassert_equal(boot.error().reason, lifecycle::BootErrorReason::ComponentFailure);
    zassert_equal(boot.error().status, solar::Status::NotReady);
    expect_trace<fixture::InitFailureTrace>(std::array{1, 2, 3});

    const auto report = lifecycle::boot_report<fixture::InitFailureApplication>();
    zassert_true(report.has_value());
    zassert_true(report->rollback_attempted);
    zassert_true(report->rollback_completed);
    zassert_equal(report->initialized_components, 1);
    zassert_equal(report->final_state, lifecycle::SystemState::Failed);
    zassert_equal(report->primary_failure->operation, lifecycle::Operation::Init);

    const auto failed =
        lifecycle::record<fixture::InitFailureComponent, fixture::InitFailureApplication>();
    zassert_true(failed.has_value());
    zassert_equal(failed->init.outcome, lifecycle::HookOutcome::Failed);
    zassert_equal(failed->deinit.outcome, lifecycle::HookOutcome::NotAttempted);
    const auto skipped =
        lifecycle::record<fixture::InitFailureSkipped, fixture::InitFailureApplication>();
    zassert_true(skipped.has_value());
    zassert_equal(skipped->init.outcome, lifecycle::HookOutcome::NotAttempted);
}

ZTEST(solar_lifecycle, test_start_failure_preserves_primary_through_cleanup_failure)
{
    fixture::StartFailureTrace::reset();
    const auto boot = solar::boot<fixture::StartFailureApplication>();
    zassert_false(boot.has_value());
    zassert_equal(boot.error().reason, lifecycle::BootErrorReason::ComponentFailure);
    zassert_equal(boot.error().status, solar::Status::Invalid);
    zassert_equal(boot.error().failure->operation, lifecycle::Operation::Start);
    expect_trace<fixture::StartFailureTrace>(std::array{1, 2, 3, 4, 5, 6, 7});

    const auto report = lifecycle::boot_report<fixture::StartFailureApplication>();
    zassert_true(report.has_value());
    zassert_equal(report->primary_failure->status, solar::Status::Invalid);
    zassert_equal(report->cleanup_failures.total, 1);
    zassert_equal(report->cleanup_failures.entries[0].status, solar::Status::Error);
    zassert_false(report->cleanup_failures.entries[0].primary);

    const auto failed =
        lifecycle::record<fixture::StartFailureComponent, fixture::StartFailureApplication>();
    zassert_equal(failed->start.outcome, lifecycle::HookOutcome::Failed);
    zassert_equal(failed->stop.outcome, lifecycle::HookOutcome::NotAttempted);
    zassert_equal(failed->deinit.outcome, lifecycle::HookOutcome::Succeeded);
}

ZTEST(solar_lifecycle, test_stop_failure_continues_deinit_and_reports_abnormal_shutdown)
{
    fixture::StopFailureComponent::deinit_called = false;
    zassert_true(solar::boot<fixture::StopFailureApplication>().has_value());
    const auto stop = solar::stop<fixture::StopFailureApplication>();
    zassert_false(stop.has_value());
    zassert_equal(stop.error().reason, lifecycle::StopErrorReason::ShutdownFailed);
    zassert_true(fixture::StopFailureComponent::deinit_called);

    const auto report = lifecycle::stop_report<fixture::StopFailureApplication>();
    zassert_true(report.has_value());
    zassert_equal(report->failures.total, 1);
    zassert_equal(report->failures.entries[0].operation, lifecycle::Operation::Stop);
    zassert_equal(report->final_state, lifecycle::SystemState::Failed);
}

ZTEST(solar_lifecycle, test_execution_prepare_and_validation_failures_roll_back)
{
    fixture::PrepareFailureService::deinit_called = false;
    const auto prepare = solar::boot<fixture::PrepareFailureApplication>();
    zassert_false(prepare.has_value());
    zassert_equal(prepare.error().reason, lifecycle::BootErrorReason::ExecutionFailure);
    zassert_equal(prepare.error().failure->operation, lifecycle::Operation::PrepareExecution);
    zassert_true(fixture::PrepareFailureService::deinit_called);
    const auto prepare_record =
        lifecycle::record<fixture::PrepareFailureService, fixture::PrepareFailureApplication>();
    zassert_equal(prepare_record->stop.outcome, lifecycle::HookOutcome::NotAttempted);

    fixture::ValidateFailureService::stop_called = false;
    fixture::ValidateFailureService::deinit_called = false;
    const auto validate = solar::boot<fixture::ValidateFailureApplication>();
    zassert_false(validate.has_value());
    zassert_equal(validate.error().reason, lifecycle::BootErrorReason::ExecutionFailure);
    zassert_equal(validate.error().failure->operation, lifecycle::Operation::ValidateExecution);
    zassert_true(fixture::ValidateFailureService::stop_called);
    zassert_true(fixture::ValidateFailureService::deinit_called);
}

ZTEST(solar_lifecycle,
      test_uncontained_execution_preserves_dependency_and_cleans_independent_branch)
{
    fixture::PreservedResource::stop_called = false;
    fixture::PreservedResource::deinit_called = false;
    fixture::IndependentResource::stop_called = false;
    fixture::IndependentResource::deinit_called = false;

    zassert_true(solar::boot<fixture::UncontainedApplication>().has_value());
    const auto stop = solar::stop<fixture::UncontainedApplication>();
    zassert_false(stop.has_value());
    zassert_equal(stop.error().reason, lifecycle::StopErrorReason::ShutdownFailed);

    const auto report = lifecycle::stop_report<fixture::UncontainedApplication>();
    zassert_true(report.has_value());
    zassert_equal(report->uncontained_execution, 1);
    zassert_equal(report->preserved_dependencies, 1);
    zassert_equal(report->join_timeouts, 1);
    zassert_false(fixture::PreservedResource::stop_called);
    zassert_false(fixture::PreservedResource::deinit_called);
    zassert_true(fixture::IndependentResource::stop_called);
    zassert_true(fixture::IndependentResource::deinit_called);

    const auto preserved =
        lifecycle::record<fixture::PreservedResource, fixture::UncontainedApplication>();
    const auto service =
        lifecycle::record<fixture::UncontainedService, fixture::UncontainedApplication>();
    zassert_true(preserved->cleanup_blocked);
    zassert_true(service->cleanup_blocked);

    const auto retry = solar::stop<fixture::UncontainedApplication>();
    zassert_false(retry.has_value());
    zassert_equal(retry.error().reason, lifecycle::StopErrorReason::ShutdownFailed);
}

ZTEST(solar_lifecycle, test_forced_containment_is_abnormal_but_allows_cleanup)
{
    fixture::ForcedService::stop_called = false;
    fixture::ForcedService::deinit_called = false;
    zassert_true(solar::boot<fixture::ForcedApplication>().has_value());
    const auto stop = solar::stop<fixture::ForcedApplication>();
    zassert_false(stop.has_value());
    zassert_equal(stop.error().reason, lifecycle::StopErrorReason::ShutdownFailed);
    zassert_true(fixture::ForcedService::stop_called);
    zassert_true(fixture::ForcedService::deinit_called);

    const auto report = lifecycle::stop_report<fixture::ForcedApplication>();
    zassert_true(report.has_value());
    zassert_equal(report->forced_exits, 1);
    zassert_equal(report->join_timeouts, 1);
    zassert_equal(report->abort_attempts, 1);
    zassert_equal(report->uncontained_execution, 0);
    zassert_equal(report->preserved_dependencies, 0);
    zassert_equal(report->final_state, lifecycle::SystemState::Failed);
}

ZTEST(solar_lifecycle, test_report_capacity_truncates_summary_but_records_remain_queryable)
{
    static_assert(lifecycle::report_failure_capacity == 2);
    const auto boot = solar::boot<fixture::CapacityApplication>();
    zassert_false(boot.has_value());
    const auto report = lifecycle::boot_report<fixture::CapacityApplication>();
    zassert_true(report.has_value());
    zassert_equal(report->cleanup_failures.total, 3);
    zassert_equal(report->cleanup_failures.retained, 2);
    zassert_true(report->cleanup_failures.truncated());

    const auto one = lifecycle::record<fixture::CapacityOne, fixture::CapacityApplication>();
    const auto two = lifecycle::record<fixture::CapacityTwo, fixture::CapacityApplication>();
    const auto three = lifecycle::record<fixture::CapacityThree, fixture::CapacityApplication>();
    zassert_equal(one->deinit.outcome, lifecycle::HookOutcome::Failed);
    zassert_equal(two->deinit.outcome, lifecycle::HookOutcome::Failed);
    zassert_equal(three->deinit.outcome, lifecycle::HookOutcome::Failed);
}

ZTEST(solar_lifecycle, test_concurrent_boot_is_busy_and_transitions_are_queryable)
{
    fixture::concurrent_boot_succeeded.store(false, std::memory_order_release);
    solar::kernel::Thread<2048> thread;
    zassert_equal(thread.launch(&fixture::concurrent_boot_entry, nullptr,
                                {.priority = solar::kernel::Priority::preemptive<1>(),
                                 .name = "lifecycle-boot"}),
                  solar::Status::Ok);
    zassert_equal(fixture::ConcurrentComponent::entered.take(
                      solar::kernel::Timeout::after(std::chrono::milliseconds{100})),
                  solar::Status::Ok);

    zassert_equal(lifecycle::state<fixture::ConcurrentApplication>(),
                  lifecycle::SystemState::Initializing);
    const auto record =
        lifecycle::record<fixture::ConcurrentComponent, fixture::ConcurrentApplication>();
    zassert_true(record.has_value());
    zassert_equal(record->state, lifecycle::ComponentState::Initializing);

    const auto rejected = solar::boot<fixture::ConcurrentApplication>();
    zassert_false(rejected.has_value());
    zassert_equal(rejected.error().reason, lifecycle::BootErrorReason::Busy);
    fixture::ConcurrentComponent::release.give();
    zassert_equal(thread.join(solar::kernel::Timeout::after(std::chrono::milliseconds{100})),
                  solar::Status::Ok);
    zassert_true(fixture::concurrent_boot_succeeded.load(std::memory_order_acquire));
    zassert_true(solar::stop<fixture::ConcurrentApplication>().has_value());
}

ZTEST(solar_lifecycle, test_empty_blueprint_has_a_complete_lifecycle)
{
    const solar::Result<solar::BootReport, solar::BootError> boot =
        solar::boot<fixture::EmptyApplication>();
    zassert_true(boot.has_value());
    zassert_equal(boot->initialized_components, 0);
    zassert_equal(boot->started_components, 0);

    const auto records = lifecycle::components<fixture::EmptyApplication>();
    zassert_true(records.has_value());
    zassert_true(records->empty());

    const solar::Result<solar::StopReport, solar::StopError> stop =
        solar::stop<fixture::EmptyApplication>();
    zassert_true(stop.has_value());
    zassert_equal(stop->stopped_components, 0);
    zassert_equal(stop->deinitialized_components, 0);
}

ZTEST_SUITE(solar_lifecycle, nullptr, nullptr, nullptr, nullptr, nullptr);
