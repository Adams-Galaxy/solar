#include <zephyr/ztest.h>

#include <solar/log.hpp>

struct DisabledSource
{
    static constexpr solar::log::SourceDescriptor descriptor{.name = "disabled-source"};
};

static_assert(!solar::log::available);
static_assert(!solar::log::enabled<DisabledSource, solar::log::Level::Info>);

ZTEST(solar_logging_disabled, test_explicit_use_reports_disabled_without_binding)
{
    auto result = solar::log::info<DisabledSource>("disabled {}", 1U);
    zassert_false(result.has_value());
    zassert_equal(result.error().reason, solar::log::Reason::Disabled);
}

ZTEST_SUITE(solar_logging_disabled, nullptr, nullptr, nullptr, nullptr, nullptr);
