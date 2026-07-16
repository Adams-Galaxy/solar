#include <array>
#include <atomic>

#include <zephyr/irq_offload.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <solar/solar.hpp>

using namespace std::chrono_literals;
using namespace solar::literals;

namespace fixture
{

#define BUS_MESSAGE(TYPE, NAME)                                                                    \
    struct TYPE                                                                                    \
    {                                                                                              \
        int value{};                                                                               \
        static constexpr solar::bus::Descriptor descriptor{.name = NAME};                          \
    }

BUS_MESSAGE(Ping, "fixture.ping");
BUS_MESSAGE(LatestUpdate, "fixture.latest");
BUS_MESSAGE(RejectUpdate, "fixture.reject");
BUS_MESSAGE(DropNewestUpdate, "fixture.drop_newest");
BUS_MESSAGE(DropOldestUpdate, "fixture.drop_oldest");
BUS_MESSAGE(WaitUpdate, "fixture.wait");
BUS_MESSAGE(IsrUpdate, "fixture.isr");
BUS_MESSAGE(ReentrantUpdate, "fixture.reentrant");
BUS_MESSAGE(FaultUpdate, "fixture.fault");
BUS_MESSAGE(AsyncFaultUpdate, "fixture.async_fault");
BUS_MESSAGE(ConcurrentUpdate, "fixture.concurrent");
BUS_MESSAGE(RootUpdate, "fixture.root");
BUS_MESSAGE(UnhandledUpdate, "fixture.unhandled");

#undef BUS_MESSAGE

struct Pulse
{
    static constexpr solar::bus::Descriptor descriptor{.name = "fixture.pulse"};
};

struct UnknownUpdate
{
    int value{};
    static constexpr solar::bus::Descriptor descriptor{.name = "fixture.unknown"};
};

inline std::atomic_uint inline_runs{};
inline std::atomic_uint queued_runs{};
inline std::atomic_int inline_value{};
inline std::atomic_int queued_value{};
inline std::atomic_uint latest_runs{};
inline std::atomic_int latest_value{};
inline std::atomic_bool latest_handler_block{};
inline std::atomic_bool latest_handler_started{};
inline std::atomic_bool latest_handler_release{};
inline std::atomic_uint pulse_runs{};
inline std::atomic_bool pulse_handler_block{};
inline std::atomic_bool pulse_handler_started{};
inline std::atomic_bool pulse_handler_release{};
inline std::atomic_uint isr_inline_runs{};
inline std::atomic_uint isr_queued_runs{};
inline std::atomic_int isr_value{};
inline std::atomic_bool isr_emit_ok{};
inline std::atomic_uint root_runs{};
inline std::atomic_int root_value{};
inline std::atomic_uint fault_success_runs{};
inline std::atomic_uint async_fault_runs{};
inline std::atomic_uint blocker_runs{};
inline std::atomic_bool blocker_started{};
inline std::atomic_bool blocker_release{};
inline std::atomic_uint reject_runs{};
inline std::atomic_uint drop_newest_runs{};
inline std::atomic_uint drop_oldest_runs{};
inline std::atomic_uint wait_runs{};
inline std::atomic_uint concurrent_runs{};
inline std::atomic_uint concurrent_ready{};
inline std::atomic_bool concurrent_go{};
inline std::atomic_uint concurrent_failures{};
inline std::atomic_uint reentrant_index{};
inline std::array<int, 8> reentrant_order{};
inline std::array<int, 8> reject_values{};
inline std::array<int, 8> drop_newest_values{};
inline std::array<int, 8> drop_oldest_values{};
inline std::array<int, 8> wait_values{};

using ControlQueue = solar::execution::WorkQueue<"bus-control", solar::execution::StackSize<2048>,
                                                 solar::execution::Priority<2>,
                                                 solar::execution::StopTimeout<200_ms>>;

struct BlockingBehavior
{
    static void execute()
    {
        blocker_started.store(true, std::memory_order_release);
        while (!blocker_release.load(std::memory_order_acquire)) {
            k_sleep(K_MSEC(1));
        }
        blocker_runs.fetch_add(1, std::memory_order_release);
    }
};

using BlockerTask = solar::execution::OnDemand<"bus-blocker", BlockingBehavior, ControlQueue>;

struct Producer
{
    static constexpr solar::component::Descriptor descriptor{.name = "producer"};
    using Messages =
        solar::bus::Messages<Ping, LatestUpdate, Pulse, RejectUpdate, DropNewestUpdate,
                             DropOldestUpdate, WaitUpdate, IsrUpdate, ReentrantUpdate, FaultUpdate,
                             AsyncFaultUpdate, ConcurrentUpdate, UnhandledUpdate>;
    using Tasks = solar::execution::Tasks<BlockerTask>;
};

struct InlineSubscriber
{
    static constexpr solar::component::Descriptor descriptor{.name = "inline-subscriber"};
    using Subscriptions =
        solar::bus::Subscriptions<solar::bus::On<Ping, solar::bus::delivery::Inline>>;

    static void handle(const Ping& message)
    {
        inline_value.store(message.value, std::memory_order_release);
        inline_runs.fetch_add(1, std::memory_order_release);
    }
};

struct QueuedSubscriber
{
    static constexpr solar::component::Descriptor descriptor{.name = "queued-subscriber"};
    using Subscriptions = solar::bus::Subscriptions<
        solar::bus::On<Ping, solar::bus::delivery::Queued<solar::execution::SystemWorkQueue, 4>>>;

    static solar::Status handle(const Ping& message)
    {
        queued_value.store(message.value, std::memory_order_release);
        queued_runs.fetch_add(1, std::memory_order_release);
        return solar::Status::Ok;
    }
};

template <typename Message, typename Counter, typename Values>
void record_value(const Message& message, Counter& counter, Values& values)
{
    const auto index = counter.load(std::memory_order_relaxed);
    values[index] = message.value;
    counter.store(index + 1, std::memory_order_release);
}

struct DeferredSubscriber
{
    static constexpr solar::component::Descriptor descriptor{.name = "deferred-subscriber"};
    using Subscriptions = solar::bus::Subscriptions<
        solar::bus::On<LatestUpdate, solar::bus::delivery::Latest<ControlQueue>>,
        solar::bus::On<Pulse, solar::bus::delivery::Coalesced<ControlQueue>>,
        solar::bus::On<RejectUpdate,
                       solar::bus::delivery::Queued<ControlQueue, 2, solar::bus::overflow::Reject>>,
        solar::bus::On<DropNewestUpdate, solar::bus::delivery::Queued<
                                             ControlQueue, 2, solar::bus::overflow::DropNewest>>,
        solar::bus::On<DropOldestUpdate, solar::bus::delivery::Queued<
                                             ControlQueue, 2, solar::bus::overflow::DropOldest>>,
        solar::bus::On<WaitUpdate, solar::bus::delivery::Queued<
                                       ControlQueue, 1, solar::bus::overflow::WaitFor<5_ms>>>,
        solar::bus::On<ConcurrentUpdate, solar::bus::delivery::Queued<ControlQueue, 32>>,
        solar::bus::On<AsyncFaultUpdate, solar::bus::delivery::Queued<ControlQueue, 1>>>;

    static solar::Result<void> handle(const LatestUpdate& message)
    {
        latest_value.store(message.value, std::memory_order_release);
        latest_runs.fetch_add(1, std::memory_order_release);
        if (latest_handler_block.load(std::memory_order_acquire)) {
            latest_handler_started.store(true, std::memory_order_release);
            while (!latest_handler_release.load(std::memory_order_acquire)) {
                k_sleep(K_MSEC(1));
            }
        }
        return {};
    }

    static void handle(const Pulse&)
    {
        pulse_runs.fetch_add(1, std::memory_order_release);
        if (pulse_handler_block.load(std::memory_order_acquire)) {
            pulse_handler_started.store(true, std::memory_order_release);
            while (!pulse_handler_release.load(std::memory_order_acquire)) {
                k_sleep(K_MSEC(1));
            }
        }
    }

    static void handle(const RejectUpdate& message)
    {
        record_value(message, reject_runs, reject_values);
    }

    static void handle(const DropNewestUpdate& message)
    {
        record_value(message, drop_newest_runs, drop_newest_values);
    }

    static void handle(const DropOldestUpdate& message)
    {
        record_value(message, drop_oldest_runs, drop_oldest_values);
    }

    static void handle(const WaitUpdate& message)
    {
        record_value(message, wait_runs, wait_values);
    }

    static void handle(const ConcurrentUpdate&)
    {
        concurrent_runs.fetch_add(1, std::memory_order_release);
    }

    static solar::Status handle(const AsyncFaultUpdate&)
    {
        async_fault_runs.fetch_add(1, std::memory_order_release);
        return solar::Status::Error;
    }
};

struct IsrSubscriber
{
    static constexpr solar::component::Descriptor descriptor{.name = "isr-subscriber"};
    using Subscriptions = solar::bus::Subscriptions<
        solar::bus::Route<struct IsrInlineRoute, IsrUpdate, struct IsrInlineHandler,
                          solar::bus::delivery::InlineIsr>,
        solar::bus::Route<struct IsrQueuedRoute, IsrUpdate, struct IsrQueuedHandler,
                          solar::bus::delivery::Queued<solar::execution::SystemWorkQueue, 2>>>;
};

struct IsrInlineHandler
{
    static void handle(const IsrUpdate& message)
    {
        isr_value.store(message.value, std::memory_order_release);
        isr_inline_runs.fetch_add(1, std::memory_order_release);
    }
};

struct IsrQueuedHandler
{
    static void handle(const IsrUpdate&)
    {
        isr_queued_runs.fetch_add(1, std::memory_order_release);
    }
};

struct ReentrantFirst
{
    static constexpr solar::component::Descriptor descriptor{.name = "reentrant-first"};
    using Subscriptions =
        solar::bus::Subscriptions<solar::bus::On<ReentrantUpdate, solar::bus::delivery::Inline>>;

    static void handle(const ReentrantUpdate& message);
};

struct ReentrantSecond
{
    static constexpr solar::component::Descriptor descriptor{.name = "reentrant-second"};
    using Subscriptions =
        solar::bus::Subscriptions<solar::bus::On<ReentrantUpdate, solar::bus::delivery::Inline>>;

    static void handle(const ReentrantUpdate& message)
    {
        const auto index = reentrant_index.fetch_add(1, std::memory_order_acq_rel);
        reentrant_order[index] = message.value * 10 + 2;
    }
};

struct FailingSubscriber
{
    static constexpr solar::component::Descriptor descriptor{.name = "failing-subscriber"};
    using Subscriptions =
        solar::bus::Subscriptions<solar::bus::On<FaultUpdate, solar::bus::delivery::Inline>>;

    static solar::Status handle(const FaultUpdate&)
    {
        return solar::Status::Error;
    }
};

struct SuccessfulSubscriber
{
    static constexpr solar::component::Descriptor descriptor{.name = "successful-subscriber"};
    using Subscriptions =
        solar::bus::Subscriptions<solar::bus::On<FaultUpdate, solar::bus::delivery::Inline>>;

    static solar::Result<void> handle(const FaultUpdate&)
    {
        fault_success_runs.fetch_add(1, std::memory_order_release);
        return {};
    }
};

struct RootSubscriber
{
    static constexpr solar::component::Descriptor descriptor{.name = "root-subscriber"};

    static void handle(const RootUpdate& message)
    {
        root_value.store(message.value, std::memory_order_release);
        root_runs.fetch_add(1, std::memory_order_release);
    }
};

using Blueprint = solar::Blueprint<
    solar::Facilities<Producer, InlineSubscriber, QueuedSubscriber, DeferredSubscriber,
                      IsrSubscriber, ReentrantFirst, ReentrantSecond, FailingSubscriber,
                      SuccessfulSubscriber, RootSubscriber>,
    solar::Executors<ControlQueue>,
    solar::Bus<solar::bus::Messages<RootUpdate>,
               solar::bus::Subscriptions<
                   solar::bus::To<RootUpdate, RootSubscriber, solar::bus::delivery::Inline>>>>;
using System = solar::System<Blueprint>;

static_assert(System::BusMessageCatalog::size == 14);
static_assert(System::BusSubscriptionCatalog::size == 17);
static_assert(solar::contains_v<typename System::BusFacility, typename System::Builtins>);
static_assert(System::ExecutionCatalog::size == 11);
static_assert(System::BusFacility::asynchronous_route_count == 10);
static_assert(System::BusFacility::payload_storage_bytes == 188);
static_assert(System::BusFacility::route_state_bytes >= System::BusFacility::payload_storage_bytes);

struct CapacityMessage
{
    int value{};
    static constexpr solar::bus::Descriptor descriptor{.name = "fixture.capacity"};
};

struct CapacitySubscriber
{
    static constexpr solar::component::Descriptor descriptor{.name = "capacity-subscriber"};
    static void handle(const CapacityMessage&) {}
};

struct ExplicitCapacityRoute;
using KconfigCapacityRoute =
    solar::bus::To<CapacityMessage, CapacitySubscriber,
                   solar::bus::delivery::Queued<solar::execution::SystemWorkQueue>>;
using ConfiguredCapacityRoute =
    solar::bus::To<CapacityMessage, CapacitySubscriber,
                   solar::bus::delivery::Queued<solar::execution::SystemWorkQueue>>;
using ExplicitCapacityRouteType = solar::bus::To<
    CapacityMessage, CapacitySubscriber,
    solar::bus::delivery::Queued<solar::execution::SystemWorkQueue, 1, solar::bus::overflow::Reject,
                                 solar::bus::stop::Drain>,
    ExplicitCapacityRoute>;

using KconfigCapacitySystem =
    solar::System<solar::Blueprint<solar::Facilities<CapacitySubscriber>,
                                   solar::Bus<solar::bus::Messages<CapacityMessage>,
                                              solar::bus::Subscriptions<KconfigCapacityRoute>>>>;
using ConfiguredCapacitySystem = solar::System<solar::Blueprint<
    solar::Facilities<CapacitySubscriber>,
    solar::Bus<solar::bus::Messages<CapacityMessage>,
               solar::bus::Subscriptions<ConfiguredCapacityRoute, ExplicitCapacityRouteType>>,
    solar::bus::Configuration<solar::bus::DefaultCapacity<2>,
                              solar::bus::DefaultOverflow<solar::bus::overflow::DropNewest>,
                              solar::bus::DefaultStop<solar::bus::stop::CancelPending>>>>;

static_assert(
    KconfigCapacitySystem::BusFacility::template Delivery<KconfigCapacityRoute>::capacity ==
    CONFIG_SOLAR_BUS_DEFAULT_QUEUE_CAPACITY);
static_assert(
    ConfiguredCapacitySystem::BusFacility::template Delivery<ConfiguredCapacityRoute>::capacity ==
    2);
static_assert(
    ConfiguredCapacitySystem::BusFacility::template Delivery<ExplicitCapacityRouteType>::capacity ==
    1);
static_assert(std::same_as<typename KconfigCapacitySystem::BusFacility::template Delivery<
                               KconfigCapacityRoute>::Overflow,
                           solar::bus::overflow::Reject>);
static_assert(
    std::same_as<
        typename KconfigCapacitySystem::BusFacility::template Delivery<KconfigCapacityRoute>::Stop,
        solar::bus::stop::Drain>);
static_assert(std::same_as<typename ConfiguredCapacitySystem::BusFacility::template Delivery<
                               ConfiguredCapacityRoute>::Overflow,
                           solar::bus::overflow::DropNewest>);
static_assert(std::same_as<typename ConfiguredCapacitySystem::BusFacility::template Delivery<
                               ConfiguredCapacityRoute>::Stop,
                           solar::bus::stop::CancelPending>);
static_assert(std::same_as<typename ConfiguredCapacitySystem::BusFacility::template Delivery<
                               ExplicitCapacityRouteType>::Overflow,
                           solar::bus::overflow::Reject>);
static_assert(std::same_as<typename ConfiguredCapacitySystem::BusFacility::template Delivery<
                               ExplicitCapacityRouteType>::Stop,
                           solar::bus::stop::Drain>);

} // namespace fixture

SOLAR_BIND_SYSTEM(fixture::System);

void fixture::ReentrantFirst::handle(const ReentrantUpdate& message)
{
    const auto index = reentrant_index.fetch_add(1, std::memory_order_acq_rel);
    reentrant_order[index] = message.value * 10 + 1;
    if (message.value == 1) {
        (void)solar::bus::emit<ReentrantUpdate>({.value = 2});
    }
}

namespace
{

void wait_for(const std::atomic_uint& value, unsigned minimum)
{
    for (int attempt = 0; attempt < 100 && value.load(std::memory_order_acquire) < minimum;
         ++attempt) {
        k_sleep(K_MSEC(2));
    }
    zassert_true(value.load(std::memory_order_acquire) >= minimum);
}

void hold_control_executor()
{
    fixture::blocker_started.store(false, std::memory_order_release);
    fixture::blocker_release.store(false, std::memory_order_release);
    zassert_true(solar::execution::submit<fixture::BlockerTask>().has_value());
    for (int attempt = 0;
         attempt < 100 && !fixture::blocker_started.load(std::memory_order_acquire); ++attempt) {
        k_sleep(K_MSEC(1));
    }
    zassert_true(fixture::blocker_started.load(std::memory_order_acquire));
}

void release_control_executor()
{
    const auto before = fixture::blocker_runs.load(std::memory_order_acquire);
    fixture::blocker_release.store(true, std::memory_order_release);
    wait_for(fixture::blocker_runs, before + 1);
}

void emit_from_isr(const void*)
{
    auto result = solar::bus::try_emit_isr<fixture::IsrUpdate>({.value = 91});
    fixture::isr_emit_ok.store(result.has_value(), std::memory_order_release);
}

struct ProducerArguments
{
    int first{};
    int count{};
};

void concurrent_producer(void* context) noexcept
{
    const auto arguments = *static_cast<const ProducerArguments*>(context);
    fixture::concurrent_ready.fetch_add(1, std::memory_order_acq_rel);
    while (!fixture::concurrent_go.load(std::memory_order_acquire)) {
        k_sleep(K_MSEC(1));
    }
    for (int offset = 0; offset < arguments.count; ++offset) {
        if (!solar::bus::emit<fixture::ConcurrentUpdate>({.value = arguments.first + offset})) {
            fixture::concurrent_failures.fetch_add(1, std::memory_order_release);
        }
    }
}

void* setup()
{
    auto boot = fixture::System::boot();
    zassert_true(boot.has_value());
    return nullptr;
}

void teardown(void*)
{
    auto stopped = fixture::System::stop();
    zassert_true(stopped.has_value());
}

} // namespace

ZTEST_SUITE(solar_bus, nullptr, setup, nullptr, nullptr, teardown);

ZTEST(solar_bus, test_component_and_root_contributions_normalize_into_one_topology)
{
    const auto messages = solar::bus::messages();
    const auto subscriptions = solar::bus::subscriptions();

    zassert_equal(messages.size(), 14);
    zassert_equal(subscriptions.size(), 17);
    std::size_t direct_routes{};
    std::size_t contributed_routes{};
    bool found_ping_queue{};
    for (const auto& subscription : subscriptions) {
        direct_routes += subscription.origin == solar::OriginKind::Direct ? 1U : 0U;
        contributed_routes += subscription.origin == solar::OriginKind::Contribution ? 1U : 0U;
        found_ping_queue =
            found_ping_queue || (subscription.message_name == "fixture.ping" &&
                                 subscription.delivery == solar::bus::DeliveryKind::Queued &&
                                 subscription.capacity == 4);
    }
    zassert_equal(direct_routes, 1);
    zassert_equal(contributed_routes, 16);
    zassert_true(found_ping_queue);

    const auto root_before = fixture::root_runs.load(std::memory_order_acquire);
    zassert_true(solar::bus::emit<fixture::RootUpdate>({.value = 73}).has_value());
    zassert_equal(fixture::root_runs.load(std::memory_order_acquire), root_before + 1);
    zassert_equal(fixture::root_value.load(std::memory_order_acquire), 73);
}

ZTEST(solar_bus, test_emit_fans_out_inline_and_queued)
{
    const auto inline_before = fixture::inline_runs.load(std::memory_order_acquire);
    const auto queued_before = fixture::queued_runs.load(std::memory_order_acquire);

    auto emitted = solar::bus::emit(fixture::Ping{.value = 42});
    zassert_true(emitted.has_value());
    zassert_equal(fixture::inline_runs.load(std::memory_order_acquire), inline_before + 1);
    zassert_equal(fixture::inline_value.load(std::memory_order_acquire), 42);

    wait_for(fixture::queued_runs, queued_before + 1);
    zassert_equal(fixture::queued_value.load(std::memory_order_acquire), 42);
}

ZTEST(solar_bus, test_latest_and_coalesced_are_bounded)
{
    hold_control_executor();
    const auto latest_before = fixture::latest_runs.load(std::memory_order_acquire);
    const auto pulse_before = fixture::pulse_runs.load(std::memory_order_acquire);

    zassert_true(solar::bus::emit<fixture::LatestUpdate>({.value = 1}).has_value());
    zassert_true(solar::bus::emit<fixture::LatestUpdate>({.value = 2}).has_value());
    zassert_true(solar::bus::emit<fixture::LatestUpdate>({.value = 3}).has_value());
    zassert_true(solar::bus::emit<fixture::Pulse>().has_value());
    zassert_true(solar::bus::emit<fixture::Pulse>().has_value());
    zassert_true(solar::bus::emit<fixture::Pulse>().has_value());

    release_control_executor();
    wait_for(fixture::latest_runs, latest_before + 1);
    wait_for(fixture::pulse_runs, pulse_before + 1);
    zassert_equal(fixture::latest_value.load(std::memory_order_acquire), 3);
    zassert_equal(fixture::latest_runs.load(std::memory_order_acquire), latest_before + 1);
    zassert_equal(fixture::pulse_runs.load(std::memory_order_acquire), pulse_before + 1);

    const auto latest = solar::bus::record<fixture::DeferredSubscriber, fixture::LatestUpdate>();
    const auto pulse = solar::bus::record<fixture::DeferredSubscriber, fixture::Pulse>();
    zassert_true(latest.has_value());
    zassert_true(pulse.has_value());
    zassert_true(latest->replacements >= 2);
    zassert_true(pulse->coalesced >= 2);
}

ZTEST(solar_bus, test_latest_resubmits_when_value_arrives_in_flight)
{
    const auto before = fixture::latest_runs.load(std::memory_order_acquire);
    fixture::latest_handler_started.store(false, std::memory_order_release);
    fixture::latest_handler_release.store(false, std::memory_order_release);
    fixture::latest_handler_block.store(true, std::memory_order_release);

    zassert_true(solar::bus::emit<fixture::LatestUpdate>({.value = 10}).has_value());
    for (int attempt = 0;
         attempt < 100 && !fixture::latest_handler_started.load(std::memory_order_acquire);
         ++attempt) {
        k_sleep(K_MSEC(1));
    }
    zassert_true(fixture::latest_handler_started.load(std::memory_order_acquire));
    zassert_true(solar::bus::emit<fixture::LatestUpdate>({.value = 11}).has_value());
    zassert_true(solar::bus::emit<fixture::LatestUpdate>({.value = 12}).has_value());

    fixture::latest_handler_block.store(false, std::memory_order_release);
    fixture::latest_handler_release.store(true, std::memory_order_release);
    wait_for(fixture::latest_runs, before + 2);
    zassert_equal(fixture::latest_runs.load(std::memory_order_acquire), before + 2);
    zassert_equal(fixture::latest_value.load(std::memory_order_acquire), 12);
}

ZTEST(solar_bus, test_coalesced_resubmits_when_signaled_in_flight)
{
    const auto before = fixture::pulse_runs.load(std::memory_order_acquire);
    fixture::pulse_handler_started.store(false, std::memory_order_release);
    fixture::pulse_handler_release.store(false, std::memory_order_release);
    fixture::pulse_handler_block.store(true, std::memory_order_release);

    zassert_true(solar::bus::emit<fixture::Pulse>().has_value());
    for (int attempt = 0;
         attempt < 100 && !fixture::pulse_handler_started.load(std::memory_order_acquire);
         ++attempt) {
        k_sleep(K_MSEC(1));
    }
    zassert_true(fixture::pulse_handler_started.load(std::memory_order_acquire));
    zassert_true(solar::bus::emit<fixture::Pulse>().has_value());
    zassert_true(solar::bus::emit<fixture::Pulse>().has_value());

    fixture::pulse_handler_block.store(false, std::memory_order_release);
    fixture::pulse_handler_release.store(true, std::memory_order_release);
    wait_for(fixture::pulse_runs, before + 2);
    zassert_equal(fixture::pulse_runs.load(std::memory_order_acquire), before + 2);
}

ZTEST(solar_bus, test_queued_overflow_policies_and_try_override)
{
    hold_control_executor();
    const auto reject_before = fixture::reject_runs.load(std::memory_order_acquire);
    const auto newest_before = fixture::drop_newest_runs.load(std::memory_order_acquire);
    const auto oldest_before = fixture::drop_oldest_runs.load(std::memory_order_acquire);
    const auto wait_before = fixture::wait_runs.load(std::memory_order_acquire);

    zassert_true(solar::bus::emit<fixture::RejectUpdate>({.value = 1}).has_value());
    zassert_true(solar::bus::emit<fixture::RejectUpdate>({.value = 2}).has_value());
    auto rejected = solar::bus::emit<fixture::RejectUpdate>({.value = 3});
    zassert_false(rejected.has_value());
    zassert_equal(rejected.error().reason, solar::bus::Reason::RouteRejected);

    zassert_true(solar::bus::emit<fixture::DropNewestUpdate>({.value = 1}).has_value());
    zassert_true(solar::bus::emit<fixture::DropNewestUpdate>({.value = 2}).has_value());
    zassert_true(solar::bus::emit<fixture::DropNewestUpdate>({.value = 3}).has_value());

    zassert_true(solar::bus::emit<fixture::DropOldestUpdate>({.value = 1}).has_value());
    zassert_true(solar::bus::emit<fixture::DropOldestUpdate>({.value = 2}).has_value());
    zassert_true(solar::bus::emit<fixture::DropOldestUpdate>({.value = 3}).has_value());

    zassert_true(solar::bus::emit<fixture::WaitUpdate>({.value = 1}).has_value());
    auto try_wait = solar::bus::try_emit<fixture::WaitUpdate>({.value = 2});
    zassert_false(try_wait.has_value());
    zassert_equal(try_wait.error().reason, solar::bus::Reason::RouteRejected);
    auto timed_out = solar::bus::emit<fixture::WaitUpdate>({.value = 3});
    zassert_false(timed_out.has_value());
    zassert_equal(timed_out.error().reason, solar::bus::Reason::RouteTimedOut);

    release_control_executor();
    wait_for(fixture::reject_runs, reject_before + 2);
    wait_for(fixture::drop_newest_runs, newest_before + 2);
    wait_for(fixture::drop_oldest_runs, oldest_before + 2);
    wait_for(fixture::wait_runs, wait_before + 1);

    zassert_equal(fixture::reject_values[reject_before], 1);
    zassert_equal(fixture::reject_values[reject_before + 1], 2);
    zassert_equal(fixture::drop_newest_values[newest_before], 1);
    zassert_equal(fixture::drop_newest_values[newest_before + 1], 2);
    zassert_equal(fixture::drop_oldest_values[oldest_before], 2);
    zassert_equal(fixture::drop_oldest_values[oldest_before + 1], 3);
    zassert_equal(fixture::wait_values[wait_before], 1);
}

ZTEST(solar_bus, test_concurrent_producers_share_route_without_lost_messages)
{
    hold_control_executor();
    const auto before = fixture::concurrent_runs.load(std::memory_order_acquire);
    fixture::concurrent_ready.store(0, std::memory_order_release);
    fixture::concurrent_go.store(false, std::memory_order_release);
    fixture::concurrent_failures.store(0, std::memory_order_release);

    ProducerArguments first{.first = 0, .count = 16};
    ProducerArguments second{.first = 100, .count = 16};
    solar::kernel::Thread<2048> first_thread;
    solar::kernel::Thread<2048> second_thread;
    const solar::kernel::ThreadConfiguration configuration{
        .priority = solar::kernel::Priority::preemptive<3>(),
    };
    zassert_equal(first_thread.launch(&concurrent_producer, &first, configuration),
                  solar::Status::Ok);
    zassert_equal(second_thread.launch(&concurrent_producer, &second, configuration),
                  solar::Status::Ok);
    for (int attempt = 0;
         attempt < 100 && fixture::concurrent_ready.load(std::memory_order_acquire) != 2;
         ++attempt) {
        k_sleep(K_MSEC(1));
    }
    zassert_equal(fixture::concurrent_ready.load(std::memory_order_acquire), 2);
    fixture::concurrent_go.store(true, std::memory_order_release);
    zassert_equal(first_thread.join(solar::kernel::Timeout::after(200ms)), solar::Status::Ok);
    zassert_equal(second_thread.join(solar::kernel::Timeout::after(200ms)), solar::Status::Ok);
    zassert_equal(fixture::concurrent_failures.load(std::memory_order_acquire), 0);

    release_control_executor();
    wait_for(fixture::concurrent_runs, before + 32);
    const auto record =
        solar::bus::record<fixture::DeferredSubscriber, fixture::ConcurrentUpdate>();
    zassert_true(record.has_value());
    zassert_true(record->pending_high_water >= 32);
}

ZTEST(solar_bus, test_inline_failure_does_not_starve_later_route)
{
    const auto before = fixture::fault_success_runs.load(std::memory_order_acquire);
    auto emitted = solar::bus::emit<fixture::FaultUpdate>({.value = 1});
    zassert_false(emitted.has_value());
    zassert_equal(emitted.error().reason, solar::bus::Reason::InlineHandlerFailed);
    zassert_equal(emitted.error().attempted_routes, 2);
    zassert_equal(emitted.error().accepted_routes, 2);
    zassert_equal(emitted.error().rejected_routes, 1);
    zassert_equal(fixture::fault_success_runs.load(std::memory_order_acquire), before + 1);
    const auto failed = solar::bus::record<fixture::FailingSubscriber, fixture::FaultUpdate>();
    zassert_true(failed.has_value());
    zassert_true(failed->has_handler_failure);
    zassert_equal(failed->first_handler_failure, solar::Status::Error);
    zassert_equal(failed->last_handler_failure, solar::Status::Error);
}

ZTEST(solar_bus, test_async_handler_failure_is_recorded)
{
    const auto before = fixture::async_fault_runs.load(std::memory_order_acquire);
    zassert_true(solar::bus::emit<fixture::AsyncFaultUpdate>({.value = 1}).has_value());
    wait_for(fixture::async_fault_runs, before + 1);

    solar::bus::RouteRecord record{};
    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto current =
            solar::bus::record<fixture::DeferredSubscriber, fixture::AsyncFaultUpdate>();
        zassert_true(current.has_value());
        record = *current;
        if (record.has_handler_failure) {
            break;
        }
        k_sleep(K_MSEC(1));
    }
    zassert_true(record.has_handler_failure);
    zassert_equal(record.handler_failed, 1);
    zassert_equal(record.first_handler_failure, solar::Status::Error);
    zassert_equal(record.last_handler_failure, solar::Status::Error);
}

ZTEST(solar_bus, test_reentrant_inline_delivery_is_depth_first)
{
    fixture::reentrant_index.store(0, std::memory_order_release);
    zassert_true(solar::bus::emit<fixture::ReentrantUpdate>({.value = 1}).has_value());
    zassert_equal(fixture::reentrant_index.load(std::memory_order_acquire), 4);
    zassert_equal(fixture::reentrant_order[0], 11);
    zassert_equal(fixture::reentrant_order[1], 21);
    zassert_equal(fixture::reentrant_order[2], 22);
    zassert_equal(fixture::reentrant_order[3], 12);
}

ZTEST(solar_bus, test_isr_emission_reaches_every_compatible_route)
{
    const auto inline_before = fixture::isr_inline_runs.load(std::memory_order_acquire);
    const auto queued_before = fixture::isr_queued_runs.load(std::memory_order_acquire);
    fixture::isr_emit_ok.store(false, std::memory_order_release);

    irq_offload(&emit_from_isr, nullptr);
    zassert_true(fixture::isr_emit_ok.load(std::memory_order_acquire));
    zassert_equal(fixture::isr_inline_runs.load(std::memory_order_acquire), inline_before + 1);
    zassert_equal(fixture::isr_value.load(std::memory_order_acquire), 91);
    wait_for(fixture::isr_queued_runs, queued_before + 1);
}

ZTEST(solar_bus, test_zero_subscriber_and_relaxed_unregistered_behavior)
{
    zassert_true(solar::bus::emit<fixture::UnhandledUpdate>({.value = 5}).has_value());
    if constexpr (!solar::frontend::strict) {
        auto unknown = solar::bus::emit<fixture::UnknownUpdate>({.value = 8});
        zassert_false(unknown.has_value());
        zassert_equal(unknown.error().reason, solar::bus::Reason::NotRegistered);
    }
}

ZTEST(solar_bus, test_route_records_are_focused_and_independent)
{
    const auto inline_record = solar::bus::record<fixture::InlineSubscriber, fixture::Ping>();
    const auto queued_record = solar::bus::record<fixture::QueuedSubscriber, fixture::Ping>();

    zassert_true(inline_record.has_value());
    zassert_true(queued_record.has_value());
    zassert_true(inline_record->delivered >= 1);
    zassert_true(queued_record->delivered >= 1);
    zassert_equal(inline_record->delivery, solar::bus::DeliveryKind::Inline);
    zassert_equal(queued_record->delivery, solar::bus::DeliveryKind::Queued);
}
