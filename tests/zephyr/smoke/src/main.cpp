#include <expected>

#include <zephyr/ztest.h>

#include <solar/solar.hpp>

static_assert(__cplusplus >= 202100L);
static_assert(solar::version == solar::Version{0, 1, 0});

ZTEST(solar_foundation, test_cpp23_expected_and_module_headers)
{
    std::expected<int, int> value{42};
    zassert_true(value.has_value());
    zassert_equal(value.value(), 42);
}

ZTEST_SUITE(solar_foundation, nullptr, nullptr, nullptr, nullptr, nullptr);
