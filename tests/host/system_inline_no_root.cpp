#include "system_api_fixture.hpp"

namespace system_fixture
{

solar::Result<int, solar::frontend::Error> call_from_header_without_root(int amount)
{
    return RelaxedClient::increment(amount);
}

} // namespace system_fixture
