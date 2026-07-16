#include <solar/solar.hpp>

namespace
{

struct MemorySink
{
    static constexpr solar::log::SinkDescriptor descriptor{.name = "memory"};
};

using InvalidSystem = solar::System<solar::Blueprint<
    solar::log::Configuration<solar::log::Sinks<solar::log::To<MemorySink>>>>>;
static_assert(InvalidSystem::valid);

} // namespace

int main()
{
    return 0;
}
