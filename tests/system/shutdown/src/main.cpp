#include <array>
#include <atomic>
#include <cstddef>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "solar/system.hpp"

namespace
{

std::array<int, 32> Events{};
std::atomic_size_t EventCount{0};

void record_event(int event)
{
    const std::size_t index = EventCount.fetch_add(1);
    if (index < Events.size())
    {
        Events[index] = event;
    }
}

void reset_events()
{
    Events.fill(0);
    EventCount = 0;
}

template <solar::FixedString ComponentName,
          int Id,
          solar::Status StopStatus = solar::Status::Ok,
          solar::Status DeinitStatus = solar::Status::Ok,
          typename DependencyList = solar::Dependencies<>>
struct Tracked
{
    using Name = solar::Name<ComponentName>;
    using Dependencies = DependencyList;

    static solar::Status init() { return solar::Status::Ok; }
    static solar::Status start() { return solar::Status::Ok; }

    static solar::Status stop()
    {
        record_event(100 + Id);
        return StopStatus;
    }

    static solar::Status deinit()
    {
        record_event(200 + Id);
        return DeinitStatus;
    }
};

template <solar::FixedString ComponentName,
          int Id,
          std::uint32_t TimeoutMs = 50,
          bool Abort = true,
          typename DependencyList = solar::Dependencies<>>
struct TrackedService : Tracked<ComponentName, Id, solar::Status::Ok,
                                solar::Status::Ok, DependencyList>
{
    using Name = solar::Name<ComponentName>;
    using Thread = solar::ServiceSpec<Name,
                                      2048,
                                      solar::kernel::Priority::Normal,
                                      TimeoutMs,
                                      Abort>;

    static solar::Result<void> run(solar::StopToken stop)
    {
        while (!stop.stop_requested())
        {
            k_sleep(K_MSEC(1));
        }
        return solar::Status::Ok;
    }
};

using OrderBoard = Tracked<"order-board", 0>;
using OrderPeripheral = Tracked<"order-peripheral", 1, solar::Status::Ok,
                                solar::Status::Ok, solar::Dependencies<OrderBoard>>;
using OrderFacility = Tracked<"order-facility", 2, solar::Status::Ok,
                              solar::Status::Ok, solar::Dependencies<OrderPeripheral>>;
using OrderDevice = Tracked<"order-device", 3, solar::Status::Ok,
                            solar::Status::Ok, solar::Dependencies<OrderFacility>>;
using OrderChannel = Tracked<"order-channel", 4, solar::Status::Ok,
                             solar::Status::Ok, solar::Dependencies<OrderDevice>>;
using OrderService = TrackedService<"order-service", 5, 50, true,
                                    solar::Dependencies<OrderChannel>>;
using OrderTask = Tracked<"order-task", 6, solar::Status::Ok,
                          solar::Status::Ok, solar::Dependencies<OrderService>>;

using OrderedSystem = solar::System<
    OrderBoard,
    solar::Peripherals<OrderPeripheral>,
    solar::Devices<OrderDevice>,
    solar::Facilities<OrderFacility>,
    solar::Services<OrderService>,
    solar::Tasks<OrderTask>,
    solar::Channels<OrderChannel>>;

using FailureSystem = solar::System<
    Tracked<"failure-board", 0>,
    solar::Peripherals<Tracked<"failure-peripheral", 1>>,
    solar::Devices<Tracked<"failure-device", 3, solar::Status::Busy>>,
    solar::Facilities<Tracked<"failure-facility", 2,
                                solar::Status::Ok,
                                solar::Status::Timeout>>,
    solar::Services<TrackedService<"failure-service", 5>>,
    solar::Tasks<Tracked<"failure-task", 6>>,
    solar::Channels<Tracked<"failure-channel", 4>>>;

template <solar::FixedString ComponentName, solar::Status RunStatus>
struct ImmediateExitService
{
    using Name = solar::Name<ComponentName>;
    using Thread = solar::ServiceSpec<Name, 2048>;

    static solar::Status init() { return solar::Status::Ok; }
    static solar::Status start() { return solar::Status::Ok; }
    static solar::Status stop() { return solar::Status::Ok; }
    static solar::Status deinit() { return solar::Status::Ok; }

    static solar::Result<void> run(solar::StopToken)
    {
        return RunStatus;
    }
};

using FailedRunSystem = solar::System<
    Tracked<"failed-run-board", 0>,
    solar::Peripherals<>,
    solar::Devices<>,
    solar::Facilities<>,
    solar::Services<ImmediateExitService<"failed-run-service", solar::Status::Error>>>;

using UnexpectedExitSystem = solar::System<
    Tracked<"unexpected-board", 0>,
    solar::Peripherals<>,
    solar::Devices<>,
    solar::Facilities<>,
    solar::Services<ImmediateExitService<"unexpected-service", solar::Status::Ok>>>;

struct AbortTimeoutService
{
    using Name = solar::Name<"abort-timeout-service">;
    using Thread = solar::ServiceSpec<Name,
                                      2048,
                                      solar::kernel::Priority::Normal,
                                      2,
                                      true>;

    static solar::Status init() { return solar::Status::Ok; }
    static solar::Status start() { return solar::Status::Ok; }
    static solar::Status stop() { return solar::Status::Ok; }
    static solar::Status deinit() { return solar::Status::Ok; }

    static solar::Result<void> run(solar::StopToken)
    {
        while (true)
        {
            k_sleep(K_MSEC(1));
        }
    }
};

using AbortTimeoutSystem = solar::System<
    Tracked<"abort-timeout-board", 0>,
    solar::Peripherals<>,
    solar::Devices<>,
    solar::Facilities<>,
    solar::Services<AbortTimeoutService>>;

struct NoAbortTimeoutService
{
    using Name = solar::Name<"no-abort-service">;
    using Thread = solar::ServiceSpec<Name,
                                      2048,
                                      solar::kernel::Priority::Normal,
                                      1,
                                      false>;

    static solar::Status init() { return solar::Status::Ok; }
    static solar::Status start() { return solar::Status::Ok; }
    static solar::Status stop() { return solar::Status::Ok; }
    static solar::Status deinit() { return solar::Status::Ok; }

    static solar::Result<void> run(solar::StopToken stop)
    {
        while (!stop.stop_requested())
        {
            k_sleep(K_MSEC(1));
        }
        k_sleep(K_MSEC(30));
        return solar::Status::Ok;
    }
};

using NoAbortTimeoutSystem = solar::System<
    Tracked<"no-abort-board", 0>,
    solar::Peripherals<>,
    solar::Devices<>,
    solar::Facilities<>,
    solar::Services<NoAbortTimeoutService>>;

template <typename SystemT, typename ServiceT>
solar::LifecycleRecord wait_for_service_failure()
{
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        const auto service = SystemT::lifecycle::template record<ServiceT>();
        if (service && service.value().state == solar::LifecycleState::Failed)
        {
            return service.value();
        }
        k_sleep(K_MSEC(1));
    }
    return {};
}

} // namespace

ZTEST(solar_system_shutdown, test_cooperative_stop_and_reverse_deinit)
{
    zassert_true(OrderedSystem::boot().has_value());
    const auto running_thread = OrderedSystem::kernel::template thread<
        OrderService>();
    zassert_true(running_thread.thread_created);
    zassert_true(running_thread.running);
    zassert_not_null(running_thread.native_id);
    zassert_equal(running_thread.configured_stack_bytes, 2048U);
    zassert_true(running_thread.stack_usage_available);
    reset_events();

    zassert_equal(OrderedSystem::stop(), solar::Status::Ok);
    const std::array<int, 14> expected{{
        106, 105, 104, 103, 102, 101, 100,
        206, 205, 204, 203, 202, 201, 200,
    }};
    zassert_equal(EventCount.load(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        zassert_equal(Events[i], expected[i]);
    }

    const auto state = OrderedSystem::lifecycle::state();
    zassert_true(state.has_value());
    zassert_equal(state.value(), solar::SystemState::Stopped);
    zassert_true(OrderedSystem::stop_report().ok());
    zassert_equal(OrderedSystem::stop_report().completed_operations, expected.size());

    const auto records = OrderedSystem::lifecycle::components();
    zassert_true(records.has_value());
    for (const auto &record : records.value())
    {
        zassert_equal(record.state, solar::LifecycleState::Deinitialized);
        zassert_true(record.deinitialized_successfully);
    }

    const auto execution = OrderedSystem::kernel::template thread<
        OrderService>();
    zassert_true(execution.thread_created);
    zassert_true(execution.stop_requested);
    zassert_true(execution.exited);
    zassert_true(execution.exit_after_stop_request);
    zassert_equal(execution.run_status, solar::Status::Ok);
    zassert_false(execution.join_timed_out);
}

ZTEST(solar_system_shutdown, test_shutdown_continues_after_failures)
{
    zassert_true(FailureSystem::boot().has_value());
    reset_events();

    zassert_equal(FailureSystem::stop(), solar::Status::Busy);
    zassert_equal(FailureSystem::stop_report().failure_count, 2U);
    zassert_equal(FailureSystem::stop_report().first_failure.component.kind,
                  solar::ComponentKind::Device);
    zassert_equal(FailureSystem::stop_report().first_failure.operation,
                  solar::LifecycleOperation::Stop);
    zassert_equal(Events[EventCount.load() - 1], 200);

    const auto records = FailureSystem::lifecycle::components();
    zassert_true(records.has_value());
    zassert_equal(records.value()[FailureSystem::DeviceOffset].state,
                  solar::LifecycleState::Failed);
    zassert_equal(records.value()[FailureSystem::FacilityOffset].state,
                  solar::LifecycleState::Failed);
    zassert_equal(records.value()[FailureSystem::BoardOffset].state,
                  solar::LifecycleState::Deinitialized);
}

ZTEST(solar_system_shutdown, test_failed_and_unexpected_run_exit)
{
    const auto failed_boot = FailedRunSystem::boot();
    const auto failed_record = wait_for_service_failure<
        FailedRunSystem,
        ImmediateExitService<"failed-run-service", solar::Status::Error>>();
    zassert_equal(failed_record.state, solar::LifecycleState::Failed);
    zassert_equal(failed_record.first_failure.status, solar::Status::Error);
    zassert_true(FailedRunSystem::kernel::service_threads()[0].exited);
    zassert_equal(FailedRunSystem::kernel::service_threads()[0].run_status,
                  solar::Status::Error);

    const auto unexpected_boot = UnexpectedExitSystem::boot();
    const auto unexpected_record = wait_for_service_failure<
        UnexpectedExitSystem,
        ImmediateExitService<"unexpected-service", solar::Status::Ok>>();
    zassert_equal(unexpected_record.state, solar::LifecycleState::Failed);
    zassert_equal(unexpected_record.first_failure.status,
                  solar::Status::UnexpectedExit);
    zassert_true(UnexpectedExitSystem::kernel::service_threads()[0].exited);
    zassert_equal(UnexpectedExitSystem::kernel::service_threads()[0].run_status,
                  solar::Status::Ok);

    (void)failed_boot;
    (void)unexpected_boot;
}

ZTEST(solar_system_shutdown, test_stop_timeout_with_abort)
{
    zassert_true(AbortTimeoutSystem::boot().has_value());
    zassert_equal(AbortTimeoutSystem::stop(), solar::Status::Timeout);

    const auto execution = AbortTimeoutSystem::kernel::template thread<
        AbortTimeoutService>();
    zassert_true(execution.stop_requested);
    zassert_true(execution.join_timed_out);
    zassert_true(execution.abort_configured);
    zassert_true(execution.abort_attempted);
    zassert_true(execution.aborted);
    zassert_equal(execution.abort_status, solar::Status::Ok);
    zassert_false(execution.running);
}

ZTEST(solar_system_shutdown, test_stop_timeout_without_abort_preserves_dependencies)
{
    zassert_true(NoAbortTimeoutSystem::boot().has_value());
    zassert_equal(NoAbortTimeoutSystem::stop(), solar::Status::Timeout);

    const auto execution = NoAbortTimeoutSystem::kernel::template thread<
        NoAbortTimeoutService>();
    zassert_true(execution.join_timed_out);
    zassert_false(execution.abort_configured);
    zassert_false(execution.abort_attempted);
    zassert_true(execution.running);

    const auto board = NoAbortTimeoutSystem::lifecycle::template record<
        Tracked<"no-abort-board", 0>>();
    zassert_true(board.has_value());
    zassert_equal(board.value().state, solar::LifecycleState::Running);
    zassert_false(board.value().deinitialized_successfully);

    k_sleep(K_MSEC(40));
}

ZTEST_SUITE(solar_system_shutdown, nullptr, nullptr, nullptr, nullptr, nullptr);
