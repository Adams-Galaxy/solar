#define CONFIG_SOLAR_STRICT_CATALOG_BINDING 1

#include "system_api_fixture.hpp"

struct Client
{
    template <typename Application = solar::DefaultApplication>
    static auto increment(int amount)
    {
        return solar::frontend::Operation<system_fixture::ControlPolicy,
                                          system_fixture::ControlValue, Application>::call(amount);
    }
};

int main()
{
    return Client::increment(1).has_value() ? 0 : 1;
}
