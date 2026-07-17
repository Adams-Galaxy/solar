#include <atomic>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <solar/solar.hpp>

namespace app
{

inline std::atomic_bool connected{true};
inline std::atomic_uint recovery_attempts{};
inline std::atomic_uint safe_state_entries{};
inline std::atomic_uint watchdog_feeds{};

// [device]
struct Imu
{
    static constexpr solar::component::Descriptor descriptor{.name = "imu"};

    struct Health
    {
        static solar::Result<solar::health::Assessment> assess()
        {
            return connected.load() ? solar::health::nominal() : solar::health::faulted();
        }

        static solar::Result<void> recover()
        {
            recovery_attempts.fetch_add(1);
            return solar::fail<solar::Error>({.status = solar::Status::Error});
        }
    };
};
// [device]

struct SafeState
{
    static solar::Result<void> enter()
    {
        safe_state_entries.fetch_add(1);
        return {};
    }
};

struct Watchdog
{
    static solar::Result<void> start() { return {}; }
    static solar::Result<void> feed()
    {
        watchdog_feeds.fetch_add(1);
        return {};
    }
    static solar::Result<void> stop() { return {}; }
};

// [policy]
using Policy = solar::supervisor::Policy<
    solar::supervisor::OnFault<Imu, solar::supervisor::Warn,
                               solar::supervisor::TryRecover<Imu>>,
    solar::supervisor::OnRecoveryFailure<
        Imu, solar::supervisor::EnterSafeState<SafeState>>>;

using Blueprint = solar::Blueprint<
    solar::Devices<Imu>,
    solar::supervisor::Configuration<Policy, solar::supervisor::Watchdog<Watchdog>>>;
using System = solar::System<Blueprint>;
// [policy]

} // namespace app

SOLAR_BIND_SYSTEM(app::System);

int main()
{
    if (!solar::boot()) {
        return 1;
    }

    for (int attempt = 0; attempt < 200; ++attempt) {
        const auto state = solar::supervisor::state();
        if (state && state->cycles > 0) {
            break;
        }
        k_sleep(K_MSEC(1));
    }

    app::connected.store(false);
    solar::supervisor::wake();

    for (int attempt = 0; attempt < 200 && app::safe_state_entries.load() == 0; ++attempt) {
        k_sleep(K_MSEC(1));
    }

    const auto health = solar::health::record<app::Imu>();
    const auto response = solar::supervisor::record<app::Imu>();
    if (!health || health->condition != solar::health::Condition::Faulted ||
        !response || response->last_action != solar::supervisor::Action::EnterSafeState ||
        app::recovery_attempts.load() != 1 || app::safe_state_entries.load() != 1 ||
        app::watchdog_feeds.load() == 0) {
        return 2;
    }

    if (!solar::stop()) {
        return 3;
    }
    printk("Solar supervised device passed\n");
    return 0;
}
