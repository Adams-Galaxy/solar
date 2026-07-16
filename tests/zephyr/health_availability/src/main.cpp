#include <zephyr/ztest.h>

#include <solar/solar.hpp>

struct Probe
{
    static constexpr solar::component::Descriptor descriptor{.name = "probe"};
};

using ProbeSystem = solar::System<solar::Blueprint<solar::Devices<Probe>>>;
SOLAR_BIND_SYSTEM(ProbeSystem);

ZTEST(solar_health_availability, test_binding_window)
{
    auto early = solar::health::report<Probe>(solar::health::nominal());
    zassert_false(early.has_value());
    zassert_equal(early.error().reason, solar::health::Reason::NotReady);

    zassert_true(ProbeSystem::boot().has_value());
    zassert_true(solar::health::report<Probe>(solar::health::nominal()).has_value());
    zassert_true(ProbeSystem::stop().has_value());
}

ZTEST_SUITE(solar_health_availability, nullptr, nullptr, nullptr, nullptr, nullptr);
