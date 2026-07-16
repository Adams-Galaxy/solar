#include <solar/solar.hpp>

using namespace solar::literals;

namespace
{

struct GoodBehavior
{
    static void execute() {}
};

struct Resource
{
    static constexpr solar::component::Descriptor descriptor{.name = "resource"};
};

using Queue = solar::execution::WorkQueue<"queue", solar::execution::StackSize<1024>>;

#if SOLAR_FAIL_CASE == 1
using InvalidTask = solar::execution::OnDemand<"missing-target", GoodBehavior>;
using InvalidSystem = solar::System<solar::Blueprint<solar::Execution<InvalidTask>>>;
#elif SOLAR_FAIL_CASE == 2
struct BadBehavior
{
    static bool execute()
    {
        return true;
    }
};
using InvalidTask =
    solar::execution::OnDemand<"bad-behavior", BadBehavior, solar::execution::SystemWorkQueue>;
using InvalidSystem = solar::System<solar::Blueprint<solar::Execution<InvalidTask>>>;
#elif SOLAR_FAIL_CASE == 3
struct BadService
{
    static constexpr solar::component::Descriptor descriptor{.name = "bad-service"};
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Services<BadService>>>;
#elif SOLAR_FAIL_CASE == 4
struct BadService
{
    static constexpr solar::component::Descriptor descriptor{.name = "bad-service"};
    using Execution = solar::execution::Service<>;
    static bool run(solar::StopToken)
    {
        return true;
    }
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Services<BadService>>>;
#elif SOLAR_FAIL_CASE == 5
using InvalidTask = solar::execution::OnDemand<"unregistered-queue", GoodBehavior, Queue>;
using InvalidSystem = solar::System<solar::Blueprint<solar::Execution<InvalidTask>>>;
#elif SOLAR_FAIL_CASE == 6
struct NotExecutor
{
    static constexpr solar::component::Descriptor descriptor{.name = "not-executor"};
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Executors<NotExecutor>>>;
#elif SOLAR_FAIL_CASE == 7
using InvalidTask = solar::execution::OnDemand<"missing-dependency", GoodBehavior,
                                               solar::execution::SystemWorkQueue,
                                               solar::execution::DependsOn<Resource>>;
using InvalidSystem = solar::System<solar::Blueprint<solar::Execution<InvalidTask>>>;
#elif SOLAR_FAIL_CASE == 8
using One = solar::execution::OnDemand<"one", GoodBehavior, solar::execution::SystemWorkQueue>;
using Two = solar::execution::OnDemand<"two", GoodBehavior, solar::execution::SystemWorkQueue>;
using Three = solar::execution::OnDemand<"three", GoodBehavior, solar::execution::SystemWorkQueue>;
using InvalidSystem = solar::System<solar::Blueprint<solar::Execution<One, Two, Three>>>;
#elif SOLAR_FAIL_CASE == 9
using InvalidTask =
    solar::execution::OnDemand<"duplicate-axis", GoodBehavior, solar::execution::SystemWorkQueue,
                               solar::execution::stop::Drain,
                               solar::execution::stop::CancelPending>;
using InvalidSystem = solar::System<solar::Blueprint<solar::Execution<InvalidTask>>>;
#elif SOLAR_FAIL_CASE == 10
using Invalid = solar::execution::Counted<0>;
using InvalidSystem = solar::System<solar::Blueprint<>>;
#else
#error SOLAR_DIAGNOSTIC_UNKNOWN_EXECUTION_FAILURE_CASE
#endif

} // namespace

int main()
{
#if SOLAR_FAIL_CASE == 10
    (void)sizeof(Invalid);
#else
    (void)InvalidSystem::boot();
#endif
    return 0;
}
