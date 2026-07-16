#include <solar/kernel.hpp>

#include <zephyr/ztest.h>

static_assert(!solar::kernel::poll_available);
static_assert(!solar::kernel::event_flags_available);

ZTEST(kernel_availability, test_remaining_primitives_work_without_optional_features)
{
    solar::kernel::Semaphore semaphore;
    semaphore.give();
    zassert_equal(semaphore.try_take(), solar::Status::Ok);
}

ZTEST_SUITE(kernel_availability, nullptr, nullptr, nullptr, nullptr, nullptr);
