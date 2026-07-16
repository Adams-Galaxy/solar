#include "system_fixture.hpp"

namespace system_fixture
{

int* state_address_from_other_translation_unit()
{
    return &RobotSystem::StateSlot<ControlValue, ControlState, int>::value;
}

solar::Result<int, solar::frontend::Error> call_from_other_translation_unit(int amount)
{
    return solar::frontend::Operation<ControlPolicy, ControlValue>::call(amount);
}

#if defined(CONFIG_SOLAR_STRICT_CATALOG_BINDING)
solar::Result<int, solar::frontend::Error> StrictClient::increment(int amount)
{
    return solar::frontend::Operation<ControlPolicy, ControlValue>::call(amount);
}
#endif

} // namespace system_fixture
