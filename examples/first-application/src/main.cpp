#include <zephyr/sys/printk.h>

#include <solar/solar.hpp>

// [component]
struct Platform
{
    static constexpr solar::component::Descriptor descriptor{
        .name = "example.platform",
        .description = "Minimal lifecycle-aware facility",
    };

    inline static bool initialized{};
    inline static bool running{};

    static solar::Result<void> init() noexcept
    {
        initialized = true;
        return {};
    }

    static solar::Result<void> start() noexcept
    {
        if (!initialized) {
            return solar::fail<solar::Error>({.status = solar::Status::NotReady});
        }
        running = true;
        return {};
    }

    static solar::Result<void> stop() noexcept
    {
        running = false;
        return {};
    }

    static solar::Result<void> deinit() noexcept
    {
        initialized = false;
        return {};
    }
};
// [component]

// [system]
using ApplicationBlueprint = solar::Blueprint<solar::Facilities<Platform>>;
using ApplicationSystem = solar::System<ApplicationBlueprint>;

SOLAR_BIND_SYSTEM(ApplicationSystem);
// [system]

// [main]
int main()
{
    const auto boot = solar::boot();
    if (!boot) {
        return -solar::to_errno(solar::status_of(boot.error()));
    }

    if (!Platform::running) {
        return -1;
    }

    printk("Solar first application passed\n");

    const auto stopped = solar::stop();
    return stopped ? 0 : -solar::to_errno(solar::status_of(stopped.error()));
}
// [main]
