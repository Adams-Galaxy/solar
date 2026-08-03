#include <array>
#include <cassert>
#include <cstddef>
#include <span>

#include <solar/events/adapters.hpp>
#include <solar/events/store.hpp>
#include <solar/execution/service_runner.hpp>
#include <solar/execution/task_queue.hpp>
#include <solar/log/adapters.hpp>
#include <solar/log/static_logger.hpp>
#include <solar/log/store.hpp>
#include <solar/metrics/adapters.hpp>
#include <solar/metrics/store.hpp>
#include <solar/persistence/storage.hpp>
#include <solar/remote/stream_sink.hpp>
#include <solar/supervisor/adapters.hpp>
#include <solar/supervisor/monitor.hpp>
#include <solar/system/composer.hpp>

namespace fixture
{
struct Alarm
{
    using Value = int;
};
struct Temperature
{
    using Value = float;
};
struct RemoteHealth
{};
struct Application
{};
inline int observed{};
void observe_alarm(const int& value) noexcept
{
    observed += value;
}
struct Observer
{
    static void observe(const int& value) noexcept
    {
        observed += value;
    }
};
struct Sink
{
    solar::Result<void> write(const int& value)
    {
        values[count++] = value;
        return {};
    }
    std::array<int, 4> values{};
    std::size_t count{};
};
struct FailingSink
{
    solar::Result<void> write(const int&)
    {
        return solar::fail<solar::Error>({.status = solar::Status::Error});
    }
};
struct MetricSink
{
    solar::Result<void> write(const solar::metrics::Sample<Temperature>& sample)
    {
        value = sample.value;
        return {};
    }
    float value{};
};
struct HealthProbe
{
    static solar::Result<solar::supervisor::Condition> condition() noexcept
    {
        return solar::supervisor::Condition::Healthy;
    }
};
struct PassiveService
{
    static constexpr std::string_view name = "fixture.passive";
    static solar::Result<void> initialize() noexcept
    {
        initialized = true;
        return {};
    }
    static solar::Result<void> deinitialize() noexcept
    {
        initialized = false;
        return {};
    }
    inline static bool initialized{};
};
struct ExportStream
{
    using Value = int;
};
struct FakeRemote
{
    template <typename Stream>
    static solar::Result<std::size_t> publish(const typename Stream::Value& value) noexcept
    {
        last = value;
        return 1;
    }
    inline static int last{};
};
struct LogSource
{
    static constexpr solar::log::SourceDescriptor descriptor{
        .name = "fixture.logger",
        .description = "Standalone logger fixture",
    };
};
struct LogSink
{
    static solar::Result<void> write(std::string_view rendered) noexcept
    {
        last_size = rendered.copy(last.data(), last.size());
        ++writes;
        return {};
    }

    inline static std::array<char, 64> last{};
    inline static std::size_t last_size{};
    inline static std::size_t writes{};
};
struct IntCodec
{
    static solar::Result<std::size_t> encode(int value, std::span<std::byte> output) noexcept
    {
        if (output.size() < sizeof(value)) {
            return solar::fail<solar::Error>({.status = solar::Status::NoSpace});
        }
        for (std::size_t index{}; index < sizeof(value); ++index) {
            output[index] = std::byte(static_cast<unsigned>(value) >> (index * 8));
        }
        return sizeof(value);
    }
};
} // namespace fixture

int main()
{
    using Logger =
        solar::log::StaticLogger<fixture::Application, solar::TypeList<fixture::LogSource>,
                                 solar::TypeList<solar::log::domain::Unclassified>,
                                 solar::TypeList<fixture::LogSink>, 2>;
    assert(Logger::initialize());
    assert(Logger::info<fixture::LogSource>("initializing"));
    assert(Logger::start());
    assert(Logger::info<fixture::LogSource>("value={}", 7));
    assert(Logger::info<fixture::LogSource>("second"));
    assert(Logger::info<fixture::LogSource>("third"));
    const auto logger_record = Logger::record();
    assert(logger_record.captured == 4);
    assert(logger_record.history_used == 2);
    assert(logger_record.history_evicted == 2);
    assert(fixture::LogSink::writes == 4);
    std::array<solar::log::Record, 2> replay_scratch{};
    const auto replay = Logger::replay<fixture::LogSink>({}, replay_scratch);
    assert(replay && replay->stale && replay->written == 2 && replay->evicted_before == 2);
    assert(fixture::LogSink::writes == 6);
    assert(Logger::stop());
    assert(!Logger::info<fixture::LogSource>("closed again"));
    assert(Logger::deinitialize());

    solar::log::RecordStore<int, 2> logs;
    assert(logs.initialize());
    assert(logs.append(1));
    assert(logs.append(2));
    assert(logs.append(3));
    fixture::Sink sink;
    assert(*logs.replay(sink) == 2);
    assert(logs.lost() == 1);
    assert(logs.report().retained == 2 && logs.report().lost == 1);
    assert(sink.values[0] == 2 && sink.values[1] == 3);
    solar::log::RecordStore<int, 2> captured;
    assert(captured.initialize());
    fixture::Sink immediate;
    solar::log::Capture capture{captured, immediate};
    assert(capture.write(7));
    assert(captured.size() == 1 && immediate.values[0] == 7);
    fixture::FailingSink failing;
    solar::log::Capture failing_capture{captured, failing};
    assert(!failing_capture.write(8));
    assert(captured.size() == 2);
    using RemoteSink = solar::remote::StreamSink<fixture::FakeRemote, fixture::ExportStream>;
    assert(RemoteSink::write(13));
    assert(fixture::FakeRemote::last == 13);

    solar::events::EventBus<solar::events::Schema<fixture::Alarm>, 1> events;
    assert(events.initialize());
    assert(events.observe<fixture::Alarm>(&fixture::observe_alarm));
    assert(!events.observe<fixture::Alarm>(&fixture::observe_alarm));
    assert(*events.emit<fixture::Alarm>(4) == 1 && fixture::observed == 4);
    assert(events.unobserve<fixture::Alarm>(&fixture::observe_alarm));
    assert(*events.emit<fixture::Alarm>(4) == 0);

    solar::events::EventBus<solar::events::Schema<fixture::Alarm>, 1, solar::events::RetainLatest>
        retained_events;
    assert(retained_events.initialize());
    assert(*retained_events.emit<fixture::Alarm>(6) == 0);
    fixture::observed = 0;
    assert(retained_events.observe<fixture::Alarm>(&fixture::observe_alarm));
    assert(fixture::observed == 6);

    solar::execution::TaskQueue<1> scheduled_tasks;
    assert(scheduled_tasks.initialize());
    solar::events::ScheduledForward<fixture::Alarm, fixture::Observer, 1> scheduled;
    assert(scheduled.emit(5, scheduled_tasks));
    assert(!scheduled.emit(6, scheduled_tasks));
    fixture::observed = 0;
    assert(*scheduled_tasks.drain() == 1 && fixture::observed == 5);

    solar::metrics::MetricStore<solar::metrics::Schema<fixture::Temperature>> metrics;
    assert(metrics.initialize());
    assert(metrics.set<fixture::Temperature>(23.5F));
    assert(metrics.get<fixture::Temperature>()->value == 23.5F);
    assert(metrics.observe<fixture::Temperature>(20.0F));
    assert(metrics.observe<fixture::Temperature>(24.0F));
    const auto summary = metrics.summary<fixture::Temperature>();
    assert(summary && summary->count == 2 && summary->total == 44.0F && summary->minimum == 20.0F &&
           summary->maximum == 24.0F);
    fixture::MetricSink metric_sink;
    using MetricExport = solar::metrics::Export<decltype(metrics), fixture::Temperature>;
    assert(MetricExport::write(metrics, metric_sink));
    assert(metric_sink.value == 24.0F);
    using MetricInspection = solar::metrics::Inspect<decltype(metrics), fixture::Temperature>;
    assert(MetricInspection::read(metrics)->value == 24.0F);

    solar::persistence::MemoryStorage<1, 4> storage;
    static_assert(solar::persistence::Storage<decltype(storage)>);
    assert(storage.initialize());
    std::array input{std::byte{1}, std::byte{2}};
    assert(storage.write(7, input));
    std::array<std::byte, 4> output{};
    assert(*storage.read(7, output) == 2);
    assert(output[0] == std::byte{1});
    assert(!storage.write(8, input));
    assert(!storage.read(7, std::span<std::byte>{output}.first(1)));

    using StaticStorage = solar::persistence::StaticMemoryStorage<fixture::Application, 1, 4>;
    assert(StaticStorage::initialize());
    assert(StaticStorage::write(7, input));
    assert(*StaticStorage::read(7, output) == 2);
    solar::persistence::MemoryStorage<1, sizeof(int)> log_storage;
    assert(log_storage.initialize());
    solar::log::PersistenceSink<decltype(log_storage), fixture::IntCodec, 9, sizeof(int)>
        persisted_log{log_storage};
    assert(persisted_log.write(0x1234));
    assert(*log_storage.read(9, output) == sizeof(int));

    int task_count{};
    solar::execution::TaskQueue<1> tasks;
    assert(tasks.initialize());
    const auto task =
        tasks.enqueue([](void* context) noexcept { ++*static_cast<int*>(context); }, &task_count);
    assert(task);
    assert(!tasks.enqueue([](void*) noexcept {}));
    assert(*tasks.drain() == 1);
    assert(task_count == 1);
    assert(
        tasks.enqueue([](void* context) noexcept { ++*static_cast<int*>(context); }, &task_count));
    assert(*tasks.stop(solar::execution::ShutdownPolicy::Cancel) == 1);
    assert(!tasks.enqueue([](void*) noexcept {}));
    using StaticTasks = solar::execution::StaticTaskQueue<fixture::Application, 1>;
    assert(StaticTasks::initialize());
    assert(StaticTasks::enqueue([](void* context) noexcept { ++*static_cast<int*>(context); },
                                &task_count));
    assert(*StaticTasks::drain() == 1 && task_count == 2);

    solar::supervisor::Monitor<solar::supervisor::Schema<fixture::RemoteHealth>> monitor;
    assert(monitor.initialize());
    assert(monitor.report<fixture::RemoteHealth>(solar::supervisor::Condition::Healthy));
    assert(monitor.state<fixture::RemoteHealth>().condition ==
           solar::supervisor::Condition::Healthy);
    assert(monitor.overall() == solar::supervisor::Condition::Healthy);

    using StaticLogs = solar::log::StaticRecordStore<fixture::Application, int, 2>;
    assert(StaticLogs::initialize());
    assert(StaticLogs::append(9));
    fixture::Sink static_sink;
    assert(*StaticLogs::replay(static_sink) == 1 && static_sink.values[0] == 9);

    using StaticEvents = solar::events::StaticEventBus<fixture::Application,
                                                       solar::events::Schema<fixture::Alarm>, 1>;
    fixture::observed = 0;
    assert(StaticEvents::initialize());
    assert(StaticEvents::observe<fixture::Alarm>(&fixture::observe_alarm));
    assert(*StaticEvents::emit<fixture::Alarm>(3) == 1 && fixture::observed == 3);

    using StaticMetrics =
        solar::metrics::StaticMetricStore<fixture::Application,
                                          solar::metrics::Schema<fixture::Temperature>>;
    assert(StaticMetrics::initialize());
    assert(StaticMetrics::set<fixture::Temperature>(12.0F));
    assert(StaticMetrics::get<fixture::Temperature>()->value == 12.0F);

    using StaticMonitor =
        solar::supervisor::StaticMonitor<fixture::Application,
                                         solar::supervisor::Schema<fixture::RemoteHealth>>;
    assert(StaticMonitor::initialize());
    assert(StaticMonitor::report<fixture::RemoteHealth>(solar::supervisor::Condition::Degraded));
    assert(StaticMonitor::state<fixture::RemoteHealth>().condition ==
           solar::supervisor::Condition::Degraded);
    using ProbeAdapter =
        solar::supervisor::Evaluate<StaticMonitor, fixture::RemoteHealth, fixture::HealthProbe>;
    assert(ProbeAdapter::poll());
    assert(StaticMonitor::state<fixture::RemoteHealth>().condition ==
           solar::supervisor::Condition::Healthy);

    using Runner =
        solar::execution::ServiceRunner<fixture::Application, fixture::PassiveService, 1024, 1>;
    assert(Runner::initialize());
    assert(fixture::PassiveService::initialized);
    assert(Runner::start());
    assert(Runner::stop());
    assert(Runner::deinitialize());
    assert(!fixture::PassiveService::initialized);

    using ComposedEvents = solar::events::StaticEventBus<fixture::Application,
                                                         solar::events::Schema<fixture::Alarm>, 1>;
    using Composition = solar::Compose<
        solar::Own<StaticLogs, ComposedEvents, StaticMetrics, StaticStorage, StaticTasks,
                   StaticMonitor>,
        solar::Connect<solar::events::Forward, ComposedEvents, fixture::Alarm, fixture::Observer>>;
    using System = solar::system::System<fixture::Application, Composition>;
    fixture::observed = 0;
    assert(System::boot());
    assert(*ComposedEvents::emit<fixture::Alarm>(11) == 1 && fixture::observed == 11);
    assert(System::shutdown());
}
