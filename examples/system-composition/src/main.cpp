#include <zephyr/sys/printk.h>

#include <solar/system.hpp>

namespace app
{
struct Application;

struct Platform
{
    static constexpr std::string_view name = "composition.platform";
    using Dependencies = solar::TypeList<>;
    static solar::Result<void> initialize() noexcept
    {
        ready = true;
        return {};
    }
    static solar::Result<void> deinitialize() noexcept
    {
        ready = false;
        return {};
    }
    inline static bool ready{};
};

struct Sensor
{
    static constexpr std::string_view name = "composition.sensor";
    using Dependencies = solar::TypeList<Platform>;
    static solar::Result<void> initialize() noexcept
    {
        initialized = Platform::ready;
        return initialized ? solar::Result<void>{}
                           : solar::Result<void>{
                                 solar::fail<solar::Error>({.status = solar::Status::NotReady})};
    }
    static solar::Result<void> deinitialize() noexcept
    {
        initialized = false;
        return {};
    }
    inline static bool initialized{};
};

struct DiagnosticsAdapter
{
    template <typename Endpoint> static solar::Result<void> connect() noexcept
    {
        static_assert(std::same_as<Endpoint, Sensor>);
        connected = Endpoint::initialized;
        return connected ? solar::Result<void>{}
                         : solar::Result<void>{
                               solar::fail<solar::Error>({.status = solar::Status::NotReady})};
    }
    template <typename> static solar::Result<void> disconnect() noexcept
    {
        connected = false;
        return {};
    }
    inline static bool connected{};
};

using Composition =
    solar::Compose<solar::Own<Platform, Sensor>, solar::Connect<DiagnosticsAdapter, Sensor>>;
using System = solar::system::System<Application, Composition>;
} // namespace app

int main()
{
    const bool passed =
        app::System::boot() && app::Sensor::initialized && app::DiagnosticsAdapter::connected;
    printk("Solar system composition %s\n", passed ? "passed" : "failed");
    return passed && app::System::shutdown() ? 0 : -1;
}
