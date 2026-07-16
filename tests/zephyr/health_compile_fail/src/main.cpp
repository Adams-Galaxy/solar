#include <solar/solar.hpp>

using namespace solar::literals;

#if SOLAR_FAIL_CASE == 1
struct InvalidComponent
{
    static constexpr solar::component::Descriptor descriptor{.name = "invalid-assess"};
    struct Health
    {
        static bool assess()
        {
            return true;
        }
    };
};
#elif SOLAR_FAIL_CASE == 2
struct InvalidCheck
{
    static bool check()
    {
        return true;
    }
};
struct InvalidComponent
{
    static constexpr solar::component::Descriptor descriptor{.name = "invalid-check"};
    struct Health
    {
        using Checks = solar::health::Checks<InvalidCheck>;
    };
};
#elif SOLAR_FAIL_CASE == 3
struct InvalidComponent
{
    static constexpr solar::component::Descriptor descriptor{.name = "missing-progress"};
};
#elif SOLAR_FAIL_CASE == 4
struct InvalidComponent
{
    static constexpr solar::component::Descriptor descriptor{.name = "oversized-stack-margin"};
    using Execution = solar::execution::Service<solar::execution::StackSize<1024>>;
    struct Health
    {
        using Checks = solar::health::Checks<solar::health::StackMargin<2048>>;
    };
    static solar::Status run(solar::StopToken)
    {
        return solar::Status::Ok;
    }
};
#else
#error SOLAR_DIAGNOSTIC_UNKNOWN_HEALTH_FAILURE_CASE
#endif

#if SOLAR_FAIL_CASE == 4
using InvalidSystem = solar::System<solar::Blueprint<solar::Services<InvalidComponent>>>;
#else
using InvalidSystem = solar::System<solar::Blueprint<solar::Devices<InvalidComponent>>>;
#endif
SOLAR_BIND_SYSTEM(InvalidSystem);

int main()
{
#if SOLAR_FAIL_CASE == 1
    (void)solar::health::assess<InvalidComponent>();
#elif SOLAR_FAIL_CASE == 2
    (void)solar::health::check<InvalidComponent, InvalidCheck>();
#elif SOLAR_FAIL_CASE == 3
    (void)solar::health::progress<InvalidComponent>();
#elif SOLAR_FAIL_CASE == 4
    using Monitor = solar::health::OwnedMonitor<InvalidComponent, solar::health::StackMargin<2048>>;
    solar::health::detail::refresh_stack<InvalidSystem, Monitor>(0);
#endif
    return 0;
}
