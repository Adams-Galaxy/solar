#include <atomic>

#include <zephyr/kernel.h>

#include <solar/kernel.hpp>

namespace
{

solar::kernel::Semaphore observed;
std::atomic_bool valid_observation{false};

void observe(const solar::kernel::FatalError& error) noexcept
{
    const auto latched = solar::kernel::fatal_reason();
    if (error.reason == solar::kernel::FatalReason::KernelOops &&
        error.native_reason == K_ERR_KERNEL_OOPS && error.trigger_status == solar::Status::Error &&
        latched && latched->reason == error.reason &&
        latched->native_reason == error.native_reason) {
        valid_observation.store(true, std::memory_order_release);
    }
    observed.give();
}

void trigger_oops(void*) noexcept
{
    k_oops();
}

} // namespace

int main()
{
    static_assert(solar::kernel::fatal_bridge_available);
    if (!solar::kernel::install_fatal_observer(&observe)) {
        return 1;
    }
    const auto duplicate = solar::kernel::install_fatal_observer(&observe);
    if (duplicate || solar::status_of(duplicate.error()) != solar::Status::Already) {
        return 2;
    }
    if (solar::status_of(solar::kernel::fatal_reason().error()) != solar::Status::NotReady) {
        return 3;
    }
    solar::kernel::Thread<1024> thread;
    const auto launched =
        thread.launch(&trigger_oops, {.priority = solar::kernel::Priority::preemptive<1>()});
    if (!launched) {
        return 4;
    }
    if (!observed.take(solar::kernel::Timeout::after(solar::Milliseconds{100}))) {
        return 5;
    }
    if (!valid_observation.load(std::memory_order_acquire)) {
        return 6;
    }
    if (!thread.join(solar::kernel::Timeout::after(solar::Milliseconds{100}))) {
        return 7;
    }
    printk("SOLAR_FATAL_BRIDGE_OBSERVED_REAL_OOPS\n");
    return 0;
}
