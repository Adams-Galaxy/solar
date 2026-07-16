#include <solar/system.hpp>

struct NotDependencies
{
    using Entries = solar::TypeList<>;
};

struct Component
{
    static constexpr solar::component::Descriptor descriptor{.name = "component"};
    using Dependencies = NotDependencies;
};

using InvalidSystem = solar::System<solar::Blueprint<solar::Devices<Component>>>;
static_assert(InvalidSystem::valid);
