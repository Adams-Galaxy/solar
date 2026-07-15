#include <atomic>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "solar/task.hpp"

namespace
{

struct CountBehavior
{
    static void execute() { ++count; }
    static inline std::atomic_uint count{0};
};

struct CooperativeBehavior
{
    static solar::Status execute(solar::kernel::StopToken stop)
    {
        while (!stop.stop_requested()) k_sleep(K_MSEC(1));
        return solar::Status::Ok;
    }
};

using Event = solar::EventTask<solar::Name<"event-task">, CountBehavior>;
using Periodic = solar::PeriodicTask<solar::Name<"periodic-task">, CountBehavior, 2>;
using Executor = solar::SharedExecutor<solar::Name<"shared-executor">, 2048>;
using SharedA = solar::SharedTask<solar::Name<"shared-a">, CountBehavior, Executor>;
using SharedB = solar::SharedTask<solar::Name<"shared-b">, CountBehavior, Executor>;
using DedicatedPolicy = solar::TaskThreadPolicy<solar::Name<"dedicated-thread">, 2048>;
using Dedicated = solar::DedicatedTask<solar::Name<"dedicated-task">,
                                       CooperativeBehavior,
                                       DedicatedPolicy>;

} // namespace

ZTEST(solar_task_execution, test_event_and_periodic_tasks_share_system_executor)
{
    CountBehavior::count = 0;
    zassert_equal(Event::start(), solar::Status::Ok);
    zassert_equal(Event::trigger(), solar::Status::Ok);
    k_sleep(K_MSEC(5));
    zassert_true(Event::execution().completed >= 1);
    (void)Event::stop();

    zassert_equal(Periodic::start(), solar::Status::Ok);
    k_sleep(K_MSEC(50));
    (void)Periodic::stop();
    zassert_true(Periodic::execution().completed >= 2);
    zassert_false(Periodic::execution().accepting);
}

ZTEST(solar_task_execution, test_multiple_tasks_share_one_explicit_executor)
{
    CountBehavior::count = 0;
    zassert_equal(Executor::start(), solar::Status::Ok);
    zassert_equal(SharedA::start(), solar::Status::Ok);
    zassert_equal(SharedB::start(), solar::Status::Ok);
    zassert_equal(SharedA::trigger(), solar::Status::Ok);
    zassert_equal(SharedB::trigger(), solar::Status::Ok);
    k_sleep(K_MSEC(5));
    zassert_equal(SharedA::execution().completed, 1U);
    zassert_equal(SharedB::execution().completed, 1U);
    (void)SharedA::stop();
    (void)SharedB::stop();
    zassert_equal(Executor::stop(), solar::Status::Ok);
}

ZTEST(solar_task_execution, test_dedicated_execution_is_explicit_and_cooperative)
{
    zassert_equal(Dedicated::start(), solar::Status::Ok);
    k_sleep(K_MSEC(2));
    zassert_true(Dedicated::execution().dedicated_thread);
    zassert_true(Dedicated::execution().in_flight);
    zassert_equal(Dedicated::stop(), solar::Status::Ok);
    zassert_equal(Dedicated::execution().last_status, solar::Status::Ok);
    zassert_equal(Dedicated::execution().completed, 1U);
}

ZTEST_SUITE(solar_task_execution, nullptr, nullptr, nullptr, nullptr, nullptr);
