#include <zephyr/kernel.h>

#include <solar/kernel.hpp>

namespace
{

void observe(const solar::kernel::FatalError&) noexcept {}

} // namespace

int main()
{
    static_assert(solar::kernel::fatal_bridge_available);
    if (solar::kernel::install_fatal_observer(&observe) != solar::Status::Ok) {
        return 1;
    }
    if (solar::kernel::install_fatal_observer(&observe) != solar::Status::Already) {
        return 2;
    }
    if (solar::kernel::fatal_reason().error() != solar::Status::NotReady) {
        return 3;
    }
    printk("SOLAR_FATAL_BRIDGE_OK\n");
    return 0;
}
