#include <cassert>

#include <solar/generated/app.hpp>
#include <solar/system/composer.hpp>

namespace fixture_app
{

struct Application;
using parameters = solar::parameters::StaticStore<Application, generated::ParameterSchema>;

struct Cockpit
{
    using Contributions = solar::Contributions<
        solar::Uses<parameters, generated::DriveKp>, solar::Provides<generated::SystemStateData>,
        solar::Handles<generated::SystemPingAction>, solar::Publishes<generated::ImuEulerStream>,
        solar::Consumes<generated::DriveCommandStream>>;

    static generated::PingResponse handle(generated::SystemPingAction, const generated::Empty&)
    {
        return {};
    }
    static generated::Euler publish(generated::ImuEulerStream)
    {
        return {};
    }
    static generated::RobotState read(generated::SystemStateData)
    {
        return state;
    }
    static solar::Result<void> write(generated::SystemStateData, const generated::RobotState& value)
    {
        state = value;
        return {};
    }
    static solar::Result<void> consume(generated::DriveCommandStream,
                                       const generated::DriveCommand&)
    {
        return {};
    }
    inline static generated::RobotState state{};
};

template <typename Parameters> struct ReusableGainReader
{
    static auto read()
    {
        return Parameters::template get<generated::DriveKp>();
    }
};

using GainReader = ReusableGainReader<parameters>;

using system =
    solar::system::System<Application,
                          solar::Compose<solar::Contract<generated::Contract>,
                                         solar::Own<parameters>, solar::Components<Cockpit>>>;

} // namespace fixture_app

int main()
{
    using namespace fixture_app;
    assert(system::boot());
    assert(parameters::set<generated::DriveKp>(2.5F));
    const auto gain = parameters::get<generated::DriveKp>();
    assert(gain && *gain == 2.5F);
    const auto reusable_gain = GainReader::read();
    assert(reusable_gain && *reusable_gain == 2.5F);
    using Endpoints = solar::system::Dispatch<generated::Contract, solar::TypeList<Cockpit>>;
    assert(Endpoints::update<generated::SystemStateData>({.enabled = true}));
    assert(Endpoints::query<generated::SystemStateData>().enabled);
    assert(system::shutdown());
}
