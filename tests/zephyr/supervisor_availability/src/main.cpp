#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <solar/solar.hpp>

namespace fixture
{

struct Root
{
    static constexpr solar::component::Descriptor descriptor{.name = "root"};
};

using System = solar::System<solar::Blueprint<solar::Facilities<Root>>>;

} // namespace fixture

SOLAR_BIND_SYSTEM(fixture::System);

ZTEST(solar_supervisor_availability, test_default_policy_is_observation_only)
{
    static_assert(fixture::System::Catalogs::template Of<solar::component::Tag>::template contains<
                  solar::health::Facility>);
    static_assert(fixture::System::Catalogs::template Of<solar::component::Tag>::template contains<
                  typename fixture::System::SupervisorService>);
    static_assert(
        solar::list_size_v<
            typename fixture::System::SupervisorArchitecture::ResponsePolicy::RuleTypes> == 0);

    auto boot = fixture::System::boot();
    zassert_true(boot.has_value());
    for (std::size_t attempt = 0; attempt < 100; ++attempt) {
        auto state = solar::supervisor::state();
        if (state && state->cycles > 0) {
            break;
        }
        k_sleep(K_MSEC(1));
    }
    auto state = solar::supervisor::state();
    auto watchdog = solar::supervisor::watchdog();
    zassert_true(state.has_value());
    zassert_true(state->cycles > 0);
    zassert_true(watchdog.has_value());
    zassert_false(watchdog->configured);
    zassert_true(fixture::System::stop().has_value());
}

ZTEST_SUITE(solar_supervisor_availability, nullptr, nullptr, nullptr, nullptr, nullptr);
