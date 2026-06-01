#include "solar/solar.hpp"
#include "low_level/serial.hpp"

#include <atomic>
#include <cassert>
#include <cstring>
#include <cstdint>
#include <type_traits>

namespace
{

struct TestBoard
{
    using Metrics = solar::metrics::List<
        solar::metrics::Metric<solar::Name<"board.boot.count">, std::uint32_t>>;

    template <typename ContextT>
    solar::Status init(ContextT &)
    {
        initialized = true;
        return solar::Status::Ok;
    }

    bool initialized = false;
};

struct LeftMotor
{
    using Name = solar::Name<"left_motor">;

    template <typename ContextT>
    solar::Status init(ContextT &)
    {
        initialized = true;
        return solar::Status::Ok;
    }

    bool initialized = false;
};

struct DebugUsb
{
    using Name = solar::Name<"usb">;
    using Events = solar::events::List<
        solar::events::Event<solar::Name<"usb.started">>>;

    bool init()
    {
        initialized = true;
        return true;
    }

    bool initialized = false;
};

struct DebugLed
{
    using Name = solar::Name<"debug_led">;

    void init()
    {
        initialized = true;
    }

    bool initialized = false;
};

struct Imu
{
    using Name = solar::Name<"imu">;

    template <typename ContextT>
    solar::Status start(ContextT &)
    {
        running = true;
        return solar::Status::Ok;
    }

    bool running = false;
};

struct Meters
{
    using Name = solar::Name<"m">;
};

struct Volts
{
    using Name = solar::Name<"V">;
};

struct MicrosecondsUnit
{
    using Name = solar::Name<"us">;
};

using ControlLoopCounter = solar::metrics::Counter<solar::Name<"control.loop.count">>;
using BatteryVoltage = solar::metrics::Gauge<solar::Name<"battery.voltage">, float, Volts>;
using ControlLoopTimer = solar::metrics::Timer<
    solar::Name<"control.loop.time">,
    solar::metrics::WindowMean<2>,
    MicrosecondsUnit>;

struct ControlXError
{
    using Raw = solar::metrics::Gauge<
        solar::Name<"control.error.x">,
        float,
        Meters>;
    using Mean = solar::metrics::Sample<
        solar::Name<"control.error.x.mean">,
        float,
        Meters,
        solar::metrics::WindowMean<3>>;
    using Maximum = solar::metrics::Sample<
        solar::Name<"control.error.x.max">,
        float,
        Meters,
        solar::metrics::Max>;
    using Metrics = solar::metrics::List<Raw, Mean, Maximum>;

    template <typename StoreT>
    static void observe(float value)
    {
        StoreT::template set<Raw>(value);
        StoreT::template observe<Mean>(value);
        StoreT::template observe<Maximum>(value);
    }
};

struct Control
{
    using Name = solar::Name<"control">;
    using Thread = solar::ServiceSpec<Name, 512, solar::rtos::Priority::Low>;
    using Dependencies = solar::Dependencies<LeftMotor::Name, Imu::Name>;
    using Metrics = solar::metrics::List<
        ControlLoopCounter>;
    using RemoteObservables = solar::remote::Observables<
        solar::remote::Observable<
            solar::Name<"control.loop.count">,
            solar::remote::generated::Empty>>;

    template <typename ContextT>
    solar::Status start(ContextT &ctx)
    {
        if (!ctx.template Get<LeftMotor::Name>().initialized)
        {
            return solar::Status::DependencyFailed;
        }
        if (!ctx.template Get<Imu::Name>().running)
        {
            return solar::Status::DependencyFailed;
        }
        running = true;
        return solar::Status::Ok;
    }

    template <typename ContextT>
    void run(ContextT &, solar::StopToken stop)
    {
        while (!stop.stop_requested())
        {
            solar::rtos::ThisThread::sleep_for(solar::Milliseconds{1});
        }
    }

    bool running = false;
};

struct PassiveFacility
{
    using Name = solar::Name<"passive_facility">;
    using Dependencies = solar::Dependencies<DebugUsb::Name>;
    using Events = solar::events::List<
        solar::events::Event<solar::Name<"facility.ready">>>;
    using RemoteMethods = solar::remote::Methods<
        solar::remote::Method<
            solar::Name<"facility.ping">,
            solar::remote::generated::Empty,
            solar::remote::generated::Empty>>;

    template <typename ContextT>
    solar::Status init(ContextT &)
    {
        initialized = true;
        return solar::Status::Ok;
    }

    bool initialized = false;
};

struct RemoteCatalogFacility
{
    using Name = solar::Name<"remote_catalog_facility">;
    using RemoteMethods = solar::remote::Methods<
        solar::remote::Method<
            solar::Name<"remote_catalog_facility.ping">,
            solar::remote::generated::Empty,
            solar::remote::generated::Empty>>;
};

struct DuplicateMotor
{
    using Name = solar::Name<"left_motor">;
};

struct MissingDependency
{
    using Name = solar::Name<"missing_dep">;
    using Dependencies = solar::Dependencies<solar::Name<"ghost">>;
};

void control_task_entry(void *) {}

std::atomic<bool> host_task_ran = false;
bool profile_preflight_ran = false;
bool profile_awake_ran = false;

void host_task_entry(void *)
{
    host_task_ran = true;
}

using ControlTaskSpec = solar::TaskSpec<
    solar::Name<"threaded_control">,
    2048,
    solar::rtos::Priority::High2>;
using ThreadedControl = solar::Task<
    ControlTaskSpec,
    &control_task_entry,
    solar::Dependencies<LeftMotor::Name>>;
using HostTask = solar::Task<
    solar::TaskSpec<solar::Name<"host_task">, 512, solar::rtos::Priority::Low>,
    &host_task_entry>;

using Telemetry = solar::services::Channel<solar::Name<"telemetry">, std::uint32_t, 4>;

using ValidRobot = solar::System<
    TestBoard,
    solar::Peripherals<DebugUsb, DebugLed>,
    solar::Devices<LeftMotor, Imu>,
    solar::Facilities<PassiveFacility>,
    solar::Services<Control>,
    solar::Tasks<solar::TaskSpec<solar::Name<"control_task">, 1024>>,
    solar::Channels<Telemetry>>;

static_assert(solar::GraphValidV<
              solar::Peripherals<DebugUsb, DebugLed>,
              solar::Devices<LeftMotor, Imu>,
              solar::Facilities<PassiveFacility>,
              solar::Services<Control>,
              solar::Tasks<solar::TaskSpec<solar::Name<"control_task">, 1024>>,
              solar::Channels<Telemetry>>);

static_assert(!solar::GraphValidV<
              solar::Peripherals<>,
              solar::Devices<LeftMotor, DuplicateMotor>,
              solar::Facilities<>,
              solar::Services<>,
              solar::Tasks<>,
              solar::Channels<>>);

static_assert(!solar::GraphValidV<
              solar::Peripherals<>,
              solar::Devices<LeftMotor>,
              solar::Facilities<>,
              solar::Services<MissingDependency>,
              solar::Tasks<>,
              solar::Channels<>>);

static_assert(ControlTaskSpec::stack_words == 2048);
static_assert(ControlTaskSpec::priority == solar::rtos::Priority::High2);
static_assert(solar::rtos::priority_index(solar::rtos::Priority::Realtime5) == solar::rtos::PriorityLevels - 1);
static_assert(solar::rtos::from_native_priority(solar::rtos::to_native_priority(solar::rtos::Priority::High)) ==
              solar::rtos::Priority::High);
static_assert(solar::GraphValidV<
              solar::Peripherals<>,
              solar::Devices<LeftMotor>,
              solar::Facilities<>,
              solar::Services<>,
              solar::Tasks<ThreadedControl>,
              solar::Channels<>>);

using HostTaskRobot = solar::System<
    TestBoard,
    solar::Peripherals<>,
    solar::Devices<>,
    solar::Facilities<>,
    solar::Services<>,
    solar::Tasks<HostTask>,
    solar::Channels<>>;

struct DuplicateMetricA
{
    using Name = solar::Name<"duplicate.metric">;
};

struct DuplicateMetricB
{
    using Name = solar::Name<"duplicate.metric">;
};

static_assert(ValidRobot::MetricsCatalog::size == 2);
static_assert(ValidRobot::EventsCatalog::size == 2);
static_assert(ValidRobot::RemoteMethodsCatalog::size == 1);
static_assert(ValidRobot::RemoteObservablesCatalog::size == 1);
static_assert(solar::detail::UniqueNamesInListV<ValidRobot::MetricsCatalog>);
static_assert(!solar::detail::UniqueNamesInListV<solar::metrics::List<DuplicateMetricA, DuplicateMetricB>>);
static_assert(solar::detail::UniqueIdsInListV<ValidRobot::RemoteMethodsCatalog>);

using ControlLogSource = solar::log::Source<
    solar::Name<"control">,
    solar::log::Categories<solar::Name<"loop">, solar::Name<"fault">>>;

class TestUsbTransport
{
public:
    solar::Status init()
    {
        low_level::serial::init<low_level::serial::UsbSerial>(115200, 0);
        return solar::Status::Ok;
    }

    std::size_t write(const std::uint8_t *data, std::size_t len)
    {
        return low_level::serial::write<low_level::serial::UsbSerial>(data, len);
    }

    int read()
    {
        return low_level::serial::read<low_level::serial::UsbSerial>();
    }

    int available()
    {
        return low_level::serial::available<low_level::serial::UsbSerial>();
    }

    void flush()
    {
        low_level::serial::flush<low_level::serial::UsbSerial>();
    }
};

class StaticRemoteTransport
{
public:
    using Name = solar::Name<"static_remote_transport">;
    using Port = low_level::serial::Uart<1>;

    static solar::Status init()
    {
        low_level::serial::clear<Port>();
        low_level::serial::init<Port>(115200, 0);
        return solar::Status::Ok;
    }

    static std::size_t write(const std::uint8_t *data, std::size_t len)
    {
        return low_level::serial::write<Port>(data, len);
    }

    static std::size_t read(std::uint8_t *out, std::size_t len)
    {
        std::size_t count = 0;
        while (count < len && available() > 0)
        {
            const int value = read();
            if (value < 0)
            {
                break;
            }
            out[count++] = static_cast<std::uint8_t>(value);
        }
        return count;
    }

    static int read()
    {
        return low_level::serial::read<Port>();
    }

    static int available()
    {
        return low_level::serial::available<Port>();
    }

    static void flush()
    {
        low_level::serial::flush<Port>();
    }
};

using RemoteCatalogRobot = solar::System<
    TestBoard,
    solar::Peripherals<StaticRemoteTransport>,
    solar::Devices<>,
    solar::Facilities<RemoteCatalogFacility>,
    solar::Services<solar::services::Remote<StaticRemoteTransport>>,
    solar::Tasks<>,
    solar::Channels<>>;

static_assert(RemoteCatalogRobot::RemoteMethodsCatalog::size == 1);

using UsbLogSink = solar::log::Sink<
    solar::Name<"usb">,
    solar::log::SerialWriter<TestUsbTransport>,
    solar::log::CompactFormat,
    solar::log::Filter<solar::log::Level::Info>>;

using MemoryLogSink = solar::log::Sink<
    solar::Name<"memory">,
    solar::log::RingBufferSink<1024>,
    solar::log::JsonLinesFormat,
    solar::log::Filter<solar::log::Level::Debug>>;

using TestLogger = solar::log::Logger<
    solar::Name<"log">,
    solar::log::Sinks<UsbLogSink, MemoryLogSink>,
    solar::log::Policy::Direct>;

using EventMemorySink = solar::events::Sink<
    solar::Name<"event_memory">,
    solar::log::RingBufferSink<512>,
    solar::events::CompactFormat,
    solar::events::Filter<solar::events::Severity::Info>>;

using TestEvents = solar::events::Facility<
    solar::Name<"events">,
    2,
    solar::events::Sinks<EventMemorySink>>;

using FacilityReadyEvent = solar::events::Event<solar::Name<"facility.ready">>;
using DebugTraceEvent = solar::events::Event<solar::Name<"debug.trace">, void, solar::events::Severity::Debug>;
using FaultEvent = solar::events::Event<solar::Name<"fault.latched">, void, solar::events::Severity::Error>;

struct TestRuntimeConfig
{
    static constexpr std::uint16_t RemoteHeartbeatMs = 250;
    static constexpr std::uint16_t RemoteSessionTimeoutMs = 1250;
};

using LoggerRobot = solar::System<
    TestBoard,
    solar::Peripherals<>,
    solar::Devices<>,
    solar::Facilities<solar::facilities::Inspection>,
    solar::Services<>,
    solar::Tasks<>,
    solar::Channels<>>;

using LoggedEntryRobot = solar::System<
    TestBoard,
    typename ValidRobot::PeripheralList,
    typename ValidRobot::DeviceList,
    typename ValidRobot::FacilityList,
    typename ValidRobot::ServiceList,
    typename ValidRobot::TaskList,
    typename ValidRobot::ChannelList,
    solar::Runtime<
        solar::Logging<TestLogger>,
        solar::Config<TestRuntimeConfig>>>;

static_assert(std::is_same_v<typename LoggerRobot::Logger, solar::log::NullLogger>);
static_assert(std::is_same_v<typename LoggedEntryRobot::Logging, solar::Logging<TestLogger>>);
static_assert(std::is_same_v<typename LoggedEntryRobot::Logger, TestLogger>);
static_assert(std::is_same_v<typename LoggedEntryRobot::UserConfig, TestRuntimeConfig>);
static_assert(LoggedEntryRobot::Config::RemoteHeartbeatMs == 250);
static_assert(LoggedEntryRobot::Config::RemotePayloadBytes == solar::DefaultConfig::RemotePayloadBytes);

using ControlLog = TestLogger::Log<
    solar::Name<"control">,
    solar::log::Categories<solar::Name<"loop">, solar::Name<"fault">>>;

struct EntryProfile
{
    using System = LoggedEntryRobot;

    static solar::Status preflight()
    {
        profile_preflight_ran = true;
        return solar::Status::Ok;
    }

    static void awake(System &system, solar::BootReport const &report)
    {
        assert(report.ok());
        assert(system.BoardObject().initialized);
        profile_awake_ran = true;
    }

    static bool finished(System &)
    {
        return true;
    }
};

} // namespace

int main()
{
    ValidRobot robot;
    assert(robot.Boot() == solar::Status::Ok);
    assert(robot.BoardObject().initialized);
    assert(robot.template Get<DebugUsb::Name>().initialized);
    assert(robot.template Peripheral<DebugUsb>().initialized);
    assert(robot.template Get<DebugLed::Name>().initialized);
    assert(robot.template Get<PassiveFacility::Name>().initialized);
    assert(robot.template Facility<PassiveFacility>().initialized);
    assert(robot.template Get<LeftMotor::Name>().initialized);
    assert(robot.template Get<Imu::Name>().running);
    assert(robot.template Service<Control>().running);

    auto snapshots = robot.Snapshots();
    assert(snapshots.size() == ValidRobot::SnapshotCapacity());
    assert(ValidRobot::SnapshotCapacity() == 8);

    auto &telemetry = robot.template Channel<Telemetry>();
    assert(telemetry.publish(42) == solar::Status::Ok);
    const auto received = telemetry.try_receive();
    assert(received);
    assert(received.value() == 42);

    solar::rtos::Queue<std::uint32_t, 2> queue;
    assert(queue.try_send(10) == solar::Status::Ok);
    std::uint32_t queued = 0;
    assert(queue.try_receive(queued) == solar::Status::Ok);
    assert(queued == 10);

    solar::rtos::Semaphore semaphore{1, 1};
    assert(semaphore.try_take() == solar::Status::Ok);
    assert(semaphore.try_take() == solar::Status::Timeout);

    HostTaskRobot host_task_robot;
    assert(host_task_robot.Boot() == solar::Status::Ok);
    for (int i = 0; i < 20 && !host_task_ran; ++i)
    {
        solar::rtos::ThisThread::sleep_for(solar::Milliseconds{1});
    }
    assert(host_task_ran);

    assert(solar::entry::preflight<EntryProfile>() == solar::Status::Ok);
    assert(solar::entry::init_facilities<EntryProfile>() == solar::Status::Ok);
    assert(solar::entry::start_facilities<EntryProfile>() == solar::Status::Ok);
    EntryProfile::System entry_system;
    assert(solar::entry::boot<EntryProfile>(entry_system) == solar::Status::Ok);
    assert(profile_preflight_ran);
    assert(profile_awake_ran);
    assert(solar::entry::finished<EntryProfile>(entry_system));
    assert(solar::entry::exit_code<EntryProfile>(entry_system) == 0);

    using UsbSerial = low_level::serial::UsbSerial;
    low_level::serial::clear<UsbSerial>();
    low_level::serial::init<UsbSerial>(115200, 0);
    TestLogger::reset();
    assert(TestLogger::init() == solar::Status::Ok);

    LoggerRobot logger_robot;
    assert(logger_robot.Boot() == solar::Status::Ok);

    assert((TestLogger::try_debug<ControlLogSource, solar::Name<"loop">>("debug filtered from usb")) == solar::Status::Ok);
    assert((ControlLog::try_info_id<solar::Name<"loop">>(
               SOLAR_LOG_ID(),
               "control awake tick=%d volts=%.1f",
               7,
               12.5)) == solar::Status::Ok);
    assert((ControlLog::try_error<solar::Name<"fault">>("fault latched code=%u", 42U)) == solar::Status::Ok);

    char serial_out[512]{};
    const auto serial_count = low_level::serial::read_tx<UsbSerial>(
        reinterpret_cast<std::uint8_t *>(serial_out),
        sizeof(serial_out) - 1U);
    serial_out[serial_count] = '\0';
    assert(std::strstr(serial_out, "[INFO] control/loop: control awake tick=7 volts=12.5") != nullptr);
    assert(std::strstr(serial_out, "[ERROR] control/fault: fault latched code=42") != nullptr);
    assert(std::strstr(serial_out, "debug filtered") == nullptr);

    char memory_out[512]{};
    auto &memory_sink = TestLogger::sink<MemoryLogSink>();
    const auto memory_count = memory_sink.writer().read(memory_out, sizeof(memory_out) - 1U);
    memory_out[memory_count] = '\0';
    assert(std::strstr(memory_out, "\"level\":\"DEBUG\"") != nullptr);
    assert(std::strstr(memory_out, "\"message\":\"debug filtered from usb\"") != nullptr);
    assert(TestLogger::stats().emitted == 3);
    const auto stats = logger_robot.template Facility<solar::facilities::Inspection>().template logger_stats<TestLogger>();
    assert(stats.emitted == 3);

    TestEvents::reset();
    assert(TestEvents::init() == solar::Status::Ok);
    assert((TestEvents::try_emit<DebugTraceEvent, solar::Name<"control">>(1, 2)) == solar::Status::Ok);
    TestEvents::emit<FacilityReadyEvent, solar::Name<"control">>(7, 9);
    TestEvents::emit<FaultEvent, solar::Name<"control">>(-3, 99);
    assert(TestEvents::count() == 2);
    assert(TestEvents::stats().emitted == 3);
    assert(TestEvents::stats().dropped == 1);

    solar::events::Record event_records[2]{};
    const auto event_count = TestEvents::read(event_records, 2);
    assert(event_count == 2);
    assert(event_records[0].id == FacilityReadyEvent::id);
    assert(event_records[0].value == 7);
    assert(event_records[0].detail == 9);
    assert(event_records[1].id == FaultEvent::id);
    assert(event_records[1].severity == solar::events::Severity::Error);
    const auto latest_event = TestEvents::latest();
    assert(latest_event);
    assert(latest_event.value().id == FaultEvent::id);

    char events_out[512]{};
    auto &event_sink = TestEvents::sink<EventMemorySink>();
    const auto events_count = event_sink.writer().read(events_out, sizeof(events_out) - 1U);
    events_out[events_count] = '\0';
    assert(std::strstr(events_out, "debug.trace") == nullptr);
    assert(std::strstr(events_out, "[INFO] control/facility.ready value=7 detail=9") != nullptr);
    assert(std::strstr(events_out, "[ERROR] control/fault.latched value=-3 detail=99") != nullptr);

    using MetricStore = solar::facilities::Metrics;
    using MetricCatalog = solar::metrics::List<
        ControlLoopCounter,
        BatteryVoltage,
        ControlXError::Raw,
        ControlXError::Mean,
        ControlXError::Maximum,
        ControlLoopTimer>;
    MetricStore::reset_catalog<MetricCatalog>();
    MetricStore::inc<ControlLoopCounter>();
    MetricStore::add<ControlLoopCounter>(4);
    MetricStore::set<BatteryVoltage>(12.25F);
    using BoundControlXError = solar::metrics::BoundGroup<MetricStore, ControlXError>;
    BoundControlXError::observe(1.0F);
    BoundControlXError::observe(3.0F);
    BoundControlXError::observe(5.0F);
    MetricStore::record<ControlLoopTimer>(solar::Microseconds{1000});
    MetricStore::record<ControlLoopTimer>(solar::Microseconds{3000});

    const auto loop_count = MetricStore::get<ControlLoopCounter>();
    assert(loop_count);
    assert(loop_count.value() == 5U);
    const auto voltage = MetricStore::get<BatteryVoltage>();
    assert(voltage);
    assert(voltage.value() == 12.25F);
    const auto error_raw = MetricStore::get<ControlXError::Raw>();
    assert(error_raw);
    assert(error_raw.value() == 5.0F);
    const auto error_mean = MetricStore::get<ControlXError::Mean>();
    assert(error_mean);
    assert(error_mean.value() == 3.0);
    const auto error_max = MetricStore::get<ControlXError::Maximum>();
    assert(error_max);
    assert(error_max.value() == 5.0F);
    const auto loop_time = MetricStore::get<ControlLoopTimer>();
    assert(loop_time);
    assert(loop_time.value() == 2000.0);

    solar::metrics::Snapshot metric_snapshots[6]{};
    const auto metric_snapshot_count = MetricStore::snapshots<MetricCatalog>(metric_snapshots, 6);
    assert(metric_snapshot_count == 6);
    assert(metric_snapshots[0].id == ControlLoopCounter::id);
    assert(metric_snapshots[0].value.u64 == 5U);
    assert(metric_snapshots[1].unit == Volts::Name::c_str());
    assert(metric_snapshots[1].value.as_f64() == 12.25);
    assert(metric_snapshots[3].value.as_f64() == 3.0);
    assert(metric_snapshots[5].unit == MicrosecondsUnit::Name::c_str());
    assert(metric_snapshots[5].value.as_f64() == 2000.0);

    std::uint8_t remote_frame_out[128]{};
    std::uint8_t remote_payload_out[32]{};
    std::size_t remote_frame_size = 0;
    constexpr std::uint8_t remote_payload[] = {1, 2, 0, 3};
    solar::remote::Frame remote_frame{};
    remote_frame.kind = solar::remote::FrameKind::Request;
    remote_frame.sequence = 11;
    remote_frame.correlation = 11;
    remote_frame.target_id = solar::remote::generated::CoreMethods[1].id;
    remote_frame.payload = remote_payload;
    remote_frame.payload_size = sizeof(remote_payload);
    assert(solar::remote::encode_frame(remote_frame, remote_frame_out, sizeof(remote_frame_out), remote_frame_size));

    solar::remote::Frame decoded_remote_frame{};
    assert(solar::remote::decode_frame(remote_frame_out, remote_frame_size, remote_payload_out, sizeof(remote_payload_out), decoded_remote_frame));
    assert(decoded_remote_frame.kind == solar::remote::FrameKind::Request);
    assert(decoded_remote_frame.sequence == 11);
    assert(decoded_remote_frame.target_id == solar::remote::generated::CoreMethods[1].id);
    assert(decoded_remote_frame.payload_size == sizeof(remote_payload));
    assert(remote_payload_out[2] == 0);

    RemoteCatalogRobot remote_catalog_robot;
    assert(remote_catalog_robot.Boot() == solar::Status::Ok);
    solar::remote::Frame hello_request{};
    hello_request.kind = solar::remote::FrameKind::Hello;
    hello_request.sequence = 21;
    hello_request.correlation = 21;
    hello_request.target_id = solar::remote::Id<"solar.hello">::value;

    std::uint8_t hello_frame[128]{};
    std::size_t hello_frame_size = 0;
    assert(solar::remote::encode_frame(hello_request, hello_frame, sizeof(hello_frame), hello_frame_size));
    assert(low_level::serial::inject_rx<StaticRemoteTransport::Port>(hello_frame, hello_frame_size) == hello_frame_size);
    for (int i = 0; i < 20 && low_level::serial::tx_size<StaticRemoteTransport::Port>() == 0; ++i)
    {
        solar::rtos::ThisThread::sleep_for(solar::Milliseconds{1});
    }

    std::uint8_t hello_tx[256]{};
    const auto hello_tx_size = low_level::serial::read_tx<StaticRemoteTransport::Port>(hello_tx, sizeof(hello_tx));
    solar::remote::Frame hello_response_frame{};
    std::uint8_t hello_payload[64]{};
    assert(solar::remote::decode_frame(hello_tx, hello_tx_size, hello_payload, sizeof(hello_payload), hello_response_frame));
    assert(hello_response_frame.kind == solar::remote::FrameKind::HelloAck);
    solar::remote::generated::HelloResponse hello_response{};
    assert(solar::remote::decode(hello_response_frame.payload, hello_response_frame.payload_size, hello_response));
    assert(hello_response.method_count == 9);

    return 0;
}
