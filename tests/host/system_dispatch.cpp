#include <cassert>

#include <solar/system/composer.hpp>

namespace fixture
{

struct Ping
{
    struct Request
    {
        unsigned sequence{};
    };
    struct Response
    {
        unsigned sequence{};
    };
};

struct Telemetry
{
    using Value = int;
};

struct Command
{
    using Value = float;
};
struct Alarm
{
    using Value = int;
};
struct Temperature
{
    using Value = float;
};

struct Contract
{
    using Actions = solar::TypeList<Ping>;
    using OutputStreams = solar::TypeList<Telemetry>;
    using InputStreams = solar::TypeList<Command>;
    using Events = solar::TypeList<Alarm>;
    using Metrics = solar::TypeList<Temperature>;
};

struct Cockpit
{
    using Contributions = solar::Contributions<solar::Handles<Ping>, solar::Publishes<Telemetry>,
                                               solar::Consumes<Command>, solar::Emits<Alarm>,
                                               solar::Records<Temperature>>;

    static Ping::Response handle(Ping, const Ping::Request& request)
    {
        return {.sequence = request.sequence + 1};
    }

    static int publish(Telemetry)
    {
        return 42;
    }

    static solar::Result<void> consume(Command, const float& value)
    {
        last_command = value;
        return {};
    }

    inline static float last_command{};
    static solar::Result<void> record(Temperature, const float& value)
    {
        temperature = value;
        return {};
    }
    inline static float temperature{};
};

struct Observer
{
    using Contributions = solar::Contributions<solar::Observes<Alarm>>;
    static void observe(Alarm, const int& value)
    {
        alarm = value;
    }
    inline static int alarm{};
};

struct Application;
using AppSystem = solar::system::System<
    Application, solar::Compose<solar::Contract<Contract>, solar::Components<Cockpit, Observer>>>;
using Endpoints = solar::system::Dispatch<Contract, solar::TypeList<Cockpit, Observer>>;

} // namespace fixture

int main()
{
    using namespace fixture;
    assert(AppSystem::boot());
    assert(Endpoints::call<Ping>({.sequence = 7}).sequence == 8);
    assert(Endpoints::publish<Telemetry>() == 42);
    assert(Endpoints::consume<Command>(0.75F));
    assert(Cockpit::last_command == 0.75F);
    Endpoints::emit<Alarm>(9);
    assert(Observer::alarm == 9);
    assert(Endpoints::record<Temperature>(24.0F));
    assert(Cockpit::temperature == 24.0F);
    assert(AppSystem::shutdown());
}
