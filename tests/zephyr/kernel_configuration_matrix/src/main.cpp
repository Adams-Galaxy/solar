#include <type_traits>

#include <solar/kernel.hpp>

static_assert(!std::is_copy_constructible_v<solar::kernel::SpinLock>);
static_assert(std::is_trivially_copyable_v<solar::kernel::SpinLockRef>);
static_assert(std::is_trivially_copyable_v<solar::kernel::SemaphoreRef>);
static_assert(std::is_trivially_copyable_v<solar::kernel::MailboxRef>);
static_assert(solar::kernel::Timeout::no_wait().is_no_wait());
static_assert(solar::kernel::Timeout::forever().is_forever());

#if defined(SOLAR_EXPECT_SMP)
static_assert(CONFIG_SMP);
static_assert(CONFIG_MP_MAX_NUM_CPUS >= 2);
#endif

#if defined(SOLAR_EXPECT_NO_CLOCK)
static_assert(!IS_ENABLED(CONFIG_SYS_CLOCK_EXISTS));
#endif

#if defined(SOLAR_EXPECT_NO_MULTITHREADING)
static_assert(!IS_ENABLED(CONFIG_MULTITHREADING));
#endif

int main()
{
    k_spinlock native{};
    solar::kernel::SpinLockRef borrowed{native};
    auto guard = borrowed.acquire();
    (void)guard;
    return 0;
}
