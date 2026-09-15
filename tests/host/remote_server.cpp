#include <array>
#include <cassert>
#include <optional>

#include <solar/parameters/store.hpp>
#include <solar/remote/server.hpp>
#include <solar/system/composer.hpp>
#include <solar/system/contributions.hpp>

namespace fixture
{

struct Gain
{
    using Value = float;
    static constexpr const char* name = "drive.gain";
    static constexpr Value default_value = 1.0F;
};

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
    static constexpr std::uint32_t id = 10;
    static constexpr bool input = false;
    static constexpr std::uint32_t maximum_rate_hz = 100;
};

struct Command
{
    using Value = float;
    static constexpr std::uint32_t id = 11;
    static constexpr bool input = true;
    static constexpr std::uint32_t maximum_rate_hz = 100;
};

struct Declarations
{
    using Actions = solar::TypeList<Ping>;
    using OutputStreams = solar::TypeList<Telemetry>;
    using InputStreams = solar::TypeList<Command>;
};

struct Cockpit
{
    using Contributions = solar::Contributions<solar::Handles<Ping>, solar::Publishes<Telemetry>,
                                               solar::Consumes<Command>>;
    static Ping::Response handle(Ping, const Ping::Request& request)
    {
        return {.sequence = request.sequence + 1};
    }
    static std::optional<int> publish(Telemetry)
    {
        return ++sample;
    }
    static solar::Result<void> consume(Command, const float& value)
    {
        command = value;
        return {};
    }
    inline static int sample{};
    inline static float command{};
};

struct Application;
using Parameters = solar::parameters::StaticStore<Application, solar::parameters::Schema<Gain>>;
using Endpoints = solar::system::Dispatch<Declarations, solar::TypeList<Cockpit>>;
struct Contract
{
    using Parameters = fixture::Parameters;
    using Endpoints = fixture::Endpoints;
    using Declarations = fixture::Declarations;
};

inline constexpr solar::remote::ServerConfig config{
    .maximum_sessions = 2,
    .maximum_subscriptions = 3,
    .maximum_inflight_requests = 1,
    .maximum_payload_bytes = 16,
    .initial_input_credit = 1,
};

using Remote = solar::remote::StaticServer<Application, Contract, config>;
using System =
    solar::system::System<Application,
                          solar::Compose<solar::Own<Parameters, Remote>, solar::Components<Cockpit>,
                                         solar::Contract<Declarations>>>;

struct Sink
{
    template <typename Stream>
    solar::Result<void> send(solar::remote::Session session, const typename Stream::Value& value)
    {
        sessions[count] = session;
        values[count] = value;
        ++count;
        return {};
    }

    std::array<solar::remote::Session, 4> sessions{};
    std::array<int, 4> values{};
    std::size_t count{};
};

} // namespace fixture

int main()
{
    using namespace fixture;
    assert(Parameters::initialize());
    solar::remote::Server<Contract, config> server;
    assert(server.initialize());
    assert(server.start());
    const auto first = server.open_session();
    const auto second = server.open_session();
    assert(first && second);
    assert(!server.open_session());
    const auto request = server.begin_request(*first, 42, 8);
    assert(request);
    assert(!server.begin_request(*first, 42, 8));
    assert(!server.begin_request(*second, 43, 8));
    assert(!server.begin_request(*second, 43, 17));
    assert(server.complete_request(*request));

    assert(server.set<Gain>(*first, 2.5F));
    assert(*server.get<Gain>(*second) == 2.5F);
    assert(server.call<Ping>(*first, {.sequence = 4})->sequence == 5);

    assert(server.subscribe<Telemetry>(*first, 20));
    Sink sink;
    assert(*server.publish<Telemetry>(1, sink, 0) == 1);
    assert(sink.count == 1 && sink.sessions[0] == *first && sink.values[0] == 1);
    assert(server.subscribe<Telemetry>(*second, 10));
    assert(*server.publish<Telemetry>(2, sink, 1'000) == 1);
    assert(*server.publish<Telemetry>(3, sink, 101'000) == 2);

    assert(server.open_input<Command>(*second, 50));
    assert(server.consume<Command>(*second, 0.75F));
    assert(Cockpit::command == 0.75F);
    assert(!server.consume<Command>(*second, 0.5F));
    assert(server.grant<Command>(*second, 2));
    assert(server.consume<Command>(*second, 0.5F));
    assert(!server.consume<Command>(*first, 0.25F));

    assert(server.close_session(*first));
    assert(!server.get<Gain>(*first));
    assert(server.session_count() == 1);
    assert(server.stop());
    assert(server.deinitialize());

    // The same owner participates in generic System composition without any
    // Remote-specific branch in the composer.
    assert(System::boot());
    const auto composed = Remote::open_session();
    assert(composed);
    assert(Remote::call<Ping>(*composed, {.sequence = 8})->sequence == 9);
    assert(System::shutdown());
}
