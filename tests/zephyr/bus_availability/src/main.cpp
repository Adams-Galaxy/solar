#include <atomic>

#include <zephyr/ztest.h>

#include <solar/solar.hpp>

namespace fixture
{

struct Application;

struct Message
{
    int value{};
    static constexpr solar::bus::Descriptor descriptor{.name = "availability.message"};
};

inline std::atomic_uint handled{};

struct Component
{
    static constexpr solar::component::Descriptor descriptor{.name = "component"};
    using Messages = solar::bus::Messages<Message>;
    using Subscriptions =
        solar::bus::Subscriptions<solar::bus::On<Message, solar::bus::delivery::Inline>>;

    static void handle(const Message&)
    {
        handled.fetch_add(1, std::memory_order_release);
    }
};

using System = solar::System<solar::Blueprint<solar::Facilities<Component>>>;

} // namespace fixture

SOLAR_BIND_SYSTEM_FOR(fixture::Application, fixture::System);

ZTEST(solar_bus_availability, test_emission_is_limited_to_system_running)
{
    using Bus = solar::bus::Of<fixture::Application>;

    auto before_boot = Bus::emit(fixture::Message{.value = 1});
    zassert_false(before_boot.has_value());
    zassert_equal(before_boot.error().reason, solar::bus::Reason::NotReady);

    auto boot = fixture::System::boot<fixture::Application>();
    zassert_true(boot.has_value());
    zassert_true(Bus::emit(fixture::Message{.value = 2}).has_value());
    zassert_equal(fixture::handled.load(std::memory_order_acquire), 1);

    auto stopped = fixture::System::stop();
    zassert_true(stopped.has_value());
    auto after_stop = Bus::emit(fixture::Message{.value = 3});
    zassert_false(after_stop.has_value());
    zassert_equal(after_stop.error().reason, solar::bus::Reason::NotReady);
}

ZTEST_SUITE(solar_bus_availability, nullptr, nullptr, nullptr, nullptr, nullptr);
