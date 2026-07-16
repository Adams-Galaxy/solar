#include <solar/system.hpp>

struct Repeated
{
    static constexpr solar::component::Descriptor descriptor{.name = "repeated"};
};

using InvalidSystem =
    solar::System<solar::Blueprint<solar::Devices<Repeated>, solar::Services<Repeated>>>;
static_assert(InvalidSystem::valid);
