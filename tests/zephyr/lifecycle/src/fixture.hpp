#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <type_traits>

#include <solar/solar.hpp>

namespace lifecycle_fixture
{

template <typename Tag, std::size_t Capacity = 64> struct Trace
{
    inline static std::array<int, Capacity> values{};
    inline static std::size_t count{};

    static void reset() noexcept
    {
        count = 0;
        values.fill(0);
    }

    static void push(int value) noexcept
    {
        if (count < Capacity) {
            values[count++] = value;
        }
    }
};

struct SuccessTraceTag;
using SuccessTrace = Trace<SuccessTraceTag>;

struct SuccessCore
{
    static constexpr solar::component::Descriptor descriptor{.name = "success.core"};
    static solar::Status init() noexcept;
    static solar::Status start() noexcept;
    static solar::Status stop() noexcept;
    static solar::Status deinit() noexcept;
};

struct SuccessSensor
{
    static constexpr solar::component::Descriptor descriptor{.name = "success.sensor"};
    using Dependencies = solar::Dependencies<SuccessCore>;
    static solar::Result<void> init() noexcept;
    static solar::Result<void> start() noexcept;
    static solar::Result<void> stop() noexcept;
    static solar::Result<void> deinit() noexcept;
};

struct SuccessPassive
{
    static constexpr solar::component::Descriptor descriptor{.name = "success.passive"};
};

struct SuccessService
{
    static constexpr solar::component::Descriptor descriptor{.name = "success.service"};
    using Dependencies = solar::Dependencies<SuccessSensor>;
    static solar::Status init() noexcept;
    static solar::Status start() noexcept;
    static solar::Status stop() noexcept;
    static solar::Status deinit() noexcept;
};

struct SuccessExecutor
    : solar::execution::WorkQueue<"success.executor", solar::execution::StackSize<1024>>
{
    static constexpr solar::component::Descriptor descriptor{.name = "success.executor"};
    using Dependencies = solar::Dependencies<SuccessService>;
    static solar::Status init() noexcept;
    static solar::Status start() noexcept;
    static solar::Status stop() noexcept;
    static solar::Status deinit() noexcept;
};

using SuccessSystem = solar::System<
    solar::Blueprint<solar::Devices<SuccessSensor>, solar::Facilities<SuccessCore, SuccessPassive>,
                     solar::Services<SuccessService>, solar::Executors<SuccessExecutor>>>;

using ExpectedSuccessOrder =
    solar::TypeList<SuccessCore, SuccessSensor, SuccessPassive, SuccessService, SuccessExecutor>;
using ExpectedSuccessReverse =
    solar::TypeList<SuccessExecutor, SuccessService, SuccessPassive, SuccessSensor, SuccessCore>;
static_assert(
    std::is_same_v<typename SuccessSystem::Graph::TopologicalOrder, ExpectedSuccessOrder>);
static_assert(
    std::is_same_v<typename SuccessSystem::Graph::ReverseTopologicalOrder, ExpectedSuccessReverse>);

inline std::atomic_bool activation_committed{};
inline std::atomic_bool execution_released{};
inline std::atomic_bool transition_states_observed{};

struct InitFailureRoot
{
    static constexpr solar::component::Descriptor descriptor{.name = "init_failure.root"};
    static solar::Status init() noexcept;
    static solar::Status deinit() noexcept;
};

struct InitFailureComponent
{
    static constexpr solar::component::Descriptor descriptor{.name = "init_failure.failure"};
    using Dependencies = solar::Dependencies<InitFailureRoot>;
    static solar::Status init() noexcept;
    static solar::Status deinit() noexcept;
};

struct InitFailureSkipped
{
    static constexpr solar::component::Descriptor descriptor{.name = "init_failure.skipped"};
    static solar::Status init() noexcept;
};

struct InitFailureTraceTag;
using InitFailureTrace = Trace<InitFailureTraceTag>;
using InitFailureSystem =
    solar::System<solar::Blueprint<solar::Devices<InitFailureComponent>,
                                   solar::Facilities<InitFailureRoot, InitFailureSkipped>>>;
struct InitFailureApplication;

struct StartFailureRoot
{
    static constexpr solar::component::Descriptor descriptor{.name = "start_failure.root"};
    static solar::Status init() noexcept;
    static solar::Status start() noexcept;
    static solar::Status stop() noexcept;
    static solar::Status deinit() noexcept;
};

struct StartFailureComponent
{
    static constexpr solar::component::Descriptor descriptor{.name = "start_failure.failure"};
    using Dependencies = solar::Dependencies<StartFailureRoot>;
    static solar::Status init() noexcept;
    static solar::Status start() noexcept;
    static solar::Status stop() noexcept;
    static solar::Status deinit() noexcept;
};

struct StartFailureTraceTag;
using StartFailureTrace = Trace<StartFailureTraceTag>;
using StartFailureSystem = solar::System<
    solar::Blueprint<solar::Devices<StartFailureComponent>, solar::Facilities<StartFailureRoot>>>;
struct StartFailureApplication;

struct StopFailureComponent
{
    static constexpr solar::component::Descriptor descriptor{.name = "stop_failure.component"};
    inline static bool deinit_called{};
    static solar::Status init() noexcept
    {
        return solar::Status::Ok;
    }
    static solar::Status start() noexcept
    {
        return solar::Status::Ok;
    }
    static solar::Status stop() noexcept
    {
        return solar::Status::Error;
    }
    static solar::Status deinit() noexcept
    {
        deinit_called = true;
        return solar::Status::Ok;
    }
};

using StopFailureSystem = solar::System<solar::Blueprint<solar::Facilities<StopFailureComponent>>>;
struct StopFailureApplication;

struct PrepareFailureService
{
    static constexpr solar::component::Descriptor descriptor{.name = "execution.prepare_failure"};
    inline static bool deinit_called{};
    static solar::Status init() noexcept
    {
        return solar::Status::Ok;
    }
    static solar::Status start() noexcept
    {
        return solar::Status::Ok;
    }
    static solar::Status stop() noexcept
    {
        return solar::Status::Ok;
    }
    static solar::Status deinit() noexcept
    {
        deinit_called = true;
        return solar::Status::Ok;
    }
};

using PrepareFailureSystem =
    solar::System<solar::Blueprint<solar::Services<PrepareFailureService>>>;
struct PrepareFailureApplication;

struct ValidateFailureService
{
    static constexpr solar::component::Descriptor descriptor{.name = "execution.validate_failure"};
    inline static bool stop_called{};
    inline static bool deinit_called{};
    static solar::Status init() noexcept
    {
        return solar::Status::Ok;
    }
    static solar::Status start() noexcept
    {
        return solar::Status::Ok;
    }
    static solar::Status stop() noexcept
    {
        stop_called = true;
        return solar::Status::Ok;
    }
    static solar::Status deinit() noexcept
    {
        deinit_called = true;
        return solar::Status::Ok;
    }
};

using ValidateFailureSystem =
    solar::System<solar::Blueprint<solar::Services<ValidateFailureService>>>;
struct ValidateFailureApplication;

struct PreservedResource
{
    static constexpr solar::component::Descriptor descriptor{.name = "uncontained.resource"};
    inline static bool stop_called{};
    inline static bool deinit_called{};
    static solar::Status init() noexcept
    {
        return solar::Status::Ok;
    }
    static solar::Status start() noexcept
    {
        return solar::Status::Ok;
    }
    static solar::Status stop() noexcept
    {
        stop_called = true;
        return solar::Status::Ok;
    }
    static solar::Status deinit() noexcept
    {
        deinit_called = true;
        return solar::Status::Ok;
    }
};

struct IndependentResource
{
    static constexpr solar::component::Descriptor descriptor{.name = "uncontained.independent"};
    inline static bool stop_called{};
    inline static bool deinit_called{};
    static solar::Status init() noexcept
    {
        return solar::Status::Ok;
    }
    static solar::Status start() noexcept
    {
        return solar::Status::Ok;
    }
    static solar::Status stop() noexcept
    {
        stop_called = true;
        return solar::Status::Ok;
    }
    static solar::Status deinit() noexcept
    {
        deinit_called = true;
        return solar::Status::Ok;
    }
};

struct UncontainedService
{
    static constexpr solar::component::Descriptor descriptor{.name = "uncontained.service"};
    using Dependencies = solar::Dependencies<PreservedResource>;
};

using UncontainedSystem =
    solar::System<solar::Blueprint<solar::Devices<PreservedResource, IndependentResource>,
                                   solar::Services<UncontainedService>>>;
struct UncontainedApplication;

struct ForcedService
{
    static constexpr solar::component::Descriptor descriptor{.name = "forced.service"};
    inline static bool stop_called{};
    inline static bool deinit_called{};
    static solar::Status init() noexcept
    {
        return solar::Status::Ok;
    }
    static solar::Status start() noexcept
    {
        return solar::Status::Ok;
    }
    static solar::Status stop() noexcept
    {
        stop_called = true;
        return solar::Status::Ok;
    }
    static solar::Status deinit() noexcept
    {
        deinit_called = true;
        return solar::Status::Ok;
    }
};

using ForcedSystem = solar::System<solar::Blueprint<solar::Services<ForcedService>>>;
struct ForcedApplication;

struct CapacityOne
{
    static constexpr solar::component::Descriptor descriptor{.name = "capacity.one"};
    static solar::Status init() noexcept
    {
        return solar::Status::Ok;
    }
    static solar::Status deinit() noexcept
    {
        return solar::Status::Error;
    }
};

struct CapacityTwo
{
    static constexpr solar::component::Descriptor descriptor{.name = "capacity.two"};
    static solar::Status init() noexcept
    {
        return solar::Status::Ok;
    }
    static solar::Status deinit() noexcept
    {
        return solar::Status::Error;
    }
};

struct CapacityThree
{
    static constexpr solar::component::Descriptor descriptor{.name = "capacity.three"};
    static solar::Status init() noexcept
    {
        return solar::Status::Ok;
    }
    static solar::Status deinit() noexcept
    {
        return solar::Status::Error;
    }
};

struct CapacityStartFailure
{
    static constexpr solar::component::Descriptor descriptor{.name = "capacity.start_failure"};
    static solar::Status init() noexcept
    {
        return solar::Status::Ok;
    }
    static solar::Status start() noexcept
    {
        return solar::Status::Invalid;
    }
};

using CapacitySystem = solar::System<solar::Blueprint<
    solar::Facilities<CapacityOne, CapacityTwo, CapacityThree, CapacityStartFailure>>>;
struct CapacityApplication;

struct ConcurrentComponent
{
    static constexpr solar::component::Descriptor descriptor{.name = "concurrent.component"};
    inline static solar::kernel::Semaphore entered{};
    inline static solar::kernel::Semaphore release{};
    static solar::Status init() noexcept
    {
        entered.give();
        return release.take(solar::kernel::Timeout::after(std::chrono::milliseconds{500}));
    }
};

using ConcurrentSystem = solar::System<solar::Blueprint<solar::Facilities<ConcurrentComponent>>>;
struct ConcurrentApplication;
inline std::atomic_bool concurrent_boot_succeeded{};

using EmptySystem = solar::System<solar::Blueprint<>>;
struct EmptyApplication;

} // namespace lifecycle_fixture

SOLAR_BIND_SYSTEM(lifecycle_fixture::SuccessSystem);
SOLAR_BIND_SYSTEM_FOR(lifecycle_fixture::InitFailureApplication,
                      lifecycle_fixture::InitFailureSystem);
SOLAR_BIND_SYSTEM_FOR(lifecycle_fixture::StartFailureApplication,
                      lifecycle_fixture::StartFailureSystem);
SOLAR_BIND_SYSTEM_FOR(lifecycle_fixture::StopFailureApplication,
                      lifecycle_fixture::StopFailureSystem);
SOLAR_BIND_SYSTEM_FOR(lifecycle_fixture::PrepareFailureApplication,
                      lifecycle_fixture::PrepareFailureSystem);
SOLAR_BIND_SYSTEM_FOR(lifecycle_fixture::ValidateFailureApplication,
                      lifecycle_fixture::ValidateFailureSystem);
SOLAR_BIND_SYSTEM_FOR(lifecycle_fixture::UncontainedApplication,
                      lifecycle_fixture::UncontainedSystem);
SOLAR_BIND_SYSTEM_FOR(lifecycle_fixture::ForcedApplication, lifecycle_fixture::ForcedSystem);
SOLAR_BIND_SYSTEM_FOR(lifecycle_fixture::CapacityApplication, lifecycle_fixture::CapacitySystem);
SOLAR_BIND_SYSTEM_FOR(lifecycle_fixture::ConcurrentApplication,
                      lifecycle_fixture::ConcurrentSystem);
SOLAR_BIND_SYSTEM_FOR(lifecycle_fixture::EmptyApplication, lifecycle_fixture::EmptySystem);

template <>
struct solar::lifecycle::ExecutionProtocol<lifecycle_fixture::SuccessSystem,
                                           lifecycle_fixture::SuccessService>
{
    static constexpr bool participates = true;

    static solar::Result<void> prepare() noexcept
    {
        lifecycle_fixture::SuccessTrace::push(32);
        return {};
    }

    static solar::Result<void> validate_activation() noexcept
    {
        lifecycle_fixture::SuccessTrace::push(33);
        return {};
    }

    static void activate() noexcept
    {
        const auto records =
            solar::lifecycle::Engine<lifecycle_fixture::SuccessSystem>::components();
        bool all_running = records.has_value();
        if (records) {
            for (const auto& record : *records) {
                all_running =
                    all_running && record.state == solar::lifecycle::ComponentState::Running;
            }
        }
        lifecycle_fixture::activation_committed.store(
            all_running && solar::lifecycle::Engine<lifecycle_fixture::SuccessSystem>::state() ==
                               solar::lifecycle::SystemState::Running,
            std::memory_order_release);
        lifecycle_fixture::execution_released.store(true, std::memory_order_release);
        lifecycle_fixture::SuccessTrace::push(34);
    }

    static solar::Status request_stop() noexcept
    {
        lifecycle_fixture::SuccessTrace::push(35);
        return solar::Status::Ok;
    }

    static solar::lifecycle::Containment contain() noexcept
    {
        lifecycle_fixture::SuccessTrace::push(36);
        return {};
    }
};

template <>
struct solar::lifecycle::ExecutionProtocol<lifecycle_fixture::PrepareFailureSystem,
                                           lifecycle_fixture::PrepareFailureService>
{
    static constexpr bool participates = true;
    static solar::Result<void> prepare() noexcept
    {
        return solar::fail(solar::Status::NoMemory);
    }
    static solar::Result<void> validate_activation() noexcept
    {
        return {};
    }
    static void activate() noexcept {}
    static solar::Status request_stop() noexcept
    {
        return solar::Status::Ok;
    }
    static solar::lifecycle::Containment contain() noexcept
    {
        return {};
    }
};

template <>
struct solar::lifecycle::ExecutionProtocol<lifecycle_fixture::ValidateFailureSystem,
                                           lifecycle_fixture::ValidateFailureService>
{
    static constexpr bool participates = true;
    static solar::Result<void> prepare() noexcept
    {
        return {};
    }
    static solar::Result<void> validate_activation() noexcept
    {
        return solar::fail(solar::Status::Invalid);
    }
    static void activate() noexcept {}
    static solar::Status request_stop() noexcept
    {
        return solar::Status::Ok;
    }
    static solar::lifecycle::Containment contain() noexcept
    {
        return {};
    }
};

template <>
struct solar::lifecycle::ExecutionProtocol<lifecycle_fixture::UncontainedSystem,
                                           lifecycle_fixture::UncontainedService>
{
    static constexpr bool participates = true;
    static solar::Result<void> prepare() noexcept
    {
        return {};
    }
    static solar::Result<void> validate_activation() noexcept
    {
        return {};
    }
    static void activate() noexcept {}
    static solar::Status request_stop() noexcept
    {
        return solar::Status::Ok;
    }
    static solar::lifecycle::Containment contain() noexcept
    {
        return {.status = solar::Status::Timeout, .contained = false, .timed_out = true};
    }
};

template <>
struct solar::lifecycle::ExecutionProtocol<lifecycle_fixture::ForcedSystem,
                                           lifecycle_fixture::ForcedService>
{
    static constexpr bool participates = true;
    static solar::Result<void> prepare() noexcept
    {
        return {};
    }
    static solar::Result<void> validate_activation() noexcept
    {
        return {};
    }
    static void activate() noexcept {}
    static solar::Status request_stop() noexcept
    {
        return solar::Status::Ok;
    }
    static solar::lifecycle::Containment contain() noexcept
    {
        return {.status = solar::Status::Ok,
                .contained = true,
                .forced = true,
                .timed_out = true,
                .abort_attempted = true};
    }
};

namespace lifecycle_fixture
{

template <typename Component>
inline void observe_success_transition(solar::lifecycle::SystemState system_state,
                                       solar::lifecycle::ComponentState component_state) noexcept
{
    const auto record = solar::lifecycle::Engine<SuccessSystem>::record<Component>();
    const bool observed = solar::lifecycle::Engine<SuccessSystem>::state() == system_state &&
                          record.has_value() && record->state == component_state;
    if (!observed) {
        transition_states_observed.store(false, std::memory_order_release);
    }
}

inline solar::Status SuccessCore::init() noexcept
{
    observe_success_transition<SuccessCore>(solar::lifecycle::SystemState::Initializing,
                                            solar::lifecycle::ComponentState::Initializing);
    SuccessTrace::push(10);
    return solar::Status::Ok;
}
inline solar::Status SuccessCore::start() noexcept
{
    observe_success_transition<SuccessCore>(solar::lifecycle::SystemState::Starting,
                                            solar::lifecycle::ComponentState::Starting);
    SuccessTrace::push(11);
    return solar::Status::Ok;
}
inline solar::Status SuccessCore::stop() noexcept
{
    SuccessTrace::push(12);
    return solar::Status::Ok;
}
inline solar::Status SuccessCore::deinit() noexcept
{
    SuccessTrace::push(13);
    return solar::Status::Ok;
}

inline solar::Result<void> SuccessSensor::init() noexcept
{
    SuccessTrace::push(20);
    return {};
}
inline solar::Result<void> SuccessSensor::start() noexcept
{
    SuccessTrace::push(21);
    return {};
}
inline solar::Result<void> SuccessSensor::stop() noexcept
{
    SuccessTrace::push(22);
    return {};
}
inline solar::Result<void> SuccessSensor::deinit() noexcept
{
    SuccessTrace::push(23);
    return {};
}

inline solar::Status SuccessService::init() noexcept
{
    SuccessTrace::push(30);
    return solar::Status::Ok;
}
inline solar::Status SuccessService::start() noexcept
{
    SuccessTrace::push(31);
    return solar::Status::Ok;
}
inline solar::Status SuccessService::stop() noexcept
{
    SuccessTrace::push(37);
    return solar::Status::Ok;
}
inline solar::Status SuccessService::deinit() noexcept
{
    SuccessTrace::push(38);
    return solar::Status::Ok;
}

inline solar::Status SuccessExecutor::init() noexcept
{
    SuccessTrace::push(40);
    return solar::Status::Ok;
}
inline solar::Status SuccessExecutor::start() noexcept
{
    SuccessTrace::push(41);
    return solar::Status::Ok;
}
inline solar::Status SuccessExecutor::stop() noexcept
{
    observe_success_transition<SuccessExecutor>(solar::lifecycle::SystemState::Stopping,
                                                solar::lifecycle::ComponentState::Stopping);
    SuccessTrace::push(42);
    return solar::Status::Ok;
}
inline solar::Status SuccessExecutor::deinit() noexcept
{
    observe_success_transition<SuccessExecutor>(solar::lifecycle::SystemState::Deinitializing,
                                                solar::lifecycle::ComponentState::Deinitializing);
    SuccessTrace::push(43);
    return solar::Status::Ok;
}

inline solar::Status InitFailureRoot::init() noexcept
{
    InitFailureTrace::push(1);
    return solar::Status::Ok;
}
inline solar::Status InitFailureRoot::deinit() noexcept
{
    InitFailureTrace::push(3);
    return solar::Status::Ok;
}
inline solar::Status InitFailureComponent::init() noexcept
{
    InitFailureTrace::push(2);
    return solar::Status::NotReady;
}
inline solar::Status InitFailureComponent::deinit() noexcept
{
    InitFailureTrace::push(99);
    return solar::Status::Ok;
}
inline solar::Status InitFailureSkipped::init() noexcept
{
    InitFailureTrace::push(98);
    return solar::Status::Ok;
}

inline solar::Status StartFailureRoot::init() noexcept
{
    StartFailureTrace::push(1);
    return solar::Status::Ok;
}
inline solar::Status StartFailureRoot::start() noexcept
{
    StartFailureTrace::push(3);
    return solar::Status::Ok;
}
inline solar::Status StartFailureRoot::stop() noexcept
{
    StartFailureTrace::push(5);
    return solar::Status::Error;
}
inline solar::Status StartFailureRoot::deinit() noexcept
{
    StartFailureTrace::push(7);
    return solar::Status::Ok;
}
inline solar::Status StartFailureComponent::init() noexcept
{
    StartFailureTrace::push(2);
    return solar::Status::Ok;
}
inline solar::Status StartFailureComponent::start() noexcept
{
    StartFailureTrace::push(4);
    return solar::Status::Invalid;
}
inline solar::Status StartFailureComponent::stop() noexcept
{
    StartFailureTrace::push(97);
    return solar::Status::Ok;
}
inline solar::Status StartFailureComponent::deinit() noexcept
{
    StartFailureTrace::push(6);
    return solar::Status::Ok;
}

inline void concurrent_boot_entry(void*) noexcept
{
    const auto result = solar::boot<ConcurrentApplication>();
    concurrent_boot_succeeded.store(result.has_value(), std::memory_order_release);
}

} // namespace lifecycle_fixture
