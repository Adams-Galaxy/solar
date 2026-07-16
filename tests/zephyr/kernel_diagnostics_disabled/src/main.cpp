#include <zephyr/ztest.h>

#include <solar/kernel.hpp>

static_assert(!solar::kernel::stack_diagnostics_available);
static_assert(!solar::kernel::runtime_diagnostics_available);
static_assert(!solar::kernel::thread_enumeration_available);
static_assert(!solar::kernel::runtime_stack_safety_available);

ZTEST(kernel_diagnostics_disabled, test_focused_queries_report_unsupported)
{
    const auto current = solar::kernel::this_thread::id();
    zassert_equal(solar::kernel::stack_usage(current).error(), solar::Status::NotSupported);
    zassert_equal(solar::kernel::runtime_stats(current).error(), solar::Status::NotSupported);
    zassert_equal(solar::kernel::set_runtime_stats(current, true), solar::Status::NotSupported);
    zassert_equal(solar::kernel::set_stack_warning_margin(current, 16),
                  solar::Status::NotSupported);
    zassert_equal(solar::kernel::check_stack_safety(current, true).error(),
                  solar::Status::NotSupported);
    zassert_equal(solar::kernel::for_each_thread_locked(nullptr), solar::Status::NotSupported);
    zassert_equal(solar::kernel::for_each_thread_unlocked(nullptr), solar::Status::NotSupported);
}

ZTEST_SUITE(kernel_diagnostics_disabled, nullptr, nullptr, nullptr, nullptr, nullptr);
