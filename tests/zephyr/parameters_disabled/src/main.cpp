#include <zephyr/ztest.h>

#include <solar/parameters.hpp>
#include <solar/solar.hpp>

namespace fixture
{

struct Parameter
{
    using Value = int;
    static constexpr solar::parameters::Descriptor descriptor{.name = "disabled.parameter"};
    static constexpr Value default_value = 1;
};

using System = solar::System<solar::Blueprint<>>;

} // namespace fixture

SOLAR_BIND_SYSTEM(fixture::System);

ZTEST(solar_parameters_disabled, test_disabled_frontend_is_explicit)
{
    auto before_boot = solar::parameters::get<fixture::Parameter>();
    zassert_false(before_boot.has_value());
    zassert_equal(before_boot.error().reason, solar::parameters::Reason::NotReady);

    auto boot = fixture::System::boot();
    zassert_true(boot.has_value());

    auto value = solar::parameters::get<fixture::Parameter>();
    zassert_false(value.has_value());
    zassert_equal(value.error().reason, solar::parameters::Reason::Disabled);

    auto stopped = fixture::System::stop();
    zassert_true(stopped.has_value());
}

ZTEST_SUITE(solar_parameters_disabled, nullptr, nullptr, nullptr, nullptr, nullptr);
