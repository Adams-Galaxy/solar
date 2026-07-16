#include <type_traits>

#include <zephyr/ztest.h>

#include "system_fixture.hpp"

namespace fixture = system_fixture;

static_assert(fixture::RobotSystem::valid);
static_assert(std::is_same_v<solar::bound_system_t<>, fixture::RobotSystem>);
static_assert(fixture::RobotSystem::catalog::Of<catalog_fixture::alpha::Tag>::size == 3);
static_assert(std::is_same_v<fixture::RobotSystem::graph::Category<fixture::Controller>,
                             solar::category::Service>);

ZTEST(solar_system, test_bound_frontend_and_canonical_state)
{
    auto& state =
        fixture::RobotSystem::StateSlot<fixture::ControlValue, fixture::ControlState, int>::value;
    state = 0;

#if defined(CONFIG_SOLAR_STRICT_CATALOG_BINDING)
    const auto first =
        solar::frontend::Operation<fixture::ControlPolicy, fixture::ControlValue>::call(2);
    zassert_true(first.has_value());
    zassert_equal(*first, 2);

    const auto out_of_line = fixture::StrictClient::increment(3);
    zassert_true(out_of_line.has_value());
    zassert_equal(*out_of_line, 5);

    const auto lazy = fixture::LazyClient::increment(1);
    zassert_true(lazy.has_value());
    zassert_equal(*lazy, 6);
#else
    solar::frontend::reset_catalog_for_test<fixture::RobotSystem, fixture::ControlPolicy>();
    const auto early = fixture::RelaxedClient::increment(1);
    zassert_false(early.has_value());
    zassert_equal(early.error(), solar::frontend::Error::NotReady);

    solar::frontend::bind_catalog<fixture::RobotSystem, fixture::ControlPolicy>();
    const auto first = fixture::RelaxedClient::increment(2);
    zassert_true(first.has_value());
    zassert_equal(*first, 2);
#endif

    zassert_equal(fixture::state_address_from_other_translation_unit(), &state);
    const auto other = fixture::call_from_other_translation_unit(1);
    zassert_true(other.has_value());
#if defined(CONFIG_SOLAR_STRICT_CATALOG_BINDING)
    zassert_equal(*other, 7);
#else
    zassert_equal(*other, 3);
#endif
}

ZTEST(solar_system, test_relaxed_availability_errors)
{
#if !defined(CONFIG_SOLAR_STRICT_CATALOG_BINDING)
    solar::frontend::bind_catalog<fixture::RobotSystem, fixture::ControlPolicy>();
    using Missing = solar::frontend::Operation<fixture::ControlPolicy, fixture::AbsentValue>;
    const auto missing = Missing::call(1);
    zassert_false(missing.has_value());
    zassert_equal(missing.error(), solar::frontend::Error::NotRegistered);

    solar::frontend::bind_disabled<fixture::ControlPolicy, fixture::DisabledApplication>();
    using Disabled = solar::frontend::Operation<fixture::ControlPolicy, fixture::ControlValue,
                                                fixture::DisabledApplication>;
    const auto disabled = Disabled::call(1);
    zassert_false(disabled.has_value());
    zassert_equal(disabled.error(), solar::frontend::Error::Disabled);
#endif
}

ZTEST_SUITE(solar_system, nullptr, nullptr, nullptr, nullptr, nullptr);
