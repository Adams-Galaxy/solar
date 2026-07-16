#include <atomic>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <solar/solar.hpp>

using namespace std::chrono_literals;
using namespace solar::literals;

namespace fixture
{

struct DrainMessage
{
    int value{};
    static constexpr solar::bus::Descriptor descriptor{.name = "shutdown.drain"};
};

struct CancelMessage
{
    int value{};
    static constexpr solar::bus::Descriptor descriptor{.name = "shutdown.cancel"};
};

inline std::atomic_bool blocker_started{};
inline std::atomic_bool blocker_release{};
inline std::atomic_uint drain_runs{};
inline std::atomic_uint cancel_runs{};

using Queue = solar::execution::WorkQueue<"shutdown-queue", solar::execution::StackSize<2048>,
                                          solar::execution::Priority<2>,
                                          solar::execution::StopTimeout<200_ms>>;

struct Blocker
{
    static void execute()
    {
        blocker_started.store(true, std::memory_order_release);
        while (!blocker_release.load(std::memory_order_acquire)) {
            k_sleep(K_MSEC(1));
        }
    }
};

using BlockerTask = solar::execution::OnDemand<"shutdown-blocker", Blocker, Queue>;

struct Producer
{
    static constexpr solar::component::Descriptor descriptor{.name = "producer"};
    using Messages = solar::bus::Messages<DrainMessage, CancelMessage>;
    using Tasks = solar::execution::Tasks<BlockerTask>;
};

struct Subscriber
{
    static constexpr solar::component::Descriptor descriptor{.name = "subscriber"};
    using Subscriptions = solar::bus::Subscriptions<
        solar::bus::On<DrainMessage,
                       solar::bus::delivery::Queued<Queue, 2, solar::bus::UseDefaultOverflow,
                                                    solar::bus::stop::Drain>>,
        solar::bus::On<CancelMessage,
                       solar::bus::delivery::Queued<Queue, 2, solar::bus::UseDefaultOverflow,
                                                    solar::bus::stop::CancelPending>>>;

    static void handle(const DrainMessage&)
    {
        drain_runs.fetch_add(1, std::memory_order_release);
    }

    static void handle(const CancelMessage&)
    {
        cancel_runs.fetch_add(1, std::memory_order_release);
    }
};

using System = solar::System<
    solar::Blueprint<solar::Facilities<Producer, Subscriber>, solar::Executors<Queue>>>;

} // namespace fixture

SOLAR_BIND_SYSTEM(fixture::System);

namespace
{

void release_blocker(void*) noexcept
{
    k_sleep(K_MSEC(20));
    fixture::blocker_release.store(true, std::memory_order_release);
}

} // namespace

ZTEST(solar_bus_shutdown, test_drain_and_cancel_pending_share_execution_containment)
{
    auto boot = fixture::System::boot();
    zassert_true(boot.has_value());

    fixture::blocker_started.store(false, std::memory_order_release);
    fixture::blocker_release.store(false, std::memory_order_release);
    zassert_true(solar::execution::submit<fixture::BlockerTask>().has_value());
    for (int attempt = 0;
         attempt < 100 && !fixture::blocker_started.load(std::memory_order_acquire); ++attempt) {
        k_sleep(K_MSEC(1));
    }
    zassert_true(fixture::blocker_started.load(std::memory_order_acquire));

    zassert_true(solar::bus::emit<fixture::DrainMessage>({.value = 1}).has_value());
    zassert_true(solar::bus::emit<fixture::CancelMessage>({.value = 2}).has_value());

    solar::kernel::Thread<1024> releaser;
    const solar::kernel::ThreadConfiguration configuration{
        .priority = solar::kernel::Priority::preemptive<3>(),
    };
    zassert_equal(releaser.launch(&release_blocker, nullptr, configuration), solar::Status::Ok);

    auto stopped = fixture::System::stop();
    zassert_true(stopped.has_value());
    zassert_equal(releaser.join(solar::kernel::Timeout::after(100ms)), solar::Status::Ok);
    zassert_equal(fixture::drain_runs.load(std::memory_order_acquire), 1);
    zassert_equal(fixture::cancel_runs.load(std::memory_order_acquire), 0);

    const auto drained = solar::bus::record<fixture::Subscriber, fixture::DrainMessage>();
    const auto cancelled = solar::bus::record<fixture::Subscriber, fixture::CancelMessage>();
    zassert_true(drained.has_value());
    zassert_true(cancelled.has_value());
    zassert_equal(drained->delivered, 1);
    zassert_equal(drained->pending, 0);
    zassert_equal(cancelled->delivered, 0);
    zassert_equal(cancelled->cancelled, 1);
    zassert_equal(cancelled->pending, 0);
}

ZTEST_SUITE(solar_bus_shutdown, nullptr, nullptr, nullptr, nullptr, nullptr);
