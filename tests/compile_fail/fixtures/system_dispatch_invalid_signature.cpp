#include <solar/system/composer.hpp>

struct Action
{
    struct Request
    {};
    struct Response
    {};
};
struct Contract
{
    using Actions = solar::TypeList<Action>;
    using OutputStreams = solar::TypeList<>;
    using InputStreams = solar::TypeList<>;
};
struct Component
{
    using Contributions = solar::Contributions<solar::Handles<Action>>;
    static int handle(Action, const Action::Request&)
    {
        return 0;
    }
};
struct Application;
using Invalid =
    solar::system::System<Application,
                          solar::Compose<solar::Contract<Contract>, solar::Components<Component>>>;
static_assert(sizeof(Invalid) > 0);
