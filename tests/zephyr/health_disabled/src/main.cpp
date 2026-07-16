#include <zephyr/ztest.h>

#include <solar/health.hpp>
#include <solar/solar.hpp>

struct DisabledProbe
{
    static constexpr solar::component::Descriptor descriptor{.name = "disabled-probe"};
};

using DisabledSystem = solar::System<solar::Blueprint<solar::Devices<DisabledProbe>>>;
SOLAR_BIND_SYSTEM(DisabledSystem);

ZTEST(solar_health_disabled, test_storage_free_disabled_frontend)
{
    static_assert(!solar::health::enabled);
    static_assert(!solar::contains_v<solar::health::Facility, typename DisabledSystem::Components>);

    auto early = solar::health::report<DisabledProbe>(solar::health::nominal());
    zassert_false(early.has_value());
    zassert_equal(early.error().reason, solar::health::Reason::NotReady);

    zassert_true(DisabledSystem::boot().has_value());
    auto disabled = solar::health::report<DisabledProbe>(solar::health::nominal());
    zassert_false(disabled.has_value());
    zassert_equal(disabled.error().reason, solar::health::Reason::Disabled);
    zassert_true(DisabledSystem::stop().has_value());
}

ZTEST_SUITE(solar_health_disabled, nullptr, nullptr, nullptr, nullptr, nullptr);
