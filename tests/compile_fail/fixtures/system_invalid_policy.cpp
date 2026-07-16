#include <solar/system.hpp>

struct SubsystemTag;
struct ForeignPolicy;

using InvalidSystem = solar::System<solar::Blueprint<
    solar::SubsystemConfiguration<SubsystemTag, ForeignPolicy>>>;
static_assert(InvalidSystem::valid);
