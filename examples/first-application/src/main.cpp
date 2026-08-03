#include <zephyr/sys/printk.h>

#include <solar/application.hpp>

namespace app
{
struct Application;

struct Contract
{
    using Data = solar::TypeList<>;
    using Actions = solar::TypeList<>;
    using OutputStreams = solar::TypeList<>;
    using InputStreams = solar::TypeList<>;
    using Events = solar::TypeList<>;
    using Metrics = solar::TypeList<>;
};

/** Small explicitly owned module; it can also be used without System. */
struct Platform
{
    static constexpr std::string_view name = "example.platform";
    using Dependencies = solar::TypeList<>;

    static solar::Result<void> initialize() noexcept
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

    static solar::Result<void> deinitialize() noexcept
    {
        initialized = false;
        return {};
    }

    inline static bool initialized{};
    inline static bool running{};
};

struct Application
{
    using Contract = app::Contract;
    using Devices = solar::Devices<Platform>;
};

using System = solar::System<Application>;
} // namespace app

int main()
{
    if (!app::System::boot() || !app::Platform::running) {
        return -1;
    }
    printk("Solar first application passed\n");
    return app::System::shutdown() ? 0 : -2;
}
