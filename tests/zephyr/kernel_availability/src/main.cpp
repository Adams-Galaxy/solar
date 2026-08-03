#include <solar/kernel.hpp>

#include <zephyr/ztest.h>

static_assert(!solar::kernel::poll_available);
static_assert(!solar::kernel::event_flags_available);
static_assert(!solar::kernel::mailbox_async_available);

ZTEST(kernel_availability, test_remaining_primitives_work_without_optional_features)
{
    solar::kernel::Semaphore semaphore;
    semaphore.give();
    zassert_true(semaphore.try_take().has_value());

    solar::kernel::Mailbox mailbox;
    std::uint32_t payload{};
    const auto receive = mailbox.receive(payload, {}, solar::kernel::Timeout::no_wait());
    zassert_false(receive.has_value());
    zassert_equal(solar::status_of(receive.error()), solar::Status::WouldBlock);
}

ZTEST_SUITE(kernel_availability, nullptr, nullptr, nullptr, nullptr, nullptr);
