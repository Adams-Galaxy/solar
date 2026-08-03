#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <thread>
#include <vector>

#include <solar/events/store.hpp>
#include <solar/log/store.hpp>
#include <solar/metrics/store.hpp>
#include <solar/persistence/storage.hpp>
#include <solar/supervisor/monitor.hpp>

namespace
{
struct Pulse
{
    using Value = int;
};
struct Counter
{
    using Value = std::uint32_t;
};
struct WorkerHealth
{};
std::atomic_uint32_t observed{};
void observe(const int& value) noexcept
{
    observed.fetch_add(static_cast<std::uint32_t>(value), std::memory_order_relaxed);
}
} // namespace

int main()
{
    solar::log::RecordStore<unsigned, 32> logs;
    assert(logs.initialize());
    std::vector<std::thread> writers;
    for (unsigned worker{}; worker < 4; ++worker) {
        writers.emplace_back([&, worker] {
            for (unsigned value{}; value < 100; ++value) {
                assert(logs.append(worker * 100 + value));
            }
        });
    }
    for (auto& writer : writers) {
        writer.join();
    }
    assert(logs.report().retained == 32);
    assert(logs.report().lost == 368);

    solar::events::EventBus<solar::events::Schema<Pulse>, 1> events;
    assert(events.initialize());
    assert(events.observe<Pulse>(&observe));
    writers.clear();
    for (unsigned worker{}; worker < 4; ++worker) {
        writers.emplace_back([&] {
            for (unsigned value{}; value < 100; ++value) {
                assert(*events.emit<Pulse>(1) == 1);
            }
        });
    }
    for (auto& writer : writers) {
        writer.join();
    }
    assert(observed.load(std::memory_order_relaxed) == 400);

    solar::metrics::MetricStore<solar::metrics::Schema<Counter>> metrics;
    assert(metrics.initialize());
    writers.clear();
    for (unsigned worker{}; worker < 4; ++worker) {
        writers.emplace_back([&] {
            for (unsigned value{}; value < 100; ++value) {
                assert(metrics.observe<Counter>(1));
            }
        });
    }
    for (auto& writer : writers) {
        writer.join();
    }
    const auto aggregate = metrics.summary<Counter>();
    assert(aggregate && aggregate->count == 400 && aggregate->total == 400);

    solar::persistence::MemoryStorage<4, 4> persistence;
    assert(persistence.initialize());
    writers.clear();
    for (unsigned worker{}; worker < 4; ++worker) {
        writers.emplace_back([&, worker] {
            const std::array bytes{std::byte(worker), std::byte{0x5A}};
            for (unsigned iteration{}; iteration < 100; ++iteration) {
                assert(persistence.write(worker + 1, bytes));
            }
        });
    }
    for (auto& writer : writers) {
        writer.join();
    }
    for (unsigned worker{}; worker < 4; ++worker) {
        std::array<std::byte, 4> output{};
        assert(*persistence.read(worker + 1, output) == 2);
        assert(output[0] == std::byte(worker));
    }

    solar::supervisor::Monitor<solar::supervisor::Schema<WorkerHealth>> monitor;
    assert(monitor.initialize());
    writers.clear();
    for (unsigned worker{}; worker < 4; ++worker) {
        writers.emplace_back([&] {
            for (unsigned iteration{}; iteration < 100; ++iteration) {
                assert(monitor.report<WorkerHealth>(solar::supervisor::Condition::Healthy));
            }
        });
    }
    for (auto& writer : writers) {
        writer.join();
    }
    assert(monitor.state<WorkerHealth>().revision == 400);
}
