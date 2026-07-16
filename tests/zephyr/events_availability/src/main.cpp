#include <zephyr/ztest.h>

#include <solar/solar.hpp>

namespace fixture
{

struct Application;

struct Ready
{
    using Payload = std::uint32_t;
    static constexpr solar::events::Descriptor descriptor{
        .name = "availability.ready",
        .domain = solar::events::domain::Lifecycle,
    };
};

struct Missing
{
    using Payload = void;
    static constexpr solar::events::Descriptor descriptor{.name = "availability.missing"};
};

using System = solar::System<solar::Blueprint<solar::Events<Ready>>>;
using EmptySystem = solar::System<solar::Blueprint<>>;

static_assert(!solar::contains_v<EmptySystem::EventFacility, typename EmptySystem::Builtins>);
static_assert(!EmptySystem::ExecutionCatalog::template contains<
              typename EmptySystem::EventFacility::ProcessorRegistration>);

} // namespace fixture

SOLAR_BIND_SYSTEM_FOR(fixture::Application, fixture::System);

ZTEST(solar_events_availability, test_frontend_follows_system_availability)
{
    using Events = solar::events::Of<fixture::Application>;
#if !defined(CONFIG_SOLAR_STRICT_CATALOG_BINDING)
    auto before_boot = Events::observe<fixture::Ready>(1);
    zassert_false(before_boot.has_value());
    zassert_equal(before_boot.error().reason, solar::events::Reason::NotReady);
#endif

    auto booted = fixture::System::boot<fixture::Application>();
    zassert_true(booted.has_value());

    auto observed = Events::observe<fixture::Ready>(2);
    zassert_true(observed.has_value());
    zassert_equal(Events::descriptors().size(), 1);
    k_sleep(K_MSEC(5));
    auto latest = Events::latest<fixture::Ready>();
    zassert_true(latest.has_value());
    auto decoded = Events::decode<fixture::Ready>(latest->view());
    zassert_true(decoded.has_value());
    zassert_equal(*decoded, 2);

#if !defined(CONFIG_SOLAR_STRICT_CATALOG_BINDING)
    auto missing = Events::observe<fixture::Missing>();
    zassert_false(missing.has_value());
    zassert_equal(missing.error().reason, solar::events::Reason::NotRegistered);
#endif

    auto stopped = fixture::System::stop();
    zassert_true(stopped.has_value());

#if !defined(CONFIG_SOLAR_STRICT_CATALOG_BINDING)
    auto after_stop = Events::observe<fixture::Ready>(3);
    zassert_false(after_stop.has_value());
    zassert_equal(after_stop.error().reason, solar::events::Reason::NotReady);
#endif
}

ZTEST_SUITE(solar_events_availability, nullptr, nullptr, nullptr, nullptr, nullptr);
