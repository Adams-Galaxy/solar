#include <solar/system.hpp>

struct MissingFacility;

struct SelectedFacility
{
    static constexpr solar::component::Descriptor descriptor{.name = "selected"};
};

template <> struct solar::builtin_traits<SelectedFacility>
{
    static constexpr bool enabled = true;
    static constexpr bool always_present = false;
    using Requirements = solar::TypeList<MissingFacility>;

    template <typename CatalogSet> static constexpr bool demanded = false;
};

using InvalidSystem =
    solar::System<solar::Blueprint<solar::Builtins<SelectedFacility>>>;
static_assert(InvalidSystem::valid);
