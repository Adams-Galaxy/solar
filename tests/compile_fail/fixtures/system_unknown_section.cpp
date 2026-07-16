#include <solar/system.hpp>

struct UnknownSection;

using InvalidSystem = solar::System<solar::Blueprint<UnknownSection>>;
static_assert(InvalidSystem::valid);
