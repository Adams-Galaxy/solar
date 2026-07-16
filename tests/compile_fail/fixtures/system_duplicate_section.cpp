#include <solar/system.hpp>

using InvalidSystem = solar::System<solar::Blueprint<solar::Devices<>, solar::Devices<>>>;
static_assert(InvalidSystem::valid);
