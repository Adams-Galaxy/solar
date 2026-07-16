#include <solar/events.hpp>
#include <solar/solar.hpp>

namespace
{

struct Event
{
    using Payload = void;
    static constexpr solar::events::Descriptor descriptor{.name = "disabled.event"};
};

using InvalidSystem = solar::System<solar::Blueprint<solar::Events<Event>>>;
static_assert(InvalidSystem::valid);

} // namespace

int main()
{
    return 0;
}
