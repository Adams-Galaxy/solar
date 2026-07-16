#if SOLAR_FAIL_CASE == 8
#include <solar/lifecycle.hpp>
#else
#include <solar/solar.hpp>
#endif

namespace
{

struct ArbitraryResult
{};

struct ValidComponent
{
    static constexpr solar::component::Descriptor descriptor{.name = "valid"};
};

struct OtherComponent
{
    static constexpr solar::component::Descriptor descriptor{.name = "other"};
};

#if SOLAR_FAIL_CASE == 1
struct InvalidComponent
{
    static constexpr solar::component::Descriptor descriptor{.name = "invalid.bool_init"};
    static bool init()
    {
        return true;
    }
};
#elif SOLAR_FAIL_CASE == 2
struct InvalidComponent
{
    static constexpr solar::component::Descriptor descriptor{.name = "invalid.integer_start"};
    static int start()
    {
        return 0;
    }
};
#elif SOLAR_FAIL_CASE == 3
struct InvalidComponent
{
    static constexpr solar::component::Descriptor descriptor{.name = "invalid.void_stop"};
    static void stop() {}
};
#elif SOLAR_FAIL_CASE == 4
struct InvalidComponent
{
    static constexpr solar::component::Descriptor descriptor{.name = "invalid.arbitrary_deinit"};
    static ArbitraryResult deinit()
    {
        return {};
    }
};
#elif SOLAR_FAIL_CASE == 5
using InvalidSystem = solar::System<solar::Blueprint<solar::Facilities<ValidComponent>>>;
#elif SOLAR_FAIL_CASE == 6
struct InvalidComponent
{
    static constexpr solar::component::Descriptor descriptor{.name = "ceiling.second"};
};
#elif SOLAR_FAIL_CASE == 7
using InvalidSystem = solar::System<solar::Blueprint<solar::Facilities<ValidComponent>>>;
struct InvalidApplication;
#elif SOLAR_FAIL_CASE == 8
using InvalidSystem = solar::System<solar::Blueprint<solar::Facilities<ValidComponent>>>;
#else
#error SOLAR_DIAGNOSTIC_UNKNOWN_LIFECYCLE_FAILURE_CASE
#endif

#if SOLAR_FAIL_CASE != 5 && SOLAR_FAIL_CASE != 7 && SOLAR_FAIL_CASE != 8
using InvalidSystem =
    solar::System<solar::Blueprint<solar::Facilities<ValidComponent, InvalidComponent>>>;
#endif

} // namespace

#if SOLAR_FAIL_CASE == 7
SOLAR_BIND_SYSTEM_FOR(InvalidApplication, InvalidSystem);
#endif

int main()
{
#if SOLAR_FAIL_CASE == 5
    (void)solar::lifecycle::Engine<InvalidSystem>::record<OtherComponent>();
#elif SOLAR_FAIL_CASE == 7
    (void)solar::graph::Of<InvalidApplication>::dependencies<OtherComponent>();
#else
    (void)solar::lifecycle::Engine<InvalidSystem>::state();
#endif
    return 0;
}
