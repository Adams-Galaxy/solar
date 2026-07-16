#define CONFIG_SOLAR_STRICT_CATALOG_BINDING 1

#include "system_fixture.hpp"

int main()
{
    using Operation = solar::frontend::Operation<system_fixture::ControlPolicy,
                                                 system_fixture::AbsentValue>;
    return Operation::call(1).has_value() ? 0 : 1;
}
