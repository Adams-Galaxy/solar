#include <cassert>
#include <type_traits>

#include "system_fixture.hpp"

namespace fixture = system_fixture;

using DefaultOperation = solar::frontend::Operation<fixture::ControlPolicy, fixture::ControlValue>;
using TaggedOperation = typename solar::frontend::Of<fixture::TestApplication>::template Operation<
    fixture::ControlPolicy, fixture::ControlValue>;

static_assert(solar::frontend::strict);
static_assert(
    std::is_same_v<decltype(DefaultOperation::call(1)), decltype(TaggedOperation::call(1))>);

int main()
{
    fixture::RobotSystem::StateSlot<fixture::ControlValue, fixture::ControlState, int>::value = 0;

    const auto direct = DefaultOperation::call(2);
    assert(direct && *direct == 2);

    const auto out_of_line = fixture::StrictClient::increment(3);
    assert(out_of_line && *out_of_line == 5);

    const auto lazy = fixture::LazyClient::increment(1);
    assert(lazy && *lazy == 6);

    const auto tagged = TaggedOperation::call(1);
    assert(tagged && *tagged == 7);
}
