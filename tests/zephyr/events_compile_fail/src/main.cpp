#include <array>
#include <span>
#include <string>

#include <solar/solar.hpp>

namespace
{

struct Store
{
    static solar::Result<void> initialize()
    {
        return {};
    }
    static solar::Result<void> write(solar::events::RecordView)
    {
        return {};
    }
};

struct First
{
    using Payload = std::uint32_t;
    static constexpr solar::events::Descriptor descriptor{.name = "failure.first"};
};

struct Second
{
    using Payload = std::uint32_t;
    static constexpr solar::events::Descriptor descriptor{.name = "failure.second"};
};

#if SOLAR_FAIL_CASE == 1
struct Invalid
{
    using Payload = std::string;
    static constexpr solar::events::Descriptor descriptor{.name = "failure.payload"};
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Events<Invalid>>>;

#elif SOLAR_FAIL_CASE == 2
struct Invalid
{
    using Payload = std::array<std::byte, 17>;
    static constexpr solar::events::Descriptor descriptor{.name = "failure.payload-size"};
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Events<Invalid>>>;

#elif SOLAR_FAIL_CASE == 3
struct alignas(16) OverAligned
{
    std::uint32_t value{};
};
struct Invalid
{
    using Payload = OverAligned;
    static constexpr solar::events::Descriptor descriptor{.name = "failure.payload-alignment"};
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Events<Invalid>>>;

#elif SOLAR_FAIL_CASE == 4
struct BadKey
{
    using Value = std::uint8_t;
};
struct Invalid : First
{
    using Capture =
        solar::events::capture::AggregateCount<solar::events::interval::Milliseconds<10>, BadKey,
                                               2>;
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Events<Invalid>>>;

#elif SOLAR_FAIL_CASE == 5
struct Invalid : First
{
    using Capture =
        solar::events::capture::AggregateCount<solar::events::interval::Milliseconds<10>, void, 5>;
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Events<Invalid>>>;

#elif SOLAR_FAIL_CASE == 6
struct Invalid : First
{
    static constexpr solar::events::Descriptor descriptor{
        .name = "failure.persistence-disabled",
        .stable_id = solar::events::Id{0x6001},
    };
    using Retention = solar::events::retention::Persistent<Store>;
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Events<Invalid>>>;

#elif SOLAR_FAIL_CASE == 7
struct Invalid : First
{
    using Retention = solar::events::retention::Persistent<Store>;
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Events<Invalid>>>;

#elif SOLAR_FAIL_CASE == 8
struct BadStore
{};
struct Invalid : First
{
    static constexpr solar::events::Descriptor descriptor{
        .name = "failure.persistence-store",
        .stable_id = solar::events::Id{0x6002},
    };
    using Retention = solar::events::retention::Persistent<BadStore>;
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Events<Invalid>>>;

#elif SOLAR_FAIL_CASE == 9
struct Invalid : First
{
    using Retention = solar::events::retention::Critical<5>;
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Events<Invalid>>>;

#elif SOLAR_FAIL_CASE == 10
struct OtherObserver
{};
struct Component
{
    static constexpr solar::component::Descriptor descriptor{.name = "failure.owner"};
    using Events = solar::events::Events<First>;
    using EventProcessors = solar::events::Processors<solar::events::Process<First, OtherObserver>>;
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Facilities<Component>>>;

#elif SOLAR_FAIL_CASE == 11
struct Component
{
    static constexpr solar::component::Descriptor descriptor{.name = "failure.handler"};
    using Events = solar::events::Events<First>;
    using EventRole = solar::events::InfrastructureObserver;
    using EventProcessors = solar::events::Processors<solar::events::Process<First, Component>>;
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Facilities<Component>>>;

#elif SOLAR_FAIL_CASE == 12
struct Component
{
    static constexpr solar::component::Descriptor descriptor{.name = "failure.registration"};
    using Events = solar::events::Events<First>;
    using EventRole = solar::events::InfrastructureObserver;
    using EventProcessors = solar::events::Processors<solar::events::Process<Second, Component>>;
    static void process(solar::events::RecordView) {}
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Facilities<Component>>>;

#elif SOLAR_FAIL_CASE == 13
struct UnregisteredSource
{};
using InvalidSystem = solar::System<solar::Blueprint<solar::Events<First>>>;

#elif SOLAR_FAIL_CASE == 14
struct Invalid : First
{
    using Capture = solar::events::capture::RateLimited<solar::events::interval::Milliseconds<10>>;
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Events<Invalid>>>;

#elif SOLAR_FAIL_CASE == 15
struct Missing;
struct Invalid : First
{
    using Resolves = Missing;
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Events<Invalid>>>;

#elif SOLAR_FAIL_CASE == 16
using InvalidSystem = solar::System<solar::Blueprint<solar::Events<First>>>;

#elif SOLAR_FAIL_CASE == 17
using InvalidSystem = solar::System<solar::Blueprint<solar::Events<First, Second>>>;

#elif SOLAR_FAIL_CASE == 18
struct Invalid
{
    using Payload = const std::byte*;
    static constexpr solar::events::Descriptor descriptor{.name = "failure.borrowed"};
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Events<Invalid>>>;

#elif SOLAR_FAIL_CASE == 19
using InvalidSystem = solar::System<solar::Blueprint<solar::Events<First>>>;

#else
#error SOLAR_DIAGNOSTIC_UNKNOWN_EVENTS_FAILURE_CASE
#endif

} // namespace

#if SOLAR_FAIL_CASE == 13 || SOLAR_FAIL_CASE == 14 || SOLAR_FAIL_CASE == 16
SOLAR_BIND_SYSTEM(InvalidSystem);
#endif

int main()
{
#if SOLAR_FAIL_CASE == 13
    (void)solar::events::observe_from<UnregisteredSource, First>(1);
#elif SOLAR_FAIL_CASE == 14
    (void)solar::events::try_observe_isr<Invalid>(1);
#elif SOLAR_FAIL_CASE == 16
    (void)solar::events::observe<Second>(1);
#else
    static_assert(InvalidSystem::valid);
#endif
    return 0;
}
