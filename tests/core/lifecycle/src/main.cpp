#include <array>
#include <atomic>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "solar/core/boot.hpp"
#include "solar/core/stop.hpp"
#include "solar/lifecycle/storage.hpp"

namespace
{

constexpr solar::ComponentId BoardId{0};
constexpr solar::ComponentId ServiceId{1};

constexpr std::array<solar::LifecycleRecord, 2> Records{{
    {
        .component = {BoardId, "board", solar::ComponentKind::Board},
        .hooks = {.init = true, .start = true, .stop = true, .deinit = true},
    },
    {
        .component = {ServiceId, "remote", solar::ComponentKind::Service},
        .hooks = {.init = true, .start = true, .run = true, .stop = true},
    },
}};

static_assert(solar::can_transition(solar::SystemState::Dormant,
                                    solar::SystemState::Booting));
static_assert(!solar::can_transition(solar::SystemState::Stopped,
                                     solar::SystemState::Booting));
static_assert(solar::can_transition(solar::LifecycleState::Registered,
                                    solar::LifecycleState::Initializing));
static_assert(!solar::can_transition(solar::LifecycleState::Registered,
                                     solar::LifecycleState::Running));

K_THREAD_STACK_DEFINE(ReaderStack, 2048);
k_thread ReaderThread;
k_sem ReaderDone;
std::atomic_uint32_t ReaderFailures{0};

void read_records(void *storage_ptr, void *, void *)
{
    auto &storage = *static_cast<solar::lifecycle::Storage<2> *>(storage_ptr);
    for (std::size_t i = 0; i < 100; ++i)
    {
        const auto records = storage.records();
        const auto board = storage.record(BoardId);
        if (!records || !board)
        {
            ++ReaderFailures;
        }
    }
    k_sem_give(&ReaderDone);
}

} // namespace

ZTEST(solar_lifecycle, test_state_transitions)
{
    solar::lifecycle::Storage<2> storage;
    zassert_equal(storage.initialize(Records), solar::Status::Ok);

    zassert_equal(storage.transition_system(solar::SystemState::Booting),
                  solar::Status::Ok);
    zassert_equal(storage.transition_system(solar::SystemState::Running),
                  solar::Status::Invalid);

    const auto state = storage.system_state();
    zassert_true(state.has_value());
    zassert_equal(state.value(), solar::SystemState::Booting);
}

ZTEST(solar_lifecycle, test_record_success_and_failure)
{
    solar::lifecycle::Storage<2> storage;
    zassert_equal(storage.initialize(Records), solar::Status::Ok);

    zassert_equal(storage.transition(BoardId,
                                     solar::LifecycleState::Initializing,
                                     solar::LifecycleOperation::Init),
                  solar::Status::Ok);
    zassert_equal(storage.transition(BoardId,
                                     solar::LifecycleState::Initialized,
                                     solar::LifecycleOperation::Init),
                  solar::Status::Ok);
    zassert_equal(storage.transition(BoardId,
                                     solar::LifecycleState::Starting,
                                     solar::LifecycleOperation::Start),
                  solar::Status::Ok);
    zassert_equal(storage.transition(BoardId,
                                     solar::LifecycleState::Failed,
                                     solar::LifecycleOperation::Start,
                                     solar::Status::NotReady),
                  solar::Status::Ok);

    const auto board = storage.record(BoardId);
    zassert_true(board.has_value());
    zassert_equal(board.value().state, solar::LifecycleState::Failed);
    zassert_equal(board.value().last_operation, solar::LifecycleOperation::Start);
    zassert_equal(board.value().last_status, solar::Status::NotReady);
    zassert_equal(board.value().first_failure.operation,
                  solar::LifecycleOperation::Start);
    zassert_equal(board.value().first_failure.status, solar::Status::NotReady);
    zassert_equal(board.value().transition_count, 4U);

    zassert_equal(storage.transition(ServiceId,
                                     solar::LifecycleState::Running,
                                     solar::LifecycleOperation::Run),
                  solar::Status::Invalid);
    zassert_equal(storage.record(solar::ComponentId{42}).status(),
                  solar::Status::NotFound);
}

ZTEST(solar_lifecycle, test_queries_return_copies)
{
    solar::lifecycle::Storage<2> storage;
    zassert_equal(storage.initialize(Records), solar::Status::Ok);

    auto snapshot = storage.records();
    zassert_true(snapshot.has_value());
    snapshot.value()[0].state = solar::LifecycleState::Disabled;

    const auto board = storage.record(BoardId);
    zassert_true(board.has_value());
    zassert_equal(board.value().state, solar::LifecycleState::Registered);
    zassert_true(board.value().hooks.deinit);
    zassert_false(board.value().hooks.run);
}

ZTEST(solar_lifecycle, test_reports_preserve_first_failure)
{
    solar::BootReport boot;
    boot.record_success();
    boot.record_failure({ServiceId, "remote", solar::ComponentKind::Service},
                        solar::BootPhase::ServiceStart,
                        solar::LifecycleOperation::Start,
                        solar::Status::NotReady);
    boot.record_failure({BoardId, "board", solar::ComponentKind::Board},
                        solar::BootPhase::BoardStart,
                        solar::LifecycleOperation::Start,
                        solar::Status::Busy);

    zassert_false(boot.ok());
    zassert_equal(boot.completed_operations, 1U);
    zassert_equal(boot.failure.component.id, ServiceId);
    zassert_equal(boot.failure.status, solar::Status::NotReady);

    solar::StopReport report;
    report.record_failure({BoardId, "board", solar::ComponentKind::Board},
                          solar::LifecycleOperation::Stop,
                          solar::Status::Busy);
    report.record_failure({ServiceId, "remote", solar::ComponentKind::Service},
                          solar::LifecycleOperation::Deinit,
                          solar::Status::Timeout);

    zassert_false(report.ok());
    zassert_equal(report.status, solar::Status::Busy);
    zassert_equal(report.failure_count, 2U);
    zassert_equal(report.first_failure.component.id, BoardId);
    zassert_equal(report.first_failure.operation, solar::LifecycleOperation::Stop);
}

ZTEST(solar_lifecycle, test_invalid_and_duplicate_descriptors_are_rejected)
{
    solar::lifecycle::Storage<2> storage;
    auto invalid = Records;
    invalid[0].component.id = {};
    zassert_equal(storage.initialize(invalid), solar::Status::Invalid);

    auto duplicate = Records;
    duplicate[1].component.id = BoardId;
    zassert_equal(storage.initialize(duplicate), solar::Status::Already);
}

ZTEST(solar_lifecycle, test_queries_are_safe_during_mutation)
{
    solar::lifecycle::Storage<2> storage;
    zassert_equal(storage.initialize(Records), solar::Status::Ok);

    ReaderFailures = 0;
    k_sem_init(&ReaderDone, 0, 1);
    k_tid_t reader = k_thread_create(&ReaderThread,
                                     ReaderStack,
                                     K_THREAD_STACK_SIZEOF(ReaderStack),
                                     read_records,
                                     &storage,
                                     nullptr,
                                     nullptr,
                                     K_PRIO_PREEMPT(0),
                                     0,
                                     K_NO_WAIT);
    zassert_not_null(reader);

    zassert_equal(storage.transition(BoardId,
                                     solar::LifecycleState::Initializing,
                                     solar::LifecycleOperation::Init),
                  solar::Status::Ok);
    zassert_equal(storage.transition(BoardId,
                                     solar::LifecycleState::Initialized,
                                     solar::LifecycleOperation::Init),
                  solar::Status::Ok);
    zassert_equal(storage.transition(BoardId,
                                     solar::LifecycleState::Starting,
                                     solar::LifecycleOperation::Start),
                  solar::Status::Ok);
    zassert_equal(storage.transition(BoardId,
                                     solar::LifecycleState::Running,
                                     solar::LifecycleOperation::Start),
                  solar::Status::Ok);

    zassert_equal(k_sem_take(&ReaderDone, K_SECONDS(1)), 0);
    zassert_equal(ReaderFailures.load(), 0U);
}

ZTEST_SUITE(solar_lifecycle, nullptr, nullptr, nullptr, nullptr, nullptr);
