#include <solar/solar.hpp>

struct Present
{
    static constexpr solar::component::Descriptor descriptor{.name = "present"};
};

struct Missing
{
    static constexpr solar::component::Descriptor descriptor{.name = "missing"};
};

struct NoRecovery
{
    static constexpr solar::component::Descriptor descriptor{.name = "no-recovery"};
    struct Health
    {};
};

struct NoSafeState
{};

struct NoFeedProvider
{};

#if SOLAR_FAIL_CASE == 1
using Configuration = solar::supervisor::Configuration<
    solar::supervisor::Policy<solar::supervisor::OnFault<Present>>>;
using InvalidSystem = solar::System<solar::Blueprint<solar::Devices<Present>, Configuration>>;
#elif SOLAR_FAIL_CASE == 2
using Configuration = solar::supervisor::Configuration<solar::supervisor::Policy<
    solar::supervisor::OnFault<NoRecovery, solar::supervisor::TryRecover<NoRecovery>>>>;
using InvalidSystem = solar::System<solar::Blueprint<solar::Devices<NoRecovery>, Configuration>>;
#elif SOLAR_FAIL_CASE == 3
using Configuration = solar::supervisor::Configuration<solar::supervisor::Policy<
    solar::supervisor::OnFault<Present, solar::supervisor::EnterSafeState<NoSafeState>>>>;
using InvalidSystem = solar::System<solar::Blueprint<solar::Devices<Present>, Configuration>>;
#elif SOLAR_FAIL_CASE == 4
using Configuration = solar::supervisor::Configuration<solar::supervisor::Watchdog<NoFeedProvider>>;
using InvalidSystem = solar::System<solar::Blueprint<solar::Devices<Present>, Configuration>>;
#elif SOLAR_FAIL_CASE == 5
using Configuration = solar::supervisor::Configuration<
    solar::supervisor::Policy<solar::supervisor::OnFault<Missing, solar::supervisor::Warn>>>;
using InvalidSystem = solar::System<solar::Blueprint<solar::Devices<Present>, Configuration>>;
#else
#error SOLAR_DIAGNOSTIC_UNKNOWN_SUPERVISOR_FAILURE_CASE
#endif

SOLAR_BIND_SYSTEM(InvalidSystem);

int main()
{
    (void)InvalidSystem::boot();
    return 0;
}
