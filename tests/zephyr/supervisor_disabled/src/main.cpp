#include <array>

#include <zephyr/ztest.h>

#include <solar/supervisor.hpp>

ZTEST(solar_supervisor_disabled, test_disabled_frontend_is_storage_free_and_explicit)
{
    static_assert(!solar::supervisor::available);
    static_assert(!solar::supervisor::enabled);

    auto state = solar::supervisor::state();
    auto watchdog = solar::supervisor::watchdog();
    std::array<solar::supervisor::ResponseRecord, 1> records{};
    auto responses = solar::supervisor::responses({}, records);
    zassert_false(state.has_value());
    zassert_false(watchdog.has_value());
    zassert_false(responses.has_value());
    zassert_equal(state.error().reason, solar::supervisor::Reason::Disabled);
}

ZTEST_SUITE(solar_supervisor_disabled, nullptr, nullptr, nullptr, nullptr, nullptr);
