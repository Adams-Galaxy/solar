#include <solar/system.hpp>

struct Missing;

struct Component
{
    static constexpr solar::component::Descriptor descriptor{.name = "component"};
    using Dependencies = solar::Dependencies<Missing>;
};

using InvalidSystem = solar::System<solar::Blueprint<solar::Devices<Component>>>;
static_assert(InvalidSystem::valid);
