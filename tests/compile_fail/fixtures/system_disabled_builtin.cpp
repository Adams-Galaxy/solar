#include <solar/system.hpp>

struct DisabledFacility
{
    static constexpr solar::component::Descriptor descriptor{.name = "disabled"};
};

template <> struct solar::builtin_traits<DisabledFacility>
{
    static constexpr bool enabled = false;
    static constexpr bool always_present = false;
    using Requirements = solar::TypeList<>;

    template <typename CatalogSet> static constexpr bool demanded = false;
};

using InvalidSystem =
    solar::System<solar::Blueprint<solar::Builtins<DisabledFacility>>>;
static_assert(InvalidSystem::valid);
