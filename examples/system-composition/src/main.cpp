#include <atomic>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <solar/solar.hpp>

namespace app
{

// [components]
struct Platform
{
    static constexpr solar::component::Descriptor descriptor{.name = "composition.platform"};
    inline static bool ready{};

    static solar::Result<void> init() noexcept
    {
        ready = true;
        return {};
    }

    static solar::Result<void> deinit() noexcept
    {
        ready = false;
        return {};
    }
};

struct Sensor
{
    static constexpr solar::component::Descriptor descriptor{.name = "composition.sensor"};
    using Dependencies = solar::Dependencies<Platform>;

    static solar::Result<void> init() noexcept
    {
        return Platform::ready
                   ? solar::Result<void>{}
                   : solar::Result<void>{
                         solar::fail<solar::Error>({.status = solar::Status::NotReady})};
    }
};
// [components]

// [contribution]
inline std::atomic_uint sample_count{};

struct SampleBehavior
{
    static void execute() noexcept
    {
        sample_count.fetch_add(1, std::memory_order_release);
    }
};

using SampleWork = solar::execution::OnDemand<
    "composition.sample", SampleBehavior, solar::execution::SystemWorkQueue,
    solar::execution::DependsOn<Sensor>>;

struct Controller
{
    static constexpr solar::component::Descriptor descriptor{.name = "composition.controller"};
    using Dependencies = solar::Dependencies<Sensor>;
    using Tasks = solar::execution::Tasks<SampleWork>;
};
// [contribution]

// [blueprint]
using Blueprint = solar::Blueprint<solar::Devices<Sensor>,
                                   solar::Facilities<Platform, Controller>>;
using System = solar::System<Blueprint>;

} // namespace app

SOLAR_BIND_SYSTEM(app::System);
// [blueprint]

int main()
{
    if (!solar::boot()) {
        return -1;
    }

    if (!solar::execution::submit<app::SampleWork>()) {
        return -2;
    }
    for (int attempt = 0; attempt < 20 && app::sample_count.load(std::memory_order_acquire) == 0;
         ++attempt) {
        k_sleep(K_MSEC(1));
    }

    const auto sensor = solar::lifecycle::record<app::Sensor>();
    const bool passed = sensor && app::sample_count.load(std::memory_order_acquire) == 1;
    printk("Solar system composition %s\n", passed ? "passed" : "failed");

    const auto stopped = solar::stop();
    return passed && stopped ? 0 : -3;
}
