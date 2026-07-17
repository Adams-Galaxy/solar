#include <cstdint>

#include <zephyr/sys/printk.h>

#include <solar/solar.hpp>

namespace app
{

// [declarations]
struct SetTarget
{
    std::int32_t value{};
    static constexpr solar::bus::Descriptor descriptor{.name = "control.set-target"};
};

struct Target
{
    using Value = std::int32_t;
    static constexpr solar::parameters::Descriptor descriptor{.name = "control.target"};
    static constexpr Value default_value = 0;
    using Validation = solar::parameters::Range<-100, 100, solar::parameters::Clamp>;
};

struct CommandsHandled
{
    using Value = std::uint64_t;
    using Instrument = solar::metrics::Counter;
    using Unit = solar::metrics::units::Count;
    static constexpr solar::metrics::Descriptor descriptor{.name = "control.commands-handled"};
};

struct TargetChanged
{
    using Payload = std::int32_t;
    static constexpr solar::events::Descriptor descriptor{
        .name = "control.target-changed",
        .severity = solar::events::Severity::Informational,
        .domain = solar::events::domain::Resource,
    };
};
// [declarations]

// [component]
struct Controller
{
    static constexpr solar::component::Descriptor descriptor{.name = "controller"};
    using Messages = solar::bus::Messages<SetTarget>;
    using Subscriptions = solar::bus::Subscriptions<
        solar::bus::On<SetTarget, solar::bus::delivery::Inline>>;
    using Parameters = solar::parameters::Parameters<Target>;
    using Events = solar::events::Events<TargetChanged>;
    using Metrics = solar::metrics::Metrics<CommandsHandled>;

    static solar::Result<void> handle(const SetTarget& command)
    {
        auto update = solar::parameters::set<Target>(command.value);
        if (!update) {
            return solar::fail<solar::Error>({.status = solar::status_of(update.error())});
        }
        if (auto counted = solar::metrics::inc<CommandsHandled>(); !counted) {
            return solar::fail<solar::Error>({.status = solar::status_of(counted.error())});
        }
        if (auto observed = solar::events::observe<TargetChanged>(update->effective_value);
            !observed) {
            return solar::fail<solar::Error>({.status = solar::status_of(observed.error())});
        }
        if (auto logged =
                solar::log::notice<Controller>("target changed to {}", update->effective_value);
            !logged) {
            return solar::fail<solar::Error>({.status = solar::status_of(logged.error())});
        }
        return {};
    }
};
// [component]

// [system]
using Blueprint = solar::Blueprint<
    solar::Facilities<Controller>,
    solar::log::Configuration<solar::log::Sinks<
        solar::log::To<solar::log::RetainedHistory,
                       solar::log::MinimumLevel<solar::log::Level::Notice>>>>>;
using System = solar::System<Blueprint>;
// [system]

} // namespace app

SOLAR_BIND_SYSTEM(app::System);

int main()
{
    // [flow]
    if (!solar::boot()) {
        return 1;
    }
    if (!solar::bus::emit<app::SetTarget>({.value = 42})) {
        return 2;
    }

    const auto target = solar::parameters::get<app::Target>();
    const auto handled = solar::metrics::get<app::CommandsHandled>();
    const auto latest_log = solar::log::latest();
    if (!target || *target != 42 || !handled || handled->value != 1 || !latest_log) {
        return 3;
    }
    if (!solar::stop()) {
        return 4;
    }
    // [flow]

    printk("Solar data pipeline passed\n");
    return 0;
}
