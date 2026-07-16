#include <solar/system.hpp>

struct Right;

struct Left
{
    static constexpr solar::component::Descriptor descriptor{.name = "left"};
    using Dependencies = solar::Dependencies<Right>;
};

struct Right
{
    static constexpr solar::component::Descriptor descriptor{.name = "right"};
    using Dependencies = solar::Dependencies<Left>;
};

using InvalidSystem =
    solar::System<solar::Blueprint<solar::Devices<Left>, solar::Services<Right>>>;
static_assert(InvalidSystem::valid);
