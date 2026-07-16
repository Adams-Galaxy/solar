#include <solar/system.hpp>

struct SubsystemTag;
struct Axis;
struct UnsupportedPolicy;

template <> struct solar::subsystem_policy_traits<SubsystemTag, UnsupportedPolicy>
{
    static constexpr bool recognized = true;
    using Axis = ::Axis;
};

template <> struct solar::subsystem_configuration_traits<SubsystemTag>
{
    template <typename Policies> static constexpr bool validate = false;
};

using InvalidSystem =
    solar::System<solar::Blueprint<solar::SubsystemConfiguration<SubsystemTag, UnsupportedPolicy>>>;
static_assert(InvalidSystem::valid);
