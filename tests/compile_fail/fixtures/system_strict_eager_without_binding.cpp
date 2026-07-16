#define CONFIG_SOLAR_STRICT_CATALOG_BINDING 1

#include "system_api_fixture.hpp"

struct EagerClient
{
    static auto increment(int amount)
    {
        return solar::frontend::Operation<system_fixture::ControlPolicy,
                                          system_fixture::ControlValue>::call(amount);
    }
};
