#include <zephyr/ztest.h>

#include <solar/solar.hpp>

namespace fixture
{
struct App
{
    static constexpr solar::component::Descriptor descriptor{.name = "app"};
};
using System = solar::System<solar::Blueprint<solar::Facilities<App>>>;
} // namespace fixture

SOLAR_BIND_SYSTEM(fixture::System);

static_assert(fixture::System::InspectionCatalog::size == 0);
static_assert(!solar::contains_v<solar::inspection::Facility, fixture::System::Builtins>);

ZTEST(inspection_disabled, test_direct_system_remains_available)
{
    constexpr auto components = solar::graph::components();
    zassert_equal(components.size(), 1);
}

ZTEST_SUITE(inspection_disabled, nullptr, nullptr, nullptr, nullptr, nullptr);
