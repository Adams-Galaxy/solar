#include <zephyr/ztest.h>

#include <solar/events.hpp>
#include <solar/solar.hpp>

namespace fixture
{

struct Event
{
    using Payload = std::uint32_t;
    static constexpr solar::events::Descriptor descriptor{.name = "disabled.event"};
};

using System = solar::System<solar::Blueprint<>>;

} // namespace fixture

SOLAR_BIND_SYSTEM(fixture::System);

ZTEST(solar_events_disabled, test_disabled_frontend_is_explicit)
{
    auto before_boot = solar::events::observe<fixture::Event>(1);
    zassert_false(before_boot.has_value());
    zassert_equal(before_boot.error().reason, solar::events::Reason::NotReady);

    auto booted = fixture::System::boot();
    zassert_true(booted.has_value());

    auto observed = solar::events::observe<fixture::Event>(2);
    zassert_false(observed.has_value());
    zassert_equal(observed.error().reason, solar::events::Reason::Disabled);
    zassert_equal(solar::events::descriptors().size(), 0);
    zassert_equal(solar::events::facility_record().last_status, solar::Status::NotSupported);

    auto stopped = fixture::System::stop();
    zassert_true(stopped.has_value());
}

ZTEST_SUITE(solar_events_disabled, nullptr, nullptr, nullptr, nullptr, nullptr);
