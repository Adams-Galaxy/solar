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
    using Parameters = solar::TypeList<>;
    using Data = solar::TypeList<>;
    using Actions = solar::TypeList<Action>;
    using OutputStreams = solar::TypeList<>;
    using InputStreams = solar::TypeList<>;
    using Events = solar::TypeList<>;
    using Metrics = solar::TypeList<>;
};
struct Component
{
    static Action::Response execute(const Action::Request&)
    {
        return {};
    }
    using Endpoints = solar::Endpoints<solar::endpoint::Handle<Action, &execute>>;
    using Contributions = solar::Contributions<solar::Handles<Action>>;
};
struct Application;
using System =
    solar::system::System<Application,
                          solar::Compose<solar::Contract<Contract>, solar::Components<Component>>>;
static_assert(sizeof(System) != 0);
