#include <solar/system.hpp>

struct SubsystemTag;
struct Axis;
struct UnavailablePolicy;

template <> struct solar::subsystem_policy_traits<SubsystemTag, UnavailablePolicy>
{
    static constexpr bool recognized = true;
    static constexpr bool available = false;
    using Axis = ::Axis;
};

using InvalidSystem = solar::System<solar::Blueprint<
    solar::SubsystemConfiguration<SubsystemTag, UnavailablePolicy>>>;
static_assert(InvalidSystem::valid);
