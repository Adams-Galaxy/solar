#include <solar/system.hpp>

struct SubsystemTag;
struct Axis;
struct FirstPolicy;
struct SecondPolicy;

template <> struct solar::subsystem_policy_traits<SubsystemTag, FirstPolicy>
{
    static constexpr bool recognized = true;
    using Axis = ::Axis;
};

template <> struct solar::subsystem_policy_traits<SubsystemTag, SecondPolicy>
{
    static constexpr bool recognized = true;
    using Axis = ::Axis;
};

using InvalidSystem = solar::System<solar::Blueprint<
    solar::SubsystemConfiguration<SubsystemTag, FirstPolicy, SecondPolicy>>>;
static_assert(InvalidSystem::valid);
