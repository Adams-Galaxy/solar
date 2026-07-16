#include <solar/bus.hpp>
#include <solar/solar.hpp>

namespace
{

struct Message
{
    int value{};
    static constexpr solar::bus::Descriptor descriptor{.name = "disabled.message"};
};

using InvalidSystem = solar::System<solar::Blueprint<solar::Bus<solar::bus::Messages<Message>>>>;

static_assert(InvalidSystem::valid);

} // namespace

int main()
{
    return 0;
}
