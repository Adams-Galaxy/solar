#include <atomic>
#include <cstdint>

#include <zephyr/ztest.h>

#include <solar/remote/adapters.hpp>
#include <solar/remote/testing/in_memory_link.hpp>
#include <solar/solar.hpp>

namespace fixture
{

struct Gain
{
    using Value = float;
    static constexpr solar::parameters::Descriptor descriptor{
        .name = "fixture.gain",
        .description = "Control gain",
    };
    static constexpr Value default_value = 1.0F;
    using Access = solar::parameters::ReadWrite;
    using External = solar::parameters::ExternallyWritable<>;
};

struct Frames
{
    using Value = std::uint32_t;
    using Instrument = solar::metrics::Counter;
    using Unit = solar::metrics::units::Frames;
    static constexpr solar::metrics::Descriptor descriptor{
        .name = "fixture.frames",
        .description = "Processed frame count",
    };
};

struct Command
{
    std::int32_t value{};
    static constexpr solar::bus::Descriptor descriptor{.name = "fixture.command"};
};

} // namespace fixture

template <> struct solar::remote::Schema<fixture::Command>
{
    static constexpr SchemaDescriptor descriptor{
        .id = TypeId{0xA204},
        .name = "fixture.Command",
    };
    using Fields = remote::Fields<Field<1, &fixture::Command::value>>;
    static constexpr std::size_t max_encoded_size = 12;
    static constexpr Codec codec = Codec::Cbor;
};

namespace fixture
{

struct Pulse
{
    using Payload = void;
    static constexpr solar::events::Descriptor descriptor{.name = "fixture.pulse"};
};

struct CommandEvent
{
    using Payload = Command;
    static constexpr solar::events::Descriptor descriptor{.name = "fixture.command-event"};
};

using EventBridge =
    solar::remote::adapters::EventTopic<CommandEvent, solar::remote::TopicId{0xA301}, 4>;
using LogBridge = solar::remote::adapters::LogTopic<64, 4, solar::remote::TopicId{0xA302},
                                                    solar::remote::TypeId{0xA303}>;

inline std::atomic_uint job_runs{};

struct JobBehavior
{
    static void execute() noexcept
    {
        job_runs.fetch_add(1, std::memory_order_release);
    }
};

using Job = solar::execution::OnDemand<"fixture-adapter-job", JobBehavior,
                                       solar::execution::SystemWorkQueue>;

using GainRemote = solar::remote::adapters::Parameter<Gain, solar::remote::DataId{0xA101},
                                                      solar::remote::TypeId{0xA201},
                                                      solar::remote::adapters::ReadWrite>;
using FramesRemote = solar::remote::adapters::Metric<Frames, solar::metrics::view::Value,
                                                     solar::remote::DataId{0xA102},
                                                     solar::remote::TypeId{0xA202}, 10>;
using LifecycleRemote = solar::remote::adapters::LifecycleState<solar::remote::DataId{0xA103},
                                                                solar::remote::TypeId{0xA203}>;
using CommandRemote = solar::remote::adapters::BusInput<Command, solar::remote::DataId{0xA104}>;
using EventRemote = solar::remote::adapters::EventStats<Pulse, solar::remote::DataId{0xA105},
                                                        solar::remote::TypeId{0xA205}>;
using LogRemote =
    solar::remote::adapters::LogStats<solar::remote::DataId{0xA106}, solar::remote::TypeId{0xA206}>;
using ExecutionRemote = solar::remote::adapters::ExecutionStats<Job, solar::remote::DataId{0xA107},
                                                                solar::remote::TypeId{0xA207}>;

struct DebugLink : solar::remote::testing::InMemoryLink<DebugLink, 256, 256>
{
    static constexpr solar::remote::LinkDescriptor descriptor{
        .id = solar::remote::LinkId{0xA001},
        .name = "fixture.adapters.link",
    };
    using Grants = solar::remote::Requires<solar::remote::permission::Observe,
                                           solar::remote::permission::Configure>;
};

struct Owner
{
    static constexpr solar::component::Descriptor descriptor{.name = "fixture.adapters.owner"};
    using Parameters = solar::parameters::Contribute<Gain>;
    using Metrics = solar::metrics::Contribute<Frames>;
    using Events = solar::events::Contribute<Pulse, CommandEvent>;
    using Messages = solar::bus::Messages<Command>;
    using Tasks = solar::execution::Tasks<Job>;
    using RemoteData =
        solar::remote::ContributeData<GainRemote, FramesRemote, LifecycleRemote, CommandRemote,
                                      EventRemote, LogRemote, ExecutionRemote>;
    using RemoteLinks = solar::remote::ContributeLinks<DebugLink>;
};

inline std::atomic_int command_value{};

struct Consumer
{
    static constexpr solar::component::Descriptor descriptor{.name = "fixture.adapters.consumer"};
    using Subscriptions =
        solar::bus::Subscriptions<solar::bus::On<Command, solar::bus::delivery::Inline>>;

    static void handle(const Command& command) noexcept
    {
        command_value.store(command.value, std::memory_order_release);
    }
};

using ComponentRemote =
    solar::remote::adapters::ComponentStats<Owner, solar::remote::DataId{0xA108},
                                            solar::remote::TypeId{0xA208}>;
using GraphRemote = solar::remote::adapters::GraphStats<solar::remote::DataId{0xA109},
                                                        solar::remote::TypeId{0xA209}>;

struct InspectionExposure
{
    static constexpr solar::component::Descriptor descriptor{
        .name = "fixture.adapters.inspection",
    };
    using RemoteData = solar::remote::ContributeData<ComponentRemote, GraphRemote>;
};

using System = solar::System<
    solar::Blueprint<solar::Facilities<Owner, Consumer, InspectionExposure, EventBridge, LogBridge>,
                     solar::log::Configuration<solar::log::Sinks<solar::log::To<
                         LogBridge::Sink, solar::log::MinimumLevel<solar::log::Level::Notice>>>>>>;

} // namespace fixture

SOLAR_BIND_SYSTEM(fixture::System);

ZTEST(remote_adapters, test_adapters_use_canonical_subsystem_state)
{
    static_assert(fixture::System::RemoteDataCatalog::contains<fixture::GainRemote>);
    static_assert(fixture::System::RemoteDataCatalog::contains<fixture::FramesRemote>);
    static_assert(fixture::System::RemoteDataCatalog::contains<fixture::LifecycleRemote>);
    static_assert(fixture::System::RemoteDataCatalog::contains<fixture::CommandRemote>);
    static_assert(fixture::System::RemoteDataCatalog::contains<fixture::EventRemote>);
    static_assert(fixture::System::RemoteDataCatalog::contains<fixture::LogRemote>);
    static_assert(fixture::System::RemoteDataCatalog::contains<fixture::ExecutionRemote>);
    static_assert(fixture::System::RemoteDataCatalog::contains<fixture::ComponentRemote>);
    static_assert(fixture::System::RemoteDataCatalog::contains<fixture::GraphRemote>);
    static_assert(fixture::System::RemoteTopicCatalog::contains<fixture::EventBridge::Topic>);
    static_assert(fixture::System::RemoteTopicCatalog::contains<fixture::LogBridge::Topic>);

    zassert_true(fixture::System::boot().has_value());

    auto gain = fixture::GainRemote::read();
    zassert_true(gain.has_value());
    zassert_equal(gain->value, 1.0F);
    zassert_true(fixture::GainRemote::write({.value = 2.5F}).has_value());
    auto canonical_gain = solar::parameters::get<fixture::Gain>();
    zassert_true(canonical_gain.has_value());
    zassert_equal(*canonical_gain, 2.5F);

    zassert_true(solar::metrics::add<fixture::Frames>(7).has_value());
    auto frames = fixture::FramesRemote::read();
    zassert_true(frames.has_value());
    zassert_equal(frames->value, 7);

    const auto lifecycle = fixture::LifecycleRemote::read();
    zassert_equal(lifecycle.value, solar::lifecycle::SystemState::Running);

    zassert_true(fixture::CommandRemote::write({.value = 42}).has_value());
    zassert_equal(fixture::command_value.load(std::memory_order_acquire), 42);

    zassert_true(solar::events::observe<fixture::Pulse>().has_value());
    auto event = fixture::EventRemote::read();
    zassert_true(event.has_value());
    zassert_equal(event->attempts, 1);
    zassert_equal(event->captured, 1);

    zassert_true(solar::events::observe<fixture::CommandEvent>({.value = 73}).has_value());
    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto record = solar::events::record<fixture::CommandEvent>();
        if (record && record->retained == 0) {
            break;
        }
        k_sleep(K_MSEC(1));
    }
    auto bridged_event = solar::events::record<fixture::CommandEvent>();
    zassert_true(bridged_event.has_value());
    zassert_equal(bridged_event->processor_failures, 0);

    zassert_true(solar::log::notice<fixture::Owner>("remote adapter {}", 19).has_value());
    zassert_true(solar::log::flush().has_value());
    auto bridge_sink = solar::log::sink_record<fixture::LogBridge::Sink>();
    zassert_true(bridge_sink.has_value());
    zassert_true(bridge_sink->accepted > 0);

    zassert_true(solar::execution::submit<fixture::Job>().has_value());
    for (int attempt = 0; attempt < 100 && fixture::job_runs.load() == 0; ++attempt) {
        k_sleep(K_MSEC(1));
    }
    auto execution = fixture::ExecutionRemote::read();
    zassert_true(execution.has_value());
    zassert_equal(execution->submissions, 1);
    zassert_equal(execution->completed, 1);

    const auto logging = fixture::LogRemote::read();
    zassert_false(logging.panic);
    auto component = fixture::ComponentRemote::read();
    zassert_true(component.has_value());
    zassert_equal(component->state, solar::lifecycle::ComponentState::Running);
    zassert_true(component->execution_contained);
    const auto graph = fixture::GraphRemote::read();
    zassert_equal(graph.devices, 0);
    zassert_true(graph.facilities >= 3);
    zassert_true(graph.services >= 1);

    const auto service_record = solar::remote::records::service();
    zassert_true(service_record.ready);
    zassert_true(service_record.accepting);
    const auto links = solar::remote::records::links();
    static_assert(links.size() == 1);
    zassert_equal(links[0].id, fixture::DebugLink::descriptor.id);

    zassert_true(fixture::System::stop().has_value());
}

ZTEST_SUITE(remote_adapters, nullptr, nullptr, nullptr, nullptr, nullptr);
