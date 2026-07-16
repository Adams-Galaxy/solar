#include <type_traits>

#include <zephyr/ztest.h>

#include <solar/solar.hpp>

namespace fixture
{

struct Configured
{
    using Payload = std::uint32_t;
    static constexpr solar::events::Descriptor descriptor{.name = "policy.configured"};
};

struct Explicit
{
    using Payload = std::uint32_t;
    static constexpr solar::events::Descriptor descriptor{.name = "policy.explicit"};
    using Capture = solar::events::capture::EveryOccurrence;
    using Retention = solar::events::retention::Buffered;
};

struct PendingAggregate
{
    using Payload = std::uint32_t;
    static constexpr solar::events::Descriptor descriptor{.name = "policy.pending-aggregate"};
    using Capture =
        solar::events::capture::AggregateCount<solar::events::interval::Milliseconds<100>>;
    using Retention = solar::events::retention::Buffered;
};

using System = solar::System<
    solar::Blueprint<solar::Events<Configured, Explicit, PendingAggregate>,
                     solar::events::Configuration<
                         solar::events::DefaultCapture<solar::events::capture::SampleEvery<3>>,
                         solar::events::DefaultRetention<solar::events::retention::Transient>>>>;

using ConfiguredPolicies = System::EventFacility::Policies<Configured>;
using ExplicitPolicies = System::EventFacility::Policies<Explicit>;

static_assert(std::is_same_v<ConfiguredPolicies::Capture, solar::events::capture::SampleEvery<3>>);
static_assert(std::is_same_v<ConfiguredPolicies::Retention, solar::events::retention::Transient>);
static_assert(std::is_same_v<ExplicitPolicies::Capture, solar::events::capture::EveryOccurrence>);
static_assert(std::is_same_v<ExplicitPolicies::Retention, solar::events::retention::Buffered>);

#if defined(CONFIG_SOLAR_EVENTS_DEFAULT_STOP_CANCEL)
static_assert(
    std::is_same_v<System::EventFacility::ProcessorStopPolicy, solar::events::stop::CancelPending>);
#else
static_assert(
    std::is_same_v<System::EventFacility::ProcessorStopPolicy, solar::events::stop::Drain>);
#endif

} // namespace fixture

SOLAR_BIND_SYSTEM(fixture::System);

ZTEST(solar_events_policy, test_typed_precedence_and_kconfig_stop_default)
{
    auto booted = fixture::System::boot();
    zassert_true(booted.has_value());

    auto first = solar::events::observe<fixture::Configured>(1);
    auto second = solar::events::observe<fixture::Configured>(2);
    auto explicit_event = solar::events::observe<fixture::Explicit>(3);
    zassert_equal(first->disposition, solar::events::CaptureDisposition::Captured);
    zassert_equal(second->disposition, solar::events::CaptureDisposition::SampledOut);
    zassert_true(explicit_event->materialized);

    k_sleep(K_MSEC(10));
    zassert_false(solar::events::history::latest<fixture::Configured>().has_value());
    zassert_true(solar::events::history::latest<fixture::Explicit>().has_value());

    auto pending = solar::events::observe<fixture::PendingAggregate>(4);
    zassert_false(pending->materialized);

    auto stopped = fixture::System::stop();
    zassert_true(stopped.has_value());

#if defined(CONFIG_SOLAR_EVENTS_DEFAULT_STOP_CANCEL)
    zassert_false(solar::events::history::latest<fixture::PendingAggregate>().has_value());
    auto record = solar::events::detail::event_record<fixture::System, fixture::PendingAggregate>();
    zassert_equal(record.known_lost, 1);
#else
    auto aggregate = solar::events::history::latest<fixture::PendingAggregate>();
    zassert_true(aggregate.has_value());
    zassert_equal(aggregate->header.occurrence_count, 1);
#endif
}

ZTEST_SUITE(solar_events_policy, nullptr, nullptr, nullptr, nullptr, nullptr);
