#include <cassert>

#include <solar/application.hpp>

namespace fixture_app
{

using parameters = solar::parameters::StaticStore<Application, generated::ParameterSchema>;

struct Cockpit
{
    using State = contract::data::system::State;
    using Ping = contract::actions::system::Ping;
    using EulerStream = contract::streams::imu::Euler;
    using DriveStream = contract::streams::drive::Command;
    using DriveKp = contract::parameters::drive::Kp;

    static generated::PingResponse ping(const generated::Empty&)
    {
        return {};
    }
    static generated::Euler euler()
    {
        return {};
    }
    static generated::RobotState read_state()
    {
        return state;
    }
    static solar::Result<void> write_state(const generated::RobotState& value)
    {
        state = value;
        return {};
    }
    static solar::Result<void> drive(const generated::DriveCommand&)
    {
        return {};
    }
    using Endpoints =
        solar::Endpoints<DriveKp::Use<parameters>, State::ReadWrite<&read_state, &write_state>,
                         Ping::Handle<&ping>, EulerStream::Output<&euler>,
                         DriveStream::Input<&drive>>;
    inline static generated::RobotState state{};
};

struct Application
{
    using Devices = solar::Devices<>;
    using Services = solar::Services<Cockpit>;
};

static_assert(std::is_same_v<parameters, solar::application::Parameters<Application>>);

template <typename Parameters> struct ReusableGainReader
{
    static auto read()
    {
        return Parameters::template get<generated::DriveKp>();
    }
};

using GainReader = ReusableGainReader<parameters>;

using system = solar::System<Application>;

using runner =
    solar::execution::ServiceRunner<Application, Cockpit, 2048, solar::PreemptivePriority<2>>;
using explicit_composition =
    solar::Compose<solar::Contract<generated::Contract>, solar::Own<parameters, runner>,
                   solar::Components<Cockpit>>;
static_assert(std::is_same_v<typename solar::application::Specification<Application>::Contract,
                             generated::Contract>);
static_assert(std::is_same_v<solar::application::Composition<Application>, explicit_composition>);

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
    using Dispatch = solar::system::Dispatch<generated::Contract, solar::TypeList<Cockpit>>;
    assert(Dispatch::update<generated::SystemStateData>({.enabled = true}));
    assert(Dispatch::query<generated::SystemStateData>().enabled);
    assert(system::shutdown());
}
