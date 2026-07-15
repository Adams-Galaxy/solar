#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>

#include <zephyr/sys/util_macro.h>

#include "solar/contribution.hpp"
#include "solar/core.hpp"
#include "solar/lifecycle/service_execution.hpp"
#include "solar/lifecycle/storage.hpp"
#include "solar/log/logger.hpp"
#include "solar/service.hpp"

namespace solar
{

template <typename LoggerT = log::NullLogger>
struct Logging
{
    using Logger = LoggerT;
};

template <typename LoggingGroup = Logging<>>
struct Runtime;

/**
 * @brief Runtime policy group carried by `System`.
 */
template <typename LoggerT>
struct Runtime<Logging<LoggerT>>
{
    using Logging = solar::Logging<LoggerT>;
    using Logger = LoggerT;
};

namespace detail
{

template <typename Needle, typename... Types>
struct TypeIndex;

template <typename Needle, typename... Tail>
struct TypeIndex<Needle, Needle, Tail...> : std::integral_constant<std::size_t, 0>
{
};

template <typename Needle, typename Head, typename... Tail>
struct TypeIndex<Needle, Head, Tail...>
    : std::integral_constant<std::size_t, 1 + TypeIndex<Needle, Tail...>::value>
{
};

template <typename Needle>
struct TypeIndex<Needle>
{
    static_assert(!std::is_same_v<Needle, Needle>,
                  "Requested component type is not registered in this Solar system");
};

template <typename NameT, typename... Types>
struct TypeWithName;

template <typename NameT, typename Head, typename... Tail>
struct TypeWithName<NameT, Head, Tail...>
{
    using type = std::conditional_t<std::is_same_v<typename Head::Name, NameT>,
                                    Head,
                                    typename TypeWithName<NameT, Tail...>::type>;
};

template <typename NameT>
struct TypeWithName<NameT>
{
    using type = void;
};

template <typename NameT, typename... Types>
using TypeWithNameT = typename TypeWithName<NameT, Types...>::type;

template <typename T>
concept HasInit = requires {
    T::init();
};

template <typename T>
concept HasStart = requires {
    T::start();
};

template <typename T>
concept HasStop = requires {
    T::stop();
};

template <typename T>
concept HasDeinit = requires {
    T::deinit();
};

template <typename T>
concept HasServiceThread = requires {
    typename T::Thread;
    typename T::Thread::Name;
    { T::Thread::stack_bytes } -> std::convertible_to<std::size_t>;
    { T::Thread::priority } -> std::convertible_to<kernel::Priority>;
};

template <typename T>
concept HasRun = requires(StopToken token) {
    T::run(token);
};

template <typename ReturnT>
constexpr Status normalize_status(ReturnT &&value)
{
    using Raw = std::remove_cvref_t<ReturnT>;
    if constexpr (std::is_same_v<Raw, Status>)
    {
        return value;
    }
    else if constexpr (std::is_same_v<Raw, Result<void>>)
    {
        return value.status();
    }
    else if constexpr (std::is_same_v<Raw, bool>)
    {
        return value ? Status::Ok : Status::Error;
    }
    else
    {
        return Status::Ok;
    }
}

template <typename T>
Status call_init()
{
    if constexpr (HasInit<T>)
    {
        if constexpr (std::is_void_v<decltype(T::init())>)
        {
            T::init();
            return Status::Ok;
        }
        else
        {
            return normalize_status(T::init());
        }
    }
    else
    {
        return Status::Ok;
    }
}

template <typename T>
Status call_start()
{
    if constexpr (HasStart<T>)
    {
        if constexpr (std::is_void_v<decltype(T::start())>)
        {
            T::start();
            return Status::Ok;
        }
        else
        {
            return normalize_status(T::start());
        }
    }
    else
    {
        return Status::Ok;
    }
}

template <typename T>
Status call_stop()
{
    if constexpr (HasStop<T>)
    {
        if constexpr (std::is_void_v<decltype(T::stop())>)
        {
            T::stop();
            return Status::Ok;
        }
        else
        {
            return normalize_status(T::stop());
        }
    }
    else
    {
        return Status::Ok;
    }
}

template <typename T>
Status call_deinit()
{
    if constexpr (HasDeinit<T>)
    {
        if constexpr (std::is_void_v<decltype(T::deinit())>)
        {
            T::deinit();
            return Status::Ok;
        }
        else
        {
            return normalize_status(T::deinit());
        }
    }
    else
    {
        return Status::Ok;
    }
}

template <typename ServiceT, typename SystemT>
class ServiceThreadRuntime
{
public:
    static_assert(HasServiceThread<ServiceT>, "Solar services must declare using Thread = solar::ServiceSpec<...>");
    static_assert(HasRun<ServiceT>,
                  "Solar services must implement static run(stop_token); polling is intentionally not a service model");

    using Spec = typename ServiceT::Thread;

    ServiceThreadRuntime()
        : thread_{Spec::Name::c_str(), Spec::priority, static_cast<std::uint32_t>(Spec::stack_bytes), &ServiceThreadRuntime::entry, this}
    {
    }

    Status start(ComponentDescriptor service_descriptor)
    {
        service_id_ = service_descriptor.id;
        reset_record(service_descriptor);

        const Status status = call_start<ServiceT>();
        if (status != Status::Ok)
        {
            return status;
        }

        const Status thread_status = thread_.start(storage_);
        if (thread_status == Status::Ok)
        {
            const kernel::LockGuard lock{record_mutex_};
            record_.thread_created = true;
            record_.running = !record_.exited && thread_.running();
            record_.native_id = thread_.native_handle();
        }
        return thread_status;
    }

    void request_stop()
    {
        const kernel::LockGuard lock{record_mutex_};
        if (!record_.thread_created)
        {
            return;
        }
        record_.stop_requested = true;
        thread_.request_stop();
    }

    Status join()
    {
        {
            const kernel::LockGuard lock{record_mutex_};
            if (!record_.thread_created)
            {
                return Status::Ok;
            }
        }

        const Status status = thread_.join(kernel::Timeout::after(Spec::stop_timeout));
        if (status == Status::Timeout)
        {
            {
                const kernel::LockGuard lock{record_mutex_};
                record_.join_timed_out = true;
            }

            if constexpr (Spec::abort_on_stop_timeout)
            {
                const Status abort_status = thread_.abort();
                const kernel::LockGuard lock{record_mutex_};
                record_.abort_attempted = true;
                record_.abort_status = abort_status;
                record_.aborted = abort_status == Status::Ok;
                record_.running = false;
                if (record_.aborted)
                {
                    record_.exited = true;
                    record_.exit_after_stop_request = true;
                    record_.run_status = Status::Cancelled;
                }
                return abort_status == Status::Ok ? Status::Timeout : abort_status;
            }
        }

        if (status == Status::Ok)
        {
            const kernel::LockGuard lock{record_mutex_};
            record_.running = false;
        }
        return status;
    }

    ServiceExecutionRecord record()
    {
        ServiceExecutionRecord copy{};
        {
            const kernel::LockGuard lock{record_mutex_};
            copy = record_;
        }

        if constexpr (IS_ENABLED(CONFIG_THREAD_STACK_INFO) &&
                      IS_ENABLED(CONFIG_INIT_STACKS))
        {
            const auto diagnostics =
                kernel::snapshot_thread(copy.service.name, thread_, Spec::stack_bytes);
            copy.stack_usage_available = diagnostics.stack_available;
            copy.stack_unused_bytes = diagnostics.stack_unused;
            copy.stack_used_bytes = diagnostics.stack_used;
        }
        return copy;
    }

    kernel::Thread &thread()
    {
        return thread_;
    }

private:
    static void entry(void *self_ptr)
    {
        auto *self = static_cast<ServiceThreadRuntime *>(self_ptr);
        if (self == nullptr)
        {
            return;
        }

        StopToken stop_token = self->thread_.stop_token();
        const Status run_status = call_run(stop_token);
        const bool stopped = stop_token.stop_requested();
        const Status lifecycle_status =
            run_status != Status::Ok ? run_status
                                     : (stopped ? Status::Ok : Status::UnexpectedExit);

        {
            const kernel::LockGuard lock{self->record_mutex_};
            self->record_.running = false;
            self->record_.exited = true;
            self->record_.exit_after_stop_request = stopped;
            self->record_.run_status = run_status;
        }
        SystemT::record_service_exit(self->service_id_, lifecycle_status, stopped);
    }

    static Status call_run(StopToken stop_token)
    {
        if constexpr (std::is_void_v<decltype(ServiceT::run(stop_token))>)
        {
            ServiceT::run(stop_token);
            return Status::Ok;
        }
        else
        {
            return normalize_status(ServiceT::run(stop_token));
        }
    }

    void reset_record(ComponentDescriptor descriptor)
    {
        const kernel::LockGuard lock{record_mutex_};
        record_ = {
            .service = descriptor,
            .abort_configured = Spec::abort_on_stop_timeout,
            .stop_timeout_ms = static_cast<std::uint32_t>(Spec::stop_timeout.count()),
            .configured_stack_bytes = Spec::stack_bytes,
        };
    }

    ComponentId service_id_{};
    kernel::Thread thread_;
    kernel::ThreadStorage<Spec::stack_bytes> storage_{};
    kernel::Mutex record_mutex_{};
    ServiceExecutionRecord record_{};
};

using SolarLogSource = solar::Name<"solar">;
using BootLogCategory = solar::Name<"boot">;
using LifecycleLogCategory = solar::Name<"lifecycle">;

template <typename LoggerT>
using SolarInternalLog = typename LoggerT::template Log<
    SolarLogSource,
    solar::log::Categories<BootLogCategory, LifecycleLogCategory>>;

} // namespace detail

template <typename BoardT,
          typename PeripheralsGroup,
          typename DevicesGroup,
          typename FacilitiesGroup,
          typename ServicesGroup,
          typename TasksGroup = Tasks<>,
          typename ChannelsGroup = Channels<>,
          typename RuntimeGroup = Runtime<>>
class System;

/**
 * @brief Static owner of a compile-time Solar graph.
 */
template <typename BoardT,
          typename... PeripheralTypes,
          typename... DeviceTypes,
          typename... FacilityTypes,
          typename... ServiceTypes,
          typename... TaskTypes,
          typename... ChannelTypes,
          typename LoggerT>
class System<BoardT,
             Peripherals<PeripheralTypes...>,
             Devices<DeviceTypes...>,
             Facilities<FacilityTypes...>,
             Services<ServiceTypes...>,
             Tasks<TaskTypes...>,
             Channels<ChannelTypes...>,
             Runtime<Logging<LoggerT>>>
{
public:
    using Board = BoardT;
    using Runtime = solar::Runtime<solar::Logging<LoggerT>>;
    using Logger = LoggerT;
    using EntryFacilities = solar::Facilities<LoggerT>;
    using Logging = typename Runtime::Logging;
    using PeripheralList = Peripherals<PeripheralTypes...>;
    using DeviceList = Devices<DeviceTypes...>;
    using FacilityList = solar::Facilities<FacilityTypes...>;
    using ServiceList = Services<ServiceTypes...>;
    using TaskList = Tasks<TaskTypes...>;
    using ChannelList = Channels<ChannelTypes...>;
    using AllComponents = TypeList<BoardT, PeripheralTypes..., DeviceTypes..., FacilityTypes...,
                                   ServiceTypes..., TaskTypes..., ChannelTypes...>;
    using AllComponentsTuple = std::tuple<BoardT, PeripheralTypes..., DeviceTypes..., FacilityTypes...,
                                          ServiceTypes..., TaskTypes..., ChannelTypes...>;
    using ThisSystem = System<BoardT, PeripheralList, DeviceList, FacilityList, ServiceList, TaskList, ChannelList, Runtime>;

    /**
     * @brief Metrics owned by all graph components and the board.
     */
    using MetricsCatalog = CollectMetrics<BoardT, PeripheralTypes..., DeviceTypes..., FacilityTypes..., ServiceTypes..., TaskTypes..., ChannelTypes...>;

    /**
     * @brief Events owned by all graph components and the board.
     */
    using EventsCatalog = CollectEvents<BoardT, PeripheralTypes..., DeviceTypes..., FacilityTypes..., ServiceTypes..., TaskTypes..., ChannelTypes...>;

    /**
     * @brief Remote schema types contributed by all graph components and the board.
     */
    using RemoteTypesCatalog = CollectRemoteTypes<BoardT, PeripheralTypes..., DeviceTypes..., FacilityTypes..., ServiceTypes..., TaskTypes..., ChannelTypes...>;

    /**
     * @brief Remote request/response methods contributed by all graph components and the board.
     */
    using RemoteMethodsCatalog = CollectRemoteMethods<BoardT, PeripheralTypes..., DeviceTypes..., FacilityTypes..., ServiceTypes..., TaskTypes..., ChannelTypes...>;

    /**
     * @brief Remote pub/sub topics contributed by all graph components and the board.
     */
    using RemoteTopicsCatalog = CollectRemoteTopics<BoardT, PeripheralTypes..., DeviceTypes..., FacilityTypes..., ServiceTypes..., TaskTypes..., ChannelTypes...>;

    /**
     * @brief Remote observable snapshot/stream resources contributed by all graph components and the board.
     */
    using RemoteObservablesCatalog = CollectRemoteObservables<BoardT, PeripheralTypes..., DeviceTypes..., FacilityTypes..., ServiceTypes..., TaskTypes..., ChannelTypes...>;

    template <typename SourceNameT, typename CategoryListT = log::Categories<>>
    using Log = typename LoggerT::template Log<SourceNameT, CategoryListT>;

    static_assert(detail::UniqueTypes<BoardT, PeripheralTypes..., DeviceTypes..., FacilityTypes...,
                                      ServiceTypes..., TaskTypes..., ChannelTypes...>::value,
                  "Solar graph contains a duplicate component type");
    static_assert(detail::UniqueTypes<ServiceTypes...>::value,
                  "Solar service list contains a duplicate service type");
    static_assert(detail::UniqueNames<BoardT, PeripheralTypes..., DeviceTypes..., FacilityTypes...,
                                      ServiceTypes..., TaskTypes..., ChannelTypes...>::value,
                  "Solar component names must be unique for diagnostics and protocol stability");
    static_assert(detail::AllDependenciesAvailable<AllComponents, BoardT, PeripheralTypes...,
                                                    DeviceTypes..., FacilityTypes..., ServiceTypes...,
                                                    TaskTypes..., ChannelTypes...>::value,
                  "Solar component declares a required dependency type that is not registered");
    static_assert(detail::UniqueNamesInListV<MetricsCatalog>, "Solar metric contribution names must be unique");
    static_assert(detail::UniqueNamesInListV<EventsCatalog>, "Solar event contribution names must be unique");
    static_assert(detail::UniqueTypeIdsInListV<RemoteTypesCatalog>, "Solar Remote type ids must be unique");
    static_assert(detail::UniqueIdsInListV<RemoteMethodsCatalog>, "Solar Remote method ids must be unique");
    static_assert(detail::UniqueIdsInListV<RemoteTopicsCatalog>, "Solar Remote topic ids must be unique");
    static_assert(detail::UniqueIdsInListV<RemoteObservablesCatalog>, "Solar Remote observable ids must be unique");

    static constexpr std::size_t PeripheralCount = sizeof...(PeripheralTypes);
    static constexpr std::size_t DeviceCount = sizeof...(DeviceTypes);
    static constexpr std::size_t FacilityCount = sizeof...(FacilityTypes);
    static constexpr std::size_t ServiceCount = sizeof...(ServiceTypes);
    static constexpr std::size_t TaskCount = sizeof...(TaskTypes);
    static constexpr std::size_t ChannelCount = sizeof...(ChannelTypes);
    static constexpr std::size_t ComponentCount =
        1 + PeripheralCount + DeviceCount + FacilityCount + ServiceCount + TaskCount + ChannelCount;
    static_assert(ComponentCount < ComponentId::InvalidValue,
                  "Solar component count exceeds the lifecycle ComponentId range");

    static constexpr std::size_t BoardOffset = 0;
    static constexpr std::size_t PeripheralOffset = 1;
    static constexpr std::size_t DeviceOffset = PeripheralOffset + PeripheralCount;
    static constexpr std::size_t FacilityOffset = DeviceOffset + DeviceCount;
    static constexpr std::size_t ServiceOffset = FacilityOffset + FacilityCount;
    static constexpr std::size_t TaskOffset = ServiceOffset + ServiceCount;
    static constexpr std::size_t ChannelOffset = TaskOffset + TaskCount;

    using LifecycleStorage = solar::lifecycle::Storage<ComponentCount>;
    using LifecycleRecords = typename LifecycleStorage::Records;
    using ComponentDescriptors = std::array<ComponentDescriptor, ComponentCount>;

    struct lifecycle
    {
        static Result<SystemState> state()
        {
            return lifecycle_storage_.system_state();
        }

        static Result<LifecycleRecords> components()
        {
            return lifecycle_storage_.records();
        }

        template <typename ComponentT>
        static Result<LifecycleRecord> record()
        {
            return lifecycle_storage_.record(component_id<ComponentT>());
        }
    };

    struct kernel
    {
        static auto service_threads()
        {
            return service_execution_records(
                std::index_sequence_for<ServiceTypes...>{});
        }

        template <typename ServiceT>
        static ServiceExecutionRecord thread()
        {
            constexpr std::size_t index = service_index<ServiceT>();
            return std::get<index>(service_threads_).record();
        }
    };

    struct graph
    {
        static constexpr ComponentDescriptors components()
        {
            return component_descriptors();
        }

        template <typename ComponentT>
        static constexpr ComponentDescriptor component()
        {
            return component_descriptors()[component_id<ComponentT>().value()];
        }

        template <typename ComponentT>
        static constexpr auto dependencies()
        {
            return dependency_descriptors<ComponentT>(detail::DependenciesOfT<ComponentT>{});
        }
    };

    System() = delete;

    /**
     * @brief Boot the graph in deterministic order.
     *
     * Boot initializes/starts board, peripherals, facilities, devices, services,
     * and tasks. Services are started as their own Kernel threads after their
     * `init(ctx)` and optional `start(ctx)` phases succeed.
     */
    static Result<BootReport> boot()
    {
        const auto current_state = lifecycle_storage_.system_state();
        if (!current_state)
        {
            return current_state.status();
        }
        if (current_state.value() != SystemState::Dormant)
        {
            return Status::Already;
        }

        boot_report_ = {};
        Status status = lifecycle_storage_.transition_system(SystemState::Booting);
        if (status != Status::Ok)
        {
            return status;
        }

        if constexpr (IS_ENABLED(CONFIG_SOLAR_BOOT_LOGS))
        {
            SolarLog::template info<detail::BootLogCategory>("boot begin");
        }

        for (const std::size_t index : topology().order)
        {
            status = initialize_index(index);
            if (status != Status::Ok) return boot_failed(status);
        }

        status = lifecycle_storage_.transition_system(SystemState::Initialized);
        if (status != Status::Ok)
        {
            return boot_failed(fail_system(status));
        }

        status = lifecycle_storage_.transition_system(SystemState::Starting);
        if (status != Status::Ok)
        {
            return boot_failed(fail_system(status));
        }

        for (const std::size_t index : topology().order)
        {
            status = start_index(index);
            if (status != Status::Ok) return boot_failed(status);
        }

        status = lifecycle_storage_.transition_system(SystemState::Running);
        if (status != Status::Ok)
        {
            return boot_failed(fail_system(status));
        }

        boot_report_.status = Status::Ok;
        if constexpr (IS_ENABLED(CONFIG_SOLAR_BOOT_LOGS))
        {
            SolarLog::template info<detail::BootLogCategory>("boot complete");
        }
        return boot_report_;
    }

    /**
     * @brief Request shutdown for service threads and call service stop hooks.
     */
    static Status stop()
    {
        const auto state = lifecycle_storage_.system_state();
        if (!state)
        {
            return state.status();
        }
        if (state.value() == SystemState::Dormant)
        {
            return Status::Ok;
        }
        if (shutdown_complete_)
        {
            return stop_report_.status;
        }
        return shutdown(false);
    }

    /**
     * @brief Report from the last boot attempt.
     */
    static BootReport const &boot_report()
    {
        return boot_report_;
    }

    static StopReport const &stop_report()
    {
        return stop_report_;
    }

    static void record_service_exit(ComponentId id, Status status, bool stop_requested)
    {
        if (status == Status::Ok && stop_requested)
        {
            return;
        }

        (void)lifecycle_storage_.transition(id,
                                            LifecycleState::Failed,
                                            LifecycleOperation::Run,
                                            status);
        (void)lifecycle_storage_.transition_system(SystemState::Failed);
    }

private:
    using SolarLog = detail::SolarInternalLog<LoggerT>;

    template <typename ComponentT>
    static constexpr const char *component_name(ComponentKind kind)
    {
        if constexpr (requires { typename ComponentT::Name; ComponentT::Name::c_str(); })
        {
            return ComponentT::Name::c_str();
        }
        else
        {
            return kind == ComponentKind::Board ? "board" : "unnamed";
        }
    }

    template <typename ComponentT>
    static constexpr ComponentDescriptor descriptor(ComponentId id, ComponentKind kind)
    {
        return {id, component_name<ComponentT>(kind), kind};
    }

    template <typename ComponentT>
    static constexpr LifecycleHooks hooks()
    {
        return {
            .init = detail::HasInit<ComponentT>,
            .start = detail::HasStart<ComponentT>,
            .run = detail::HasRun<ComponentT>,
            .stop = detail::HasStop<ComponentT>,
            .deinit = detail::HasDeinit<ComponentT>,
        };
    }

    template <typename ComponentT>
    static consteval ComponentId component_id()
    {
        if constexpr (std::is_same_v<ComponentT, BoardT>)
        {
            return ComponentId{BoardOffset};
        }
        else if constexpr (detail::ContainsTypeV<ComponentT, PeripheralTypes...>)
        {
            return ComponentId{static_cast<ComponentId::Value>(
                PeripheralOffset + detail::TypeIndex<ComponentT, PeripheralTypes...>::value)};
        }
        else if constexpr (detail::ContainsTypeV<ComponentT, DeviceTypes...>)
        {
            return ComponentId{static_cast<ComponentId::Value>(
                DeviceOffset + detail::TypeIndex<ComponentT, DeviceTypes...>::value)};
        }
        else if constexpr (detail::ContainsTypeV<ComponentT, FacilityTypes...>)
        {
            return ComponentId{static_cast<ComponentId::Value>(
                FacilityOffset + detail::TypeIndex<ComponentT, FacilityTypes...>::value)};
        }
        else if constexpr (detail::ContainsTypeV<ComponentT, ServiceTypes...>)
        {
            return ComponentId{static_cast<ComponentId::Value>(
                ServiceOffset + detail::TypeIndex<ComponentT, ServiceTypes...>::value)};
        }
        else if constexpr (detail::ContainsTypeV<ComponentT, TaskTypes...>)
        {
            return ComponentId{static_cast<ComponentId::Value>(
                TaskOffset + detail::TypeIndex<ComponentT, TaskTypes...>::value)};
        }
        else if constexpr (detail::ContainsTypeV<ComponentT, ChannelTypes...>)
        {
            return ComponentId{static_cast<ComponentId::Value>(
                ChannelOffset + detail::TypeIndex<ComponentT, ChannelTypes...>::value)};
        }
        else
        {
            static_assert(!std::is_same_v<ComponentT, ComponentT>,
                          "Requested component type is not registered in this Solar system");
        }
    }

    template <typename ServiceT>
    static consteval std::size_t service_index()
    {
        static_assert(detail::ContainsTypeV<ServiceT, ServiceTypes...>,
                      "Requested service type is not registered in this Solar system");
        return detail::TypeIndex<ServiceT, ServiceTypes...>::value;
    }

    template <typename ComponentT, typename... DependencyTypes>
    static constexpr auto dependency_descriptors(Dependencies<DependencyTypes...>)
    {
        static_assert((detail::ContainsTypeV<DependencyTypes, BoardT, PeripheralTypes...,
                                             DeviceTypes..., FacilityTypes..., ServiceTypes...,
                                             TaskTypes..., ChannelTypes...> && ...),
                      "Requested dependency metadata contains an unregistered type");
        return std::array<ComponentDescriptor, sizeof...(DependencyTypes)>{
            descriptor<DependencyTypes>(component_id<DependencyTypes>(),
                                        component_kind(component_id<DependencyTypes>().value()))...};
    }

    static constexpr ComponentKind component_kind(std::size_t index)
    {
        if (index == BoardOffset) return ComponentKind::Board;
        if (index < DeviceOffset) return ComponentKind::Peripheral;
        if (index < FacilityOffset) return ComponentKind::Device;
        if (index < ServiceOffset) return ComponentKind::Facility;
        if (index < TaskOffset) return ComponentKind::Service;
        if (index < ChannelOffset) return ComponentKind::Task;
        return ComponentKind::Channel;
    }

    template <typename ComponentT, typename... DependencyTypes>
    static constexpr void add_dependency_edges(
        std::array<std::array<bool, ComponentCount>, ComponentCount> &edges,
        Dependencies<DependencyTypes...>)
    {
        if constexpr ((detail::ContainsTypeV<DependencyTypes, BoardT, PeripheralTypes...,
                                             DeviceTypes..., FacilityTypes..., ServiceTypes...,
                                             TaskTypes..., ChannelTypes...> && ...))
        {
            constexpr std::size_t component = component_id<ComponentT>().value();
            ((edges[component][component_id<DependencyTypes>().value()] = true), ...);
        }
    }

    static constexpr auto dependency_edges()
    {
        std::array<std::array<bool, ComponentCount>, ComponentCount> edges{};
        auto add = [&]<typename ComponentT>() {
            add_dependency_edges<ComponentT>(edges, detail::DependenciesOfT<ComponentT>{});
        };
        add.template operator()<BoardT>();
        (add.template operator()<PeripheralTypes>(), ...);
        (add.template operator()<DeviceTypes>(), ...);
        (add.template operator()<FacilityTypes>(), ...);
        (add.template operator()<ServiceTypes>(), ...);
        (add.template operator()<TaskTypes>(), ...);
        (add.template operator()<ChannelTypes>(), ...);
        return edges;
    }

    struct Topology
    {
        std::array<std::size_t, ComponentCount> order{};
        bool valid = false;
    };

    static constexpr Topology topology()
    {
        const auto edges = dependency_edges();
        Topology result{};
        std::array<bool, ComponentCount> emitted{};
        for (std::size_t position = 0; position < ComponentCount; ++position)
        {
            bool found = false;
            for (std::size_t candidate = 0; candidate < ComponentCount; ++candidate)
            {
                if (emitted[candidate]) continue;
                bool ready = true;
                for (std::size_t dependency = 0; dependency < ComponentCount; ++dependency)
                {
                    if (edges[candidate][dependency] && !emitted[dependency]) ready = false;
                }
                if (ready)
                {
                    result.order[position] = candidate;
                    emitted[candidate] = true;
                    found = true;
                    break;
                }
            }
            if (!found) return result;
        }
        result.valid = true;
        return result;
    }

    static_assert(topology().valid,
                  "Solar graph contains a required dependency cycle");

    static constexpr LifecycleRecords initial_lifecycle_records()
    {
        LifecycleRecords records{};
        std::size_t index = 0;

        auto add = [&]<typename ComponentT>(ComponentKind kind) {
            records[index] = {
                .component = descriptor<ComponentT>(ComponentId{static_cast<ComponentId::Value>(index)}, kind),
                .hooks = hooks<ComponentT>(),
            };
            ++index;
        };

        add.template operator()<BoardT>(ComponentKind::Board);
        (add.template operator()<PeripheralTypes>(ComponentKind::Peripheral), ...);
        (add.template operator()<DeviceTypes>(ComponentKind::Device), ...);
        (add.template operator()<FacilityTypes>(ComponentKind::Facility), ...);
        (add.template operator()<ServiceTypes>(ComponentKind::Service), ...);
        (add.template operator()<TaskTypes>(ComponentKind::Task), ...);
        (add.template operator()<ChannelTypes>(ComponentKind::Channel), ...);
        return records;
    }

    static constexpr ComponentDescriptors component_descriptors()
    {
        ComponentDescriptors descriptors{};
        const LifecycleRecords records = initial_lifecycle_records();
        for (std::size_t index = 0; index < records.size(); ++index)
        {
            descriptors[index] = records[index].component;
        }
        return descriptors;
    }

    static Status fail_component(ComponentDescriptor component,
                                 BootPhase phase,
                                 LifecycleOperation operation,
                                 Status status)
    {
        (void)lifecycle_storage_.transition(component.id,
                                            LifecycleState::Failed,
                                            operation,
                                            status);
        boot_report_.record_failure(component, phase, operation, status);
        (void)lifecycle_storage_.transition_system(SystemState::Failed);

        if constexpr (IS_ENABLED(CONFIG_SOLAR_BOOT_LOGS))
        {
            SolarLog::template error<detail::BootLogCategory>(
                "boot failed phase=%u status=%u component=%s",
                static_cast<unsigned>(phase),
                static_cast<unsigned>(status),
                component.name);
        }
        return status;
    }

    static Status fail_system(Status status)
    {
        boot_report_.record_failure({}, BootPhase::None, LifecycleOperation::None, status);
        (void)lifecycle_storage_.transition_system(SystemState::Failed);
        return status;
    }

    static Result<BootReport> boot_failed(Status status)
    {
        (void)shutdown(true);
        return status;
    }

    static void record_shutdown_failure(ComponentDescriptor component,
                                        LifecycleOperation operation,
                                        Status status)
    {
        stop_report_.record_failure(component, operation, status);
        (void)lifecycle_storage_.transition(component.id,
                                            LifecycleState::Failed,
                                            operation,
                                            status);
    }

    template <typename ComponentT>
    static void stop_one(ComponentId id, ComponentKind kind)
    {
        const auto before = lifecycle_storage_.record(id);
        if (!before || !before.value().started_successfully)
        {
            return;
        }

        const ComponentDescriptor component_descriptor = descriptor<ComponentT>(id, kind);
        const bool preserve_failed_state = before.value().has_failure();
        Status status = lifecycle_storage_.transition(id,
                                                      LifecycleState::Stopping,
                                                      LifecycleOperation::Stop);
        if (status != Status::Ok)
        {
            stop_report_.record_failure(component_descriptor,
                                        LifecycleOperation::Stop,
                                        status);
            return;
        }

        status = detail::call_stop<ComponentT>();
        if (status != Status::Ok)
        {
            record_shutdown_failure(component_descriptor, LifecycleOperation::Stop, status);
            return;
        }

        const LifecycleState final_state = preserve_failed_state
                                               ? LifecycleState::Failed
                                               : LifecycleState::Stopped;
        status = lifecycle_storage_.transition(id,
                                               final_state,
                                               LifecycleOperation::Stop);
        if (status != Status::Ok)
        {
            stop_report_.record_failure(component_descriptor,
                                        LifecycleOperation::Stop,
                                        status);
            return;
        }
        stop_report_.record_success();
    }

    template <typename ComponentT>
    static void deinit_one(ComponentId id, ComponentKind kind)
    {
        const auto before = lifecycle_storage_.record(id);
        if (!before || !before.value().initialized_successfully ||
            before.value().deinitialized_successfully)
        {
            return;
        }

        const ComponentDescriptor component_descriptor = descriptor<ComponentT>(id, kind);
        const bool preserve_failed_state = before.value().has_failure();
        Status status = lifecycle_storage_.transition(id,
                                                      LifecycleState::Deinitializing,
                                                      LifecycleOperation::Deinit);
        if (status != Status::Ok)
        {
            stop_report_.record_failure(component_descriptor,
                                        LifecycleOperation::Deinit,
                                        status);
            return;
        }

        status = detail::call_deinit<ComponentT>();
        if (status != Status::Ok)
        {
            record_shutdown_failure(component_descriptor, LifecycleOperation::Deinit, status);
            return;
        }

        const LifecycleState final_state = preserve_failed_state
                                               ? LifecycleState::Failed
                                               : LifecycleState::Deinitialized;
        status = lifecycle_storage_.transition(id,
                                               final_state,
                                               LifecycleOperation::Deinit);
        if (status != Status::Ok)
        {
            stop_report_.record_failure(component_descriptor,
                                        LifecycleOperation::Deinit,
                                        status);
            return;
        }
        stop_report_.record_success();
    }

    template <ComponentKind Kind,
              std::size_t Offset,
              typename... Types,
              std::size_t... Indices>
    static void stop_group_reverse(TypeList<Types...>, std::index_sequence<Indices...>)
    {
        constexpr std::size_t count = sizeof...(Indices);
        (stop_one<std::tuple_element_t<count - 1 - Indices, std::tuple<Types...>>>(
             ComponentId{static_cast<ComponentId::Value>(Offset + count - 1 - Indices)}, Kind),
         ...);
    }

    template <ComponentKind Kind,
              std::size_t Offset,
              typename... Types,
              std::size_t... Indices>
    static void deinit_group_reverse(TypeList<Types...>, std::index_sequence<Indices...>)
    {
        constexpr std::size_t count = sizeof...(Indices);
        (deinit_one<std::tuple_element_t<count - 1 - Indices, std::tuple<Types...>>>(
             ComponentId{static_cast<ComponentId::Value>(Offset + count - 1 - Indices)}, Kind),
         ...);
    }

    static Status shutdown(bool rollback)
    {
        stop_report_ = {};
        const auto current = lifecycle_storage_.system_state();
        if (!current)
        {
            return current.status();
        }

        if (current.value() != SystemState::Failed)
        {
            const Status transition_status =
                lifecycle_storage_.transition_system(SystemState::Stopping);
            if (transition_status != Status::Ok)
            {
                stop_report_.status = transition_status;
                return transition_status;
            }
        }

        request_service_stops(std::index_sequence_for<ServiceTypes...>{});
        bool active_service_remains = false;
        const auto order = topology().order;
        for (std::size_t position = ComponentCount; position > 0; --position)
        {
            const std::size_t index = order[position - 1];
            stop_index(index);
            if (component_kind(index) == ComponentKind::Service)
            {
                active_service_remains = service_thread_running(
                    std::index_sequence_for<ServiceTypes...>{});
                if (active_service_remains) break;
            }
        }

        if (!active_service_remains)
        {
            for (std::size_t position = ComponentCount; position > 0; --position)
            {
                deinit_index(order[position - 1]);
            }
        }

        shutdown_complete_ = true;
        if (!rollback && current.value() != SystemState::Failed)
        {
            const SystemState final_state = stop_report_.ok()
                                                ? SystemState::Stopped
                                                : SystemState::Failed;
            const Status transition_status =
                lifecycle_storage_.transition_system(final_state);
            if (transition_status != Status::Ok && stop_report_.ok())
            {
                stop_report_.status = transition_status;
            }
        }
        return stop_report_.status;
    }

    static constexpr BootPhase boot_phase(ComponentKind kind, LifecycleOperation operation)
    {
        const bool start = operation == LifecycleOperation::Start;
        switch (kind)
        {
        case ComponentKind::Board: return start ? BootPhase::BoardStart : BootPhase::BoardInit;
        case ComponentKind::Peripheral: return start ? BootPhase::PeripheralStart : BootPhase::PeripheralInit;
        case ComponentKind::Device: return start ? BootPhase::DeviceStart : BootPhase::DeviceInit;
        case ComponentKind::Facility: return start ? BootPhase::FacilityStart : BootPhase::FacilityInit;
        case ComponentKind::Service: return start ? BootPhase::ServiceStart : BootPhase::ServiceInit;
        case ComponentKind::Task: return start ? BootPhase::TaskStart : BootPhase::TaskInit;
        case ComponentKind::Channel: return start ? BootPhase::ChannelStart : BootPhase::ChannelInit;
        case ComponentKind::Unknown: return BootPhase::None;
        }
        return BootPhase::None;
    }

    template <typename Visitor, std::size_t... Indices>
    static Status visit_component(std::size_t index,
                                  Visitor &&visitor,
                                  std::index_sequence<Indices...>)
    {
        Status status = Status::NotFound;
        ((index == Indices
              ? (status = visitor.template operator()<
                     std::tuple_element_t<Indices, AllComponentsTuple>>(), true)
              : false) || ...);
        return status;
    }

    static Status initialize_index(std::size_t index)
    {
        const ComponentKind kind = component_kind(index);
        return visit_component(index, [&]<typename ComponentT>() {
            return initialize_one<ComponentT>(
                ComponentId{static_cast<ComponentId::Value>(index)},
                kind,
                boot_phase(kind, LifecycleOperation::Init));
        }, std::make_index_sequence<ComponentCount>{});
    }

    static Status start_index(std::size_t index)
    {
        const ComponentKind kind = component_kind(index);
        return visit_component(index, [&]<typename ComponentT>() {
            if constexpr (detail::ContainsTypeV<ComponentT, ServiceTypes...>)
            {
                return start_service_thread<service_index<ComponentT>()>();
            }
            else
            {
                return start_one<ComponentT>(
                    ComponentId{static_cast<ComponentId::Value>(index)},
                    kind,
                    boot_phase(kind, LifecycleOperation::Start));
            }
        }, std::make_index_sequence<ComponentCount>{});
    }

    static void stop_index(std::size_t index)
    {
        const ComponentKind kind = component_kind(index);
        (void)visit_component(index, [&]<typename ComponentT>() {
            if constexpr (detail::ContainsTypeV<ComponentT, ServiceTypes...>)
            {
                join_service_thread<service_index<ComponentT>()>();
            }
            stop_one<ComponentT>(ComponentId{static_cast<ComponentId::Value>(index)}, kind);
            return Status::Ok;
        }, std::make_index_sequence<ComponentCount>{});
    }

    static void deinit_index(std::size_t index)
    {
        const ComponentKind kind = component_kind(index);
        (void)visit_component(index, [&]<typename ComponentT>() {
            deinit_one<ComponentT>(ComponentId{static_cast<ComponentId::Value>(index)}, kind);
            return Status::Ok;
        }, std::make_index_sequence<ComponentCount>{});
    }

    template <typename ComponentT>
    static Status initialize_one(ComponentId id, ComponentKind kind, BootPhase phase)
    {
        const ComponentDescriptor component_descriptor = descriptor<ComponentT>(id, kind);
        Status status = lifecycle_storage_.transition(id,
                                                      LifecycleState::Initializing,
                                                      LifecycleOperation::Init);
        if (status != Status::Ok)
        {
            return fail_system(status);
        }

        status = detail::call_init<ComponentT>();
        if (status != Status::Ok)
        {
            return fail_component(component_descriptor, phase, LifecycleOperation::Init, status);
        }

        status = lifecycle_storage_.transition(id,
                                               LifecycleState::Initialized,
                                               LifecycleOperation::Init);
        if (status != Status::Ok)
        {
            return fail_system(status);
        }
        boot_report_.record_success();
        return Status::Ok;
    }

    template <typename ComponentT>
    static Status start_one(ComponentId id, ComponentKind kind, BootPhase phase)
    {
        const ComponentDescriptor component_descriptor = descriptor<ComponentT>(id, kind);
        Status status = lifecycle_storage_.transition(id,
                                                      LifecycleState::Starting,
                                                      LifecycleOperation::Start);
        if (status != Status::Ok)
        {
            return fail_system(status);
        }

        status = detail::call_start<ComponentT>();
        if (status != Status::Ok)
        {
            return fail_component(component_descriptor, phase, LifecycleOperation::Start, status);
        }

        status = lifecycle_storage_.transition(id,
                                               LifecycleState::Running,
                                               LifecycleOperation::Start);
        if (status != Status::Ok)
        {
            return fail_system(status);
        }
        boot_report_.record_success();
        return Status::Ok;
    }

    template <ComponentKind Kind,
              BootPhase Phase,
              std::size_t Offset,
              typename... Types,
              std::size_t... Indices>
    static Status initialize_tuple(TypeList<Types...>, std::index_sequence<Indices...>)
    {
        Status status = Status::Ok;
        ((status == Status::Ok
              ? status = initialize_one<std::tuple_element_t<Indices, std::tuple<Types...>>>(
                    ComponentId{static_cast<ComponentId::Value>(Offset + Indices)}, Kind, Phase)
              : status),
         ...);
        return status;
    }

    template <ComponentKind Kind,
              BootPhase Phase,
              std::size_t Offset,
              typename... Types,
              std::size_t... Indices>
    static Status start_tuple(TypeList<Types...>, std::index_sequence<Indices...>)
    {
        Status status = Status::Ok;
        ((status == Status::Ok
              ? status = start_one<std::tuple_element_t<Indices, std::tuple<Types...>>>(
                    ComponentId{static_cast<ComponentId::Value>(Offset + Indices)}, Kind, Phase)
              : status),
         ...);
        return status;
    }

    template <std::size_t... Indices>
    static Status start_service_threads(std::index_sequence<Indices...>)
    {
        Status status = Status::Ok;
        ((status == Status::Ok
              ? status = start_service_thread<Indices>()
              : status),
         ...);
        return status;
    }

    template <std::size_t Index>
    static Status start_service_thread()
    {
        using ServiceT = std::tuple_element_t<Index, std::tuple<ServiceTypes...>>;
        constexpr ComponentId id{static_cast<ComponentId::Value>(ServiceOffset + Index)};
        const ComponentDescriptor component_descriptor = descriptor<ServiceT>(id, ComponentKind::Service);

        Status status = lifecycle_storage_.transition(id,
                                                      LifecycleState::Starting,
                                                      LifecycleOperation::Start);
        if (status != Status::Ok)
        {
            return fail_system(status);
        }

        status = std::get<Index>(service_threads_).start(component_descriptor);
        if (status != Status::Ok)
        {
            return fail_component(component_descriptor,
                                  BootPhase::ServiceStart,
                                  LifecycleOperation::Start,
                                  status);
        }

        const ServiceExecutionRecord execution =
            std::get<Index>(service_threads_).record();
        if (execution.exited &&
            (execution.run_status != Status::Ok || !execution.exit_after_stop_request))
        {
            const Status exit_status = execution.run_status != Status::Ok
                                           ? execution.run_status
                                           : Status::UnexpectedExit;
            return fail_component(component_descriptor,
                                  BootPhase::ExecutionStart,
                                  LifecycleOperation::Run,
                                  exit_status);
        }

        status = lifecycle_storage_.transition(id,
                                               LifecycleState::Running,
                                               LifecycleOperation::Start);
        if (status != Status::Ok)
        {
            return fail_system(status);
        }
        boot_report_.record_success();
        return Status::Ok;
    }

    template <std::size_t... Indices>
    static auto service_execution_records(std::index_sequence<Indices...>)
    {
        return std::array<ServiceExecutionRecord, ServiceCount>{
            std::get<Indices>(service_threads_).record()...};
    }

    template <std::size_t... Indices>
    static void request_service_stops(std::index_sequence<Indices...>)
    {
        (std::get<Indices>(service_threads_).request_stop(), ...);
    }

    template <std::size_t... Indices>
    static void join_service_threads_reverse(std::index_sequence<Indices...>)
    {
        constexpr std::size_t count = sizeof...(Indices);
        (join_service_thread<count - 1 - Indices>(), ...);
    }

    template <std::size_t Index>
    static void join_service_thread()
    {
        using ServiceT = std::tuple_element_t<Index, std::tuple<ServiceTypes...>>;
        constexpr ComponentId id{static_cast<ComponentId::Value>(ServiceOffset + Index)};
        const ServiceExecutionRecord before = std::get<Index>(service_threads_).record();
        if (!before.thread_created)
        {
            return;
        }

        const Status status = std::get<Index>(service_threads_).join();
        if (status != Status::Ok)
        {
            record_shutdown_failure(descriptor<ServiceT>(id, ComponentKind::Service),
                                    LifecycleOperation::Stop,
                                    status);
        }
    }

    template <std::size_t... Indices>
    static bool service_thread_running(std::index_sequence<Indices...>)
    {
        return (false || ... || std::get<Indices>(service_threads_).record().running);
    }

    static inline std::tuple<detail::ServiceThreadRuntime<ServiceTypes, ThisSystem>...> service_threads_{};
    static inline LifecycleStorage lifecycle_storage_{initial_lifecycle_records()};
    static inline BootReport boot_report_{};
    static inline StopReport stop_report_{};
    static inline bool shutdown_complete_ = false;
};

} // namespace solar
