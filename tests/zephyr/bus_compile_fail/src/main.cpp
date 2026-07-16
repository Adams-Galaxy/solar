#include <string>

#include <solar/solar.hpp>

namespace
{

struct Message
{
    int value{};
    static constexpr solar::bus::Descriptor descriptor{.name = "failure.message"};
};

struct OtherMessage
{
    int value{};
    static constexpr solar::bus::Descriptor descriptor{.name = "failure.other"};
};

struct ThirdMessage
{
    int value{};
    static constexpr solar::bus::Descriptor descriptor{.name = "failure.third"};
};

struct Producer
{
    static constexpr solar::component::Descriptor descriptor{.name = "producer"};
    using Messages = solar::bus::Messages<Message>;
};

struct GoodSubscriber
{
    static constexpr solar::component::Descriptor descriptor{.name = "subscriber"};
    using Subscriptions =
        solar::bus::Subscriptions<solar::bus::On<Message, solar::bus::delivery::Inline>>;

    static void handle(const Message&) {}
};

#if SOLAR_FAIL_CASE == 1
struct Subscriber
{
    static constexpr solar::component::Descriptor descriptor{.name = "subscriber"};
    using Subscriptions =
        solar::bus::Subscriptions<solar::bus::On<Message, solar::bus::delivery::Inline>>;
    static void handle(const Message&) {}
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Facilities<Subscriber>>>;
#elif SOLAR_FAIL_CASE == 2
struct Subscriber
{
    static constexpr solar::component::Descriptor descriptor{.name = "subscriber"};
    using Subscriptions =
        solar::bus::Subscriptions<solar::bus::On<Message, solar::bus::delivery::Inline>,
                                  solar::bus::On<Message, solar::bus::delivery::InlineIsr>>;
    static void handle(const Message&) {}
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Facilities<Producer, Subscriber>>>;
#elif SOLAR_FAIL_CASE == 3
struct Subscriber
{
    static constexpr solar::component::Descriptor descriptor{.name = "subscriber"};
    using Subscriptions =
        solar::bus::Subscriptions<solar::bus::On<Message, solar::bus::delivery::Inline>>;
    static bool handle(const Message&)
    {
        return true;
    }
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Facilities<Producer, Subscriber>>>;
#elif SOLAR_FAIL_CASE == 4
struct NonTrivial
{
    std::string value;
    static constexpr solar::bus::Descriptor descriptor{.name = "failure.nontrivial"};
};
struct NonTrivialProducer
{
    static constexpr solar::component::Descriptor descriptor{.name = "producer"};
    using Messages = solar::bus::Messages<NonTrivial>;
};
struct Subscriber
{
    static constexpr solar::component::Descriptor descriptor{.name = "subscriber"};
    using Subscriptions = solar::bus::Subscriptions<solar::bus::On<
        NonTrivial, solar::bus::delivery::Queued<solar::execution::SystemWorkQueue, 1>>>;
    static void handle(const NonTrivial&) {}
};
using InvalidSystem =
    solar::System<solar::Blueprint<solar::Facilities<NonTrivialProducer, Subscriber>>>;
#elif SOLAR_FAIL_CASE == 5
struct Large
{
    char bytes[16]{};
    static constexpr solar::bus::Descriptor descriptor{.name = "failure.large"};
};
struct LargeProducer
{
    static constexpr solar::component::Descriptor descriptor{.name = "producer"};
    using Messages = solar::bus::Messages<Large>;
};
struct Subscriber
{
    static constexpr solar::component::Descriptor descriptor{.name = "subscriber"};
    using Subscriptions = solar::bus::Subscriptions<
        solar::bus::On<Large, solar::bus::delivery::Queued<solar::execution::SystemWorkQueue, 1>>>;
    static void handle(const Large&) {}
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Facilities<LargeProducer, Subscriber>>>;
#elif SOLAR_FAIL_CASE == 6
struct Subscriber
{
    static constexpr solar::component::Descriptor descriptor{.name = "subscriber"};
    using Subscriptions = solar::bus::Subscriptions<solar::bus::On<
        Message, solar::bus::delivery::Queued<solar::execution::SystemWorkQueue, 0>>>;
    static void handle(const Message&) {}
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Facilities<Producer, Subscriber>>>;
#elif SOLAR_FAIL_CASE == 7
struct Subscriber
{
    static constexpr solar::component::Descriptor descriptor{.name = "subscriber"};
    using Subscriptions = solar::bus::Subscriptions<solar::bus::On<
        Message, solar::bus::delivery::Queued<solar::execution::SystemWorkQueue, 3>>>;
    static void handle(const Message&) {}
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Facilities<Producer, Subscriber>>>;
#elif SOLAR_FAIL_CASE == 8
struct Subscriber
{
    static constexpr solar::component::Descriptor descriptor{.name = "subscriber"};
    using Subscriptions = solar::bus::Subscriptions<solar::bus::On<
        Message, solar::bus::delivery::Coalesced<solar::execution::SystemWorkQueue>>>;
    static void handle(const Message&) {}
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Facilities<Producer, Subscriber>>>;
#elif SOLAR_FAIL_CASE == 9
using MissingQueue = solar::execution::WorkQueue<"missing", solar::execution::StackSize<1024>>;
struct Subscriber
{
    static constexpr solar::component::Descriptor descriptor{.name = "subscriber"};
    using Subscriptions = solar::bus::Subscriptions<
        solar::bus::On<Message, solar::bus::delivery::Queued<MissingQueue, 1>>>;
    static void handle(const Message&) {}
};
using InvalidSystem = solar::System<solar::Blueprint<solar::Facilities<Producer, Subscriber>>>;
#elif SOLAR_FAIL_CASE == 10
struct NotExecutor
{
    static constexpr solar::component::Descriptor descriptor{.name = "not-executor"};
};
struct Subscriber
{
    static constexpr solar::component::Descriptor descriptor{.name = "subscriber"};
    using Subscriptions = solar::bus::Subscriptions<
        solar::bus::On<Message, solar::bus::delivery::Queued<NotExecutor, 1>>>;
    static void handle(const Message&) {}
};
using InvalidSystem =
    solar::System<solar::Blueprint<solar::Facilities<Producer, Subscriber, NotExecutor>>>;
#elif SOLAR_FAIL_CASE == 11
using InvalidSystem = solar::System<
    solar::Blueprint<solar::Bus<solar::bus::Messages<Message>>,
                     solar::bus::Configuration<solar::bus::RequireSubscriber<Message>>>>;
#elif SOLAR_FAIL_CASE == 12
using InvalidSystem = solar::System<solar::Blueprint<solar::Facilities<Producer, GoodSubscriber>>>;
#elif SOLAR_FAIL_CASE == 13
using InvalidSystem = solar::System<
    solar::Blueprint<solar::Bus<solar::bus::Messages<Message, OtherMessage, ThirdMessage>>>>;
#elif SOLAR_FAIL_CASE == 14
using InvalidSystem = solar::System<solar::Blueprint<solar::Bus<solar::bus::Messages<Message>>>>;
#elif SOLAR_FAIL_CASE == 15
struct Aligned
{
    alignas(16) int value{};
    static constexpr solar::bus::Descriptor descriptor{.name = "failure.aligned"};
};
struct AlignedProducer
{
    static constexpr solar::component::Descriptor descriptor{.name = "producer"};
    using Messages = solar::bus::Messages<Aligned>;
};
struct Subscriber
{
    static constexpr solar::component::Descriptor descriptor{.name = "subscriber"};
    using Subscriptions = solar::bus::Subscriptions<solar::bus::On<
        Aligned, solar::bus::delivery::Queued<solar::execution::SystemWorkQueue, 1>>>;
    static void handle(const Aligned&) {}
};
using InvalidSystem =
    solar::System<solar::Blueprint<solar::Facilities<AlignedProducer, Subscriber>>>;
#else
#error SOLAR_DIAGNOSTIC_UNKNOWN_BUS_FAILURE_CASE
#endif

} // namespace

#if SOLAR_FAIL_CASE == 12 || SOLAR_FAIL_CASE == 14
SOLAR_BIND_SYSTEM(InvalidSystem);
#endif

int main()
{
#if SOLAR_FAIL_CASE == 12
    (void)solar::bus::try_emit_isr<Message>({.value = 1});
#elif SOLAR_FAIL_CASE == 14
    (void)solar::bus::emit<OtherMessage>({.value = 1});
#else
    static_assert(InvalidSystem::valid);
#endif
    return 0;
}
