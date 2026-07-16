#include <zephyr/ztest.h>

#include <solar/bus.hpp>
#include <solar/solar.hpp>

namespace fixture
{

struct Message
{
    int value{};
    static constexpr solar::bus::Descriptor descriptor{.name = "disabled.message"};
};

using System = solar::System<solar::Blueprint<>>;

} // namespace fixture

SOLAR_BIND_SYSTEM(fixture::System);

ZTEST(solar_bus_disabled, test_disabled_frontend_is_explicit)
{
    auto boot = fixture::System::boot();
    zassert_true(boot.has_value());

    auto emitted = solar::bus::emit<fixture::Message>({.value = 1});
    zassert_false(emitted.has_value());
    zassert_equal(emitted.error().reason, solar::bus::Reason::Disabled);

    auto stopped = fixture::System::stop();
    zassert_true(stopped.has_value());
}

ZTEST_SUITE(solar_bus_disabled, nullptr, nullptr, nullptr, nullptr, nullptr);
