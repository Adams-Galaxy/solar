#include "system_fixture.hpp"

namespace system_fixture
{

solar::Result<int, solar::frontend::Error> RelaxedClient::increment(int amount)
{
    return solar::frontend::Operation<ControlPolicy, ControlValue>::call(amount);
}

solar::Result<int, solar::frontend::Error> StrictClient::increment(int amount)
{
    return solar::frontend::Operation<ControlPolicy, ControlValue>::call(amount);
}

} // namespace system_fixture
