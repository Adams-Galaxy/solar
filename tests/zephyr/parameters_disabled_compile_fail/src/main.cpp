#include <solar/parameters.hpp>
#include <solar/solar.hpp>

namespace
{

struct Parameter
{
    using Value = int;
    static constexpr solar::parameters::Descriptor descriptor{.name = "disabled.parameter"};
    static constexpr Value default_value = 1;
};

using InvalidSystem = solar::System<solar::Blueprint<solar::Parameters<Parameter>>>;

static_assert(InvalidSystem::valid);

} // namespace

int main()
{
    return 0;
}
