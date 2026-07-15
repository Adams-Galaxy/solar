#include <array>
#include <string_view>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "solar/system.hpp"

namespace
{

template <solar::FixedString ComponentName>
struct Passive
{
    using Name = solar::Name<ComponentName>;
};

template <solar::FixedString ComponentName>
struct Successful
{
    using Name = solar::Name<ComponentName>;

    static solar::Status init()
    {
        return solar::Status::Ok;
    }

    static solar::Status start()
    {
        return solar::Status::Ok;
    }
};

template <solar::FixedString ComponentName,
          typename DependencyList = solar::Dependencies<>>
struct FailingInit
{
    using Name = solar::Name<ComponentName>;
    using Dependencies = DependencyList;

    static solar::Status init()
    {
        return solar::Status::NotReady;
    }
};

template <solar::FixedString ComponentName,
          typename DependencyList = solar::Dependencies<>>
struct FailingStart
{
    using Name = solar::Name<ComponentName>;
    using Dependencies = DependencyList;

    static solar::Status init()
    {
        return solar::Status::Ok;
    }

    static solar::Status start()
    {
        return solar::Status::Busy;
    }
};

template <solar::FixedString ComponentName>
struct Service
{
    using Name = solar::Name<ComponentName>;
    using Thread = solar::ServiceSpec<Name, 2048>;

    static solar::Status init()
    {
        return solar::Status::Ok;
    }

    static solar::Status start()
    {
        return solar::Status::Ok;
    }

    static solar::Result<void> run(solar::StopToken stop)
    {
        while (!stop.stop_requested())
        {
            k_sleep(K_MSEC(1));
        }
        return solar::Status::Ok;
    }

    static solar::Status stop()
    {
        return solar::Status::Ok;
    }
};

template <solar::FixedString ComponentName>
struct FailingService
{
    using Name = solar::Name<ComponentName>;
    using Thread = solar::ServiceSpec<Name, 2048>;

    static solar::Status init()
    {
        return solar::Status::NotReady;
    }

    static solar::Result<void> run(solar::StopToken)
    {
        return solar::Status::Ok;
    }
};

using GoodBoard = Successful<"board">;
using GoodPeripheral = Successful<"peripheral">;
using GoodDevice = Successful<"device">;
using PassiveFacility = Passive<"facility">;
using GoodService = Service<"service">;
using PassiveTask = Passive<"task">;
using PassiveChannel = Passive<"channel">;

using GoodSystem = solar::System<
    GoodBoard,
    solar::Peripherals<GoodPeripheral>,
    solar::Devices<GoodDevice>,
    solar::Facilities<PassiveFacility>,
    solar::Services<GoodService>,
    solar::Tasks<PassiveTask>,
    solar::Channels<PassiveChannel>>;

template <typename SystemT>
void expect_failure(solar::BootPhase phase,
                    solar::ComponentKind kind,
                    std::string_view name)
{
    const auto result = SystemT::boot();
    zassert_false(result.has_value());
    zassert_equal(result.status(), solar::Status::NotReady);

    const auto &report = SystemT::boot_report();
    zassert_equal(report.status, solar::Status::NotReady);
    zassert_equal(report.failure.phase, phase);
    zassert_equal(report.failure.operation, solar::LifecycleOperation::Init);
    zassert_equal(report.failure.component.kind, kind);
    zassert_true(report.failure.component.identified());
    zassert_equal(std::string_view{report.failure.component.name}, name);

    const auto state = SystemT::lifecycle::state();
    zassert_true(state.has_value());
    zassert_equal(state.value(), solar::SystemState::Failed);

    const auto records = SystemT::lifecycle::components();
    zassert_true(records.has_value());
    const auto &failed = records.value()[report.failure.component.id.value()];
    zassert_equal(failed.state, solar::LifecycleState::Failed);
    zassert_equal(failed.last_status, solar::Status::NotReady);

    const auto repeated = SystemT::boot();
    zassert_false(repeated.has_value());
    zassert_equal(repeated.status(), solar::Status::Already);
}

using FailBoardSystem = solar::System<
    FailingInit<"bad-board">,
    solar::Peripherals<>,
    solar::Devices<>,
    solar::Facilities<>,
    solar::Services<>>;

using FailPeripheralSystem = solar::System<
    Successful<"p-board">,
    solar::Peripherals<FailingInit<"bad-peripheral">>,
    solar::Devices<>,
    solar::Facilities<>,
    solar::Services<>>;

using FailFacilitySystem = solar::System<
    Successful<"f-board">,
    solar::Peripherals<>,
    solar::Devices<>,
    solar::Facilities<FailingInit<"bad-facility">>,
    solar::Services<>>;

using FailDeviceSystem = solar::System<
    Successful<"d-board">,
    solar::Peripherals<>,
    solar::Devices<FailingInit<"bad-device">>,
    solar::Facilities<>,
    solar::Services<>>;

using FailChannelSystem = solar::System<
    Successful<"c-board">,
    solar::Peripherals<>,
    solar::Devices<>,
    solar::Facilities<>,
    solar::Services<>,
    solar::Tasks<>,
    solar::Channels<FailingInit<"bad-channel">>>;

using FailServiceSystem = solar::System<
    Successful<"s-board">,
    solar::Peripherals<>,
    solar::Devices<>,
    solar::Facilities<>,
    solar::Services<FailingService<"bad-service">>>;

using FailTaskSystem = solar::System<
    Successful<"t-board">,
    solar::Peripherals<>,
    solar::Devices<>,
    solar::Facilities<>,
    solar::Services<>,
    solar::Tasks<FailingInit<"bad-task">>>;

using PartialFacility = Successful<"partial-facility">;
using PartialDevice = FailingInit<"partial-device", solar::Dependencies<PartialFacility>>;
using PartialInitSystem = solar::System<
    Successful<"partial-board">,
    solar::Peripherals<Successful<"partial-peripheral">>,
    solar::Devices<PartialDevice>,
    solar::Facilities<PartialFacility>,
    solar::Services<>,
    solar::Tasks<Passive<"partial-task">>,
    solar::Channels<Passive<"partial-channel">>>;

using StartFacility = Passive<"start-facility">;
using StartDevice = FailingStart<"start-device", solar::Dependencies<StartFacility>>;
using StartFailureSystem = solar::System<
    Successful<"start-board">,
    solar::Peripherals<Successful<"start-peripheral">>,
    solar::Devices<StartDevice>,
    solar::Facilities<StartFacility>,
    solar::Services<>,
    solar::Tasks<Passive<"start-task">>,
    solar::Channels<Passive<"start-channel">>>;

} // namespace

ZTEST(solar_system_lifecycle, test_successful_boot_tracks_every_category)
{
    const auto result = GoodSystem::boot();
    zassert_true(result.has_value());
    zassert_true(result.value().ok());
    zassert_equal(result.value().completed_operations, GoodSystem::ComponentCount * 2U);

    const auto system_state = GoodSystem::lifecycle::state();
    zassert_true(system_state.has_value());
    zassert_equal(system_state.value(), solar::SystemState::Running);

    const auto records = GoodSystem::lifecycle::components();
    zassert_true(records.has_value());
    zassert_equal(records.value().size(), GoodSystem::ComponentCount);
    for (const auto &record : records.value())
    {
        zassert_equal(record.state, solar::LifecycleState::Running);
        zassert_equal(record.last_status, solar::Status::Ok);
        zassert_equal(record.transition_count, 4U);
    }

    const auto &facility = records.value()[GoodSystem::FacilityOffset];
    zassert_false(facility.hooks.init);
    zassert_false(facility.hooks.start);
    zassert_equal(facility.state, solar::LifecycleState::Running);

    const auto &channel = records.value()[GoodSystem::ChannelOffset];
    zassert_false(channel.hooks.init);
    zassert_false(channel.hooks.start);
    zassert_equal(channel.state, solar::LifecycleState::Running);

    constexpr auto graph = GoodSystem::graph::components();
    static_assert(graph.size() == GoodSystem::ComponentCount);
    static_assert(graph[GoodSystem::BoardOffset].kind == solar::ComponentKind::Board);
    static_assert(GoodSystem::graph::component<PassiveChannel>().kind ==
                  solar::ComponentKind::Channel);
    zassert_equal(std::string_view{graph[GoodSystem::BoardOffset].name}, "board");

    const auto typed_channel = GoodSystem::lifecycle::record<PassiveChannel>();
    zassert_true(typed_channel.has_value());
    zassert_equal(typed_channel.value().state, solar::LifecycleState::Running);

    const auto repeated = GoodSystem::boot();
    zassert_false(repeated.has_value());
    zassert_equal(repeated.status(), solar::Status::Already);
}

ZTEST(solar_system_lifecycle, test_init_failures_identify_every_category)
{
    expect_failure<FailBoardSystem>(solar::BootPhase::BoardInit,
                                    solar::ComponentKind::Board,
                                    "bad-board");
    expect_failure<FailPeripheralSystem>(solar::BootPhase::PeripheralInit,
                                         solar::ComponentKind::Peripheral,
                                         "bad-peripheral");
    expect_failure<FailFacilitySystem>(solar::BootPhase::FacilityInit,
                                       solar::ComponentKind::Facility,
                                       "bad-facility");
    expect_failure<FailDeviceSystem>(solar::BootPhase::DeviceInit,
                                     solar::ComponentKind::Device,
                                     "bad-device");
    expect_failure<FailChannelSystem>(solar::BootPhase::ChannelInit,
                                      solar::ComponentKind::Channel,
                                      "bad-channel");
    expect_failure<FailServiceSystem>(solar::BootPhase::ServiceInit,
                                      solar::ComponentKind::Service,
                                      "bad-service");
    expect_failure<FailTaskSystem>(solar::BootPhase::TaskInit,
                                   solar::ComponentKind::Task,
                                   "bad-task");
}

ZTEST(solar_system_lifecycle, test_partial_init_failure_rolls_back_completed_init)
{
    constexpr auto dependencies = PartialInitSystem::graph::dependencies<PartialDevice>();
    static_assert(dependencies.size() == 1);
    zassert_equal(dependencies[0].id.value(), PartialInitSystem::FacilityOffset);
    zassert_equal(dependencies[0].kind, solar::ComponentKind::Facility);

    const auto result = PartialInitSystem::boot();
    zassert_false(result.has_value());

    const auto records = PartialInitSystem::lifecycle::components();
    zassert_true(records.has_value());
    zassert_equal(records.value()[PartialInitSystem::BoardOffset].state,
                  solar::LifecycleState::Deinitialized);
    zassert_equal(records.value()[PartialInitSystem::PeripheralOffset].state,
                  solar::LifecycleState::Deinitialized);
    zassert_equal(records.value()[PartialInitSystem::FacilityOffset].state,
                  solar::LifecycleState::Deinitialized);
    zassert_equal(records.value()[PartialInitSystem::DeviceOffset].state,
                  solar::LifecycleState::Failed);
    zassert_equal(records.value()[PartialInitSystem::ChannelOffset].state,
                  solar::LifecycleState::Registered);
    zassert_equal(records.value()[PartialInitSystem::TaskOffset].state,
                  solar::LifecycleState::Registered);
}

ZTEST(solar_system_lifecycle, test_start_failure_rolls_back_started_graph)
{
    const auto result = StartFailureSystem::boot();
    zassert_false(result.has_value());
    zassert_equal(result.status(), solar::Status::Busy);
    zassert_equal(StartFailureSystem::boot_report().failure.phase, solar::BootPhase::DeviceStart);
    zassert_equal(StartFailureSystem::boot_report().failure.operation,
                  solar::LifecycleOperation::Start);

    const auto records = StartFailureSystem::lifecycle::components();
    zassert_true(records.has_value());
    zassert_equal(records.value()[StartFailureSystem::BoardOffset].state,
                  solar::LifecycleState::Deinitialized);
    zassert_equal(records.value()[StartFailureSystem::PeripheralOffset].state,
                  solar::LifecycleState::Deinitialized);
    zassert_equal(records.value()[StartFailureSystem::FacilityOffset].state,
                  solar::LifecycleState::Deinitialized);
    zassert_equal(records.value()[StartFailureSystem::DeviceOffset].state,
                  solar::LifecycleState::Failed);
    zassert_true(records.value()[StartFailureSystem::DeviceOffset]
                     .deinitialized_successfully);
    zassert_equal(records.value()[StartFailureSystem::ChannelOffset].state,
                  solar::LifecycleState::Deinitialized);
    zassert_equal(records.value()[StartFailureSystem::TaskOffset].state,
                  solar::LifecycleState::Deinitialized);
}

ZTEST_SUITE(solar_system_lifecycle, nullptr, nullptr, nullptr, nullptr, nullptr);
