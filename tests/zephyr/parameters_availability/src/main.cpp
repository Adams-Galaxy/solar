#include <zephyr/ztest.h>

#include <solar/solar.hpp>

namespace fixture
{

struct Gain
{
    using Value = int;
    static constexpr solar::parameters::Descriptor descriptor{.name = "availability.gain"};
    static constexpr Value default_value = 4;
};

struct Unregistered
{
    using Value = int;
    static constexpr solar::parameters::Descriptor descriptor{.name = "availability.missing"};
    static constexpr Value default_value = 9;
};

using System = solar::System<solar::Blueprint<solar::Parameters<Gain>>>;

} // namespace fixture

SOLAR_BIND_SYSTEM(fixture::System);

ZTEST(solar_parameters_availability, test_calls_follow_system_availability)
{
    auto before_boot = solar::parameters::get<fixture::Gain>();
    zassert_false(before_boot.has_value());
    zassert_equal(before_boot.error().reason, solar::parameters::Reason::NotReady);

    auto boot = fixture::System::boot();
    zassert_true(boot.has_value());

    auto initial = solar::parameters::get<fixture::Gain>();
    zassert_true(initial.has_value());
    zassert_equal(*initial, fixture::Gain::default_value);

    auto updated = solar::parameters::set<fixture::Gain>(12);
    zassert_true(updated.has_value());
    zassert_equal(updated->effective_value, 12);

#if !defined(CONFIG_SOLAR_STRICT_CATALOG_BINDING)
    auto missing = solar::parameters::get<fixture::Unregistered>();
    zassert_false(missing.has_value());
    zassert_equal(missing.error().reason, solar::parameters::Reason::NotRegistered);
#endif

    auto stopped = fixture::System::stop();
    zassert_true(stopped.has_value());

    auto after_stop = solar::parameters::get<fixture::Gain>();
    zassert_false(after_stop.has_value());
    zassert_equal(after_stop.error().reason, solar::parameters::Reason::NotReady);

    auto mutation_after_stop = solar::parameters::set<fixture::Gain>(15);
    zassert_false(mutation_after_stop.has_value());
    zassert_equal(mutation_after_stop.error().reason, solar::parameters::Reason::NotReady);
}

ZTEST_SUITE(solar_parameters_availability, nullptr, nullptr, nullptr, nullptr, nullptr);
