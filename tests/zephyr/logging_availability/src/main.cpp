#include <zephyr/ztest.h>

#include <solar/solar.hpp>

using AvailabilitySystem = solar::System<solar::Blueprint<solar::log::Configuration<
    solar::log::CompileLevel<solar::log::Level::Notice>>>>;
SOLAR_BIND_SYSTEM(AvailabilitySystem);

static_assert(solar::log::available);
static_assert(solar::frontend::strict !=
              solar::log::enabled<AvailabilitySystem::LogFacility, solar::log::Level::Info>);
static_assert(!solar::log::enabled<AvailabilitySystem::LogFacility, solar::log::Level::Trace>);
static_assert(AvailabilitySystem::LogSourceCatalog::contains<AvailabilitySystem::LogFacility>);
static_assert(solar::contains_v<AvailabilitySystem::LogFacility, AvailabilitySystem::Builtins>);

static void* setup_suite()
{
    auto boot = AvailabilitySystem::boot();
    zassert_true(boot.has_value());
    return nullptr;
}

ZTEST(solar_logging_availability, test_enabled_but_unused_facility_is_available)
{
    const auto record = solar::log::record();
    zassert_true(record.ready);
    zassert_equal(record.ingress_capacity, CONFIG_SOLAR_LOG_INGRESS_BYTES);

    const auto trace = solar::log::trace<AvailabilitySystem::LogFacility>("filtered trace");
    zassert_true(trace.has_value());
    zassert_equal(trace->disposition, solar::log::Disposition::CompileTimeFiltered);

    const auto info = solar::log::info<AvailabilitySystem::LogFacility>("policy filtered info");
    zassert_true(info.has_value());
    zassert_equal(info->disposition,
                  solar::frontend::strict ? solar::log::Disposition::CompileTimeFiltered
                                          : solar::log::Disposition::RuntimeFiltered);
}

ZTEST_SUITE(solar_logging_availability, nullptr, setup_suite, nullptr, nullptr, nullptr);
