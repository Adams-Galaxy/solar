#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>

#include "solar/contribution.hpp"
#include "solar/core.hpp"
#include "solar/log/logger.hpp"
#include "solar/service.hpp"

namespace solar
{

template <typename SystemT>
class Context;

template <typename LoggerT = log::NullLogger>
struct Logging
{
    using Logger = LoggerT;
};

/**
 * @brief Default system policy values used when a robot omits overrides.
 */
struct DefaultConfig
{
    static constexpr bool EnableBootLogs = true;
    static constexpr bool EnableRemoteLogs = true;
    static constexpr bool AllowDynamicAllocation = false;

    static constexpr std::size_t RemotePayloadBytes = 1024;
    static constexpr std::size_t RemoteFrameBufferBytes = 1152;
    static constexpr std::uint16_t RemoteHeartbeatMs = 1000;
    static constexpr std::uint16_t RemoteSessionTimeoutMs = 5000;
};

/**
 * @brief Runtime config wrapper.
 *
 * `Config<T>` lets a robot provide only the policy fields it cares about while
 * Solar resolves missing fields from `DefaultConfig`.
 */
template <typename ConfigT = DefaultConfig>
struct Config
{
    using Settings = ConfigT;
};

template <typename LoggingGroup = Logging<>, typename ConfigGroup = Config<>>
struct Runtime;

/**
 * @brief Runtime policy group carried by `System`.
 */
template <typename LoggerT, typename ConfigT>
struct Runtime<Logging<LoggerT>, Config<ConfigT>>
{
    using Logging = solar::Logging<LoggerT>;
    using Logger = LoggerT;
    using Config = ConfigT;
};

/**
 * @brief Lightweight static graph snapshot entry.
 */
struct ComponentSnapshot
{
    const char *name = "";
    const char *kind = "";
    LifecycleState state = LifecycleState::Registered;
};

namespace detail
{

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

template <typename T, typename ContextT>
concept HasContextInit = requires(T object, ContextT &ctx) {
    object.init(ctx);
};

template <typename T>
concept HasBareInit = requires(T object) {
    object.init();
};

template <typename T, typename ContextT>
concept HasInit = HasContextInit<T, ContextT> || HasBareInit<T>;

template <typename T, typename ContextT>
concept HasStart = requires(T object, ContextT &ctx) {
    object.start(ctx);
};

template <typename T, typename ContextT>
concept HasStop = requires(T object, ContextT &ctx) {
    object.stop(ctx);
};

template <typename T>
concept HasServiceThread = requires {
    typename T::Thread;
    typename T::Thread::Name;
    { T::Thread::stack_bytes } -> std::convertible_to<std::size_t>;
    { T::Thread::priority } -> std::convertible_to<kernel::Priority>;
};

template <typename T, typename ContextT>
concept HasRun = requires(T object, ContextT &ctx, StopToken token) {
    object.run(ctx, token);
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

template <typename T, typename ContextT>
Status call_init(T &object, ContextT &ctx)
{
    if constexpr (HasContextInit<T, ContextT>)
    {
        if constexpr (std::is_void_v<decltype(object.init(ctx))>)
        {
            object.init(ctx);
            return Status::Ok;
        }
        else
        {
            return normalize_status(object.init(ctx));
        }
    }
    else if constexpr (HasBareInit<T>)
    {
        if constexpr (std::is_void_v<decltype(object.init())>)
        {
            object.init();
            return Status::Ok;
        }
        else
        {
            return normalize_status(object.init());
        }
    }
    else
    {
        return Status::Ok;
    }
}

template <typename T, typename ContextT>
Status call_start(T &object, ContextT &ctx)
{
    if constexpr (HasStart<T, ContextT>)
    {
        if constexpr (std::is_void_v<decltype(object.start(ctx))>)
        {
            object.start(ctx);
            return Status::Ok;
        }
        else
        {
            return normalize_status(object.start(ctx));
        }
    }
    else
    {
        return Status::Ok;
    }
}

template <typename T, typename ContextT>
Status call_stop(T &object, ContextT &ctx)
{
    if constexpr (HasStop<T, ContextT>)
    {
        if constexpr (std::is_void_v<decltype(object.stop(ctx))>)
        {
            object.stop(ctx);
            return Status::Ok;
        }
        else
        {
            return normalize_status(object.stop(ctx));
        }
    }
    else
    {
        return Status::Ok;
    }
}

template <typename TupleT, typename ContextT, typename PhaseT>
Status for_each_status(TupleT &tuple, ContextT &ctx, PhaseT phase)
{
    Status status = Status::Ok;
    std::apply(
        [&](auto &...items) {
            ((status == Status::Ok ? status = phase(items, ctx) : status), ...);
        },
        tuple);
    return status;
}

template <typename TupleT, typename ContextT, typename PhaseT>
void for_each_void(TupleT &tuple, ContextT &ctx, PhaseT phase)
{
    std::apply([&](auto &...items) { (phase(items, ctx), ...); }, tuple);
}

template <typename ServiceT, typename SystemT>
class ServiceThreadRuntime
{
public:
    static_assert(HasServiceThread<ServiceT>, "Solar services must declare using Thread = solar::ServiceSpec<...>");
    static_assert(HasRun<ServiceT, Context<SystemT>>,
                  "Solar services must implement run(ctx, stop_token); polling is intentionally not a service model");

    using Spec = typename ServiceT::Thread;

    ServiceThreadRuntime()
        : thread_{Spec::Name::c_str(), Spec::priority, static_cast<std::uint32_t>(Spec::stack_bytes), &ServiceThreadRuntime::entry, this}
    {
    }

    Status start(ServiceT &service, SystemT &system)
    {
        service_ = &service;
        system_ = &system;
        Context<SystemT> ctx{system};
        const Status status = call_start(service, ctx);
        if (status != Status::Ok)
        {
            return status;
        }
        return thread_.start(storage_);
    }

    Status stop()
    {
        thread_.request_stop();
        const Status status = thread_.join(kernel::Timeout::after(Spec::stop_timeout));
        if (status == Status::Timeout)
        {
            const Status abort_status = thread_.abort();
            return abort_status == Status::Ok ? Status::Timeout : abort_status;
        }
        return status;
    }

    kernel::Thread &thread()
    {
        return thread_;
    }

private:
    static void entry(void *self_ptr)
    {
        auto *self = static_cast<ServiceThreadRuntime *>(self_ptr);
        if (self == nullptr || self->service_ == nullptr || self->system_ == nullptr)
        {
            return;
        }

        Context<SystemT> ctx{*self->system_};
        StopToken stop_token = self->thread_.stop_token();

        (void)call_run(*self->service_, ctx, stop_token);
    }

    static Status call_run(ServiceT &service, Context<SystemT> &ctx, StopToken stop_token)
    {
        if constexpr (std::is_void_v<decltype(service.run(ctx, stop_token))>)
        {
            service.run(ctx, stop_token);
            return Status::Ok;
        }
        else
        {
            return normalize_status(service.run(ctx, stop_token));
        }
    }

    ServiceT *service_ = nullptr;
    SystemT *system_ = nullptr;
    kernel::Thread thread_;
    kernel::ThreadStorage<Spec::stack_bytes> storage_{};
};

using SolarLogSource = solar::Name<"solar">;
using BootLogCategory = solar::Name<"boot">;
using LifecycleLogCategory = solar::Name<"lifecycle">;

template <typename LoggerT>
using SolarInternalLog = typename LoggerT::template Log<
    SolarLogSource,
    solar::log::Categories<BootLogCategory, LifecycleLogCategory>>;

template <typename ConfigT>
struct ResolvedConfig
{
    static constexpr bool EnableBootLogs = [] {
        if constexpr (requires { ConfigT::EnableBootLogs; })
        {
            return static_cast<bool>(ConfigT::EnableBootLogs);
        }
        else
        {
            return DefaultConfig::EnableBootLogs;
        }
    }();

    static constexpr bool EnableRemoteLogs = [] {
        if constexpr (requires { ConfigT::EnableRemoteLogs; })
        {
            return static_cast<bool>(ConfigT::EnableRemoteLogs);
        }
        else
        {
            return DefaultConfig::EnableRemoteLogs;
        }
    }();

    static constexpr bool AllowDynamicAllocation = [] {
        if constexpr (requires { ConfigT::AllowDynamicAllocation; })
        {
            return static_cast<bool>(ConfigT::AllowDynamicAllocation);
        }
        else
        {
            return DefaultConfig::AllowDynamicAllocation;
        }
    }();

    static constexpr std::size_t RemotePayloadBytes = [] {
        if constexpr (requires { ConfigT::RemotePayloadBytes; })
        {
            return static_cast<std::size_t>(ConfigT::RemotePayloadBytes);
        }
        else
        {
            return DefaultConfig::RemotePayloadBytes;
        }
    }();

    static constexpr std::size_t RemoteFrameBufferBytes = [] {
        if constexpr (requires { ConfigT::RemoteFrameBufferBytes; })
        {
            return static_cast<std::size_t>(ConfigT::RemoteFrameBufferBytes);
        }
        else
        {
            return DefaultConfig::RemoteFrameBufferBytes;
        }
    }();

    static constexpr std::uint16_t RemoteHeartbeatMs = [] {
        if constexpr (requires { ConfigT::RemoteHeartbeatMs; })
        {
            return static_cast<std::uint16_t>(ConfigT::RemoteHeartbeatMs);
        }
        else
        {
            return DefaultConfig::RemoteHeartbeatMs;
        }
    }();

    static constexpr std::uint16_t RemoteSessionTimeoutMs = [] {
        if constexpr (requires { ConfigT::RemoteSessionTimeoutMs; })
        {
            return static_cast<std::uint16_t>(ConfigT::RemoteSessionTimeoutMs);
        }
        else
        {
            return DefaultConfig::RemoteSessionTimeoutMs;
        }
    }();
};

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
 * @brief Tuple-owned runtime instance of a static Solar graph.
 *
 * `System` is the central object created by firmware/simulation entry code. The
 * graph shape is entirely encoded in template arguments, while runtime objects
 * are owned in tuples and reached through typed accessors or `Context`.
 */
template <typename BoardT,
          typename... PeripheralTypes,
          typename... DeviceTypes,
          typename... FacilityTypes,
          typename... ServiceTypes,
          typename... TaskTypes,
          typename... ChannelTypes,
          typename LoggerT,
          typename ConfigT>
class System<BoardT,
             Peripherals<PeripheralTypes...>,
             Devices<DeviceTypes...>,
             Facilities<FacilityTypes...>,
             Services<ServiceTypes...>,
             Tasks<TaskTypes...>,
             Channels<ChannelTypes...>,
             Runtime<Logging<LoggerT>, Config<ConfigT>>>
{
public:
    using Board = BoardT;
    using Runtime = solar::Runtime<solar::Logging<LoggerT>, solar::Config<ConfigT>>;
    using Logger = LoggerT;
    using EntryFacilities = solar::Facilities<LoggerT>;
    using Logging = typename Runtime::Logging;
    using UserConfig = ConfigT;
    using Config = detail::ResolvedConfig<ConfigT>;
    using PeripheralList = Peripherals<PeripheralTypes...>;
    using DeviceList = Devices<DeviceTypes...>;
    using FacilityList = solar::Facilities<FacilityTypes...>;
    using ServiceList = Services<ServiceTypes...>;
    using TaskList = Tasks<TaskTypes...>;
    using ChannelList = Channels<ChannelTypes...>;
    using ThisSystem = System<BoardT, PeripheralList, DeviceList, FacilityList, ServiceList, TaskList, ChannelList, Runtime>;
    using ContextType = Context<ThisSystem>;

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

    static_assert(GraphValidV<PeripheralList, DeviceList, FacilityList, ServiceList, TaskList, ChannelList>,
                  "Solar graph must have unique component names and resolvable dependencies");
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

    ~System()
    {
        (void)Stop();
    }

    /**
     * @brief Boot the graph in deterministic order.
     *
     * Boot initializes/starts board, peripherals, facilities, devices, services,
     * and tasks. Services are started as their own Kernel threads after their
     * `init(ctx)` and optional `start(ctx)` phases succeed.
     */
    Status Boot()
    {
        ContextType ctx{*this};
        if constexpr (Config::EnableBootLogs)
        {
            SolarLog::template info<detail::BootLogCategory>("boot begin");
        }

        if constexpr (detail::HasInit<BoardT, ContextType>)
        {
            const Status board_status = detail::call_init(board_, ctx);
            if (board_status != Status::Ok)
            {
                return fail(BootPhase::BoardInit, board_status, "board");
            }
        }

        Status status = detail::for_each_status(peripherals_, ctx, [](auto &item, auto &context) {
            return detail::call_init(item, context);
        });
        if (status != Status::Ok)
        {
            return fail(BootPhase::PeripheralInit, status, "peripheral");
        }

        status = detail::for_each_status(peripherals_, ctx, [](auto &item, auto &context) {
            return detail::call_start(item, context);
        });
        if (status != Status::Ok)
        {
            return fail(BootPhase::PeripheralStart, status, "peripheral");
        }

        status = detail::for_each_status(facilities_, ctx, [](auto &item, auto &context) {
            return detail::call_init(item, context);
        });
        if (status != Status::Ok)
        {
            return fail(BootPhase::ServiceInit, status, "facility");
        }

        status = detail::for_each_status(facilities_, ctx, [](auto &item, auto &context) {
            return detail::call_start(item, context);
        });
        if (status != Status::Ok)
        {
            return fail(BootPhase::ServiceStart, status, "facility");
        }

        status = detail::for_each_status(devices_, ctx, [](auto &item, auto &context) {
            return detail::call_init(item, context);
        });
        if (status != Status::Ok)
        {
            return fail(BootPhase::DeviceInit, status, "device");
        }

        status = detail::for_each_status(services_, ctx, [](auto &item, auto &context) {
            return detail::call_init(item, context);
        });
        if (status != Status::Ok)
        {
            return fail(BootPhase::ServiceInit, status, "service");
        }

        status = detail::for_each_status(devices_, ctx, [](auto &item, auto &context) {
            return detail::call_start(item, context);
        });
        if (status != Status::Ok)
        {
            return fail(BootPhase::DeviceStart, status, "device");
        }

        status = start_service_threads(std::index_sequence_for<ServiceTypes...>{});
        if (status != Status::Ok)
        {
            return fail(BootPhase::ServiceStart, status, "service");
        }

        status = detail::for_each_status(tasks_, ctx, [](auto &item, auto &context) {
            return detail::call_init(item, context);
        });
        if (status != Status::Ok)
        {
            return fail(BootPhase::TaskInit, status, "task");
        }

        status = detail::for_each_status(tasks_, ctx, [](auto &item, auto &context) {
            return detail::call_start(item, context);
        });
        if (status != Status::Ok)
        {
            return fail(BootPhase::TaskStart, status, "task");
        }

        boot_report_ = {};
        boot_report_.status = Status::Ok;
        if constexpr (Config::EnableBootLogs)
        {
            SolarLog::template info<detail::BootLogCategory>("boot complete");
        }
        return Status::Ok;
    }

    /**
     * @brief Request shutdown for service threads and call service stop hooks.
     */
    Status Stop()
    {
        Status status = stop_service_threads(std::index_sequence_for<ServiceTypes...>{});
        ContextType ctx{*this};
        const Status service_status = detail::for_each_status(services_, ctx, [](auto &item, auto &context) {
            return detail::call_stop(item, context);
        });
        return status == Status::Ok ? service_status : status;
    }

    /**
     * @brief Report from the last boot attempt.
     */
    BootReport const &Report() const
    {
        return boot_report_;
    }

    /**
     * @brief Access the board object owned by this system.
     */
    BoardT &BoardObject()
    {
        return board_;
    }

    template <typename DeviceT>
    /**
     * @brief Access a device by concrete type.
     */
    DeviceT &Device()
    {
        return std::get<DeviceT>(devices_);
    }

    template <typename PeripheralT>
    /**
     * @brief Access a peripheral by concrete type.
     */
    PeripheralT &Peripheral()
    {
        return std::get<PeripheralT>(peripherals_);
    }

    template <typename ServiceT>
    /**
     * @brief Access a service by concrete type.
     */
    ServiceT &Service()
    {
        return std::get<ServiceT>(services_);
    }

    template <typename FacilityT>
    /**
     * @brief Access a facility by concrete type.
     */
    FacilityT &Facility()
    {
        return std::get<FacilityT>(facilities_);
    }

    template <typename ChannelT>
    /**
     * @brief Access a channel by concrete type.
     */
    ChannelT &Channel()
    {
        return std::get<ChannelT>(channels_);
    }

    template <typename TaskT>
    /**
     * @brief Access a task by concrete type.
     */
    TaskT &Task()
    {
        return std::get<TaskT>(tasks_);
    }

    template <typename NameT>
    /**
     * @brief Access a graph component by `solar::Name<"...">`.
     *
     * Lookup is compile-time checked across peripherals, devices, facilities,
     * services, tasks, and channels. A missing name fails compilation.
     */
    decltype(auto) Get()
    {
        using PeripheralT = detail::TypeWithNameT<NameT, PeripheralTypes...>;
        if constexpr (!std::is_void_v<PeripheralT>)
        {
            return std::get<PeripheralT>(peripherals_);
        }
        else
        {
            using DeviceT = detail::TypeWithNameT<NameT, DeviceTypes...>;
            if constexpr (!std::is_void_v<DeviceT>)
            {
                return std::get<DeviceT>(devices_);
            }
            else
            {
                using FacilityT = detail::TypeWithNameT<NameT, FacilityTypes...>;
                if constexpr (!std::is_void_v<FacilityT>)
                {
                    return std::get<FacilityT>(facilities_);
                }
                else
                {
                    using ServiceT = detail::TypeWithNameT<NameT, ServiceTypes...>;
                    if constexpr (!std::is_void_v<ServiceT>)
                    {
                        return std::get<ServiceT>(services_);
                    }
                    else
                    {
                        using TaskT = detail::TypeWithNameT<NameT, TaskTypes...>;
                        if constexpr (!std::is_void_v<TaskT>)
                        {
                            return std::get<TaskT>(tasks_);
                        }
                        else
                        {
                            using ChannelT = detail::TypeWithNameT<NameT, ChannelTypes...>;
                            static_assert(!std::is_void_v<ChannelT>, "No component with requested Solar name exists in this system");
                            return std::get<ChannelT>(channels_);
                        }
                    }
                }
            }
        }
    }

    /**
     * @brief Number of graph entries included in static snapshots.
     */
    static constexpr std::size_t SnapshotCapacity()
    {
        return PeripheralCount + DeviceCount + FacilityCount + ServiceCount + TaskCount + ChannelCount;
    }

    /**
     * @brief Return static component snapshots for Remote/inspection.
     */
    auto Snapshots() const
    {
        std::array<ComponentSnapshot, SnapshotCapacity()> snapshots{};
        std::size_t index = 0;
        ((snapshots[index++] = {PeripheralTypes::Name::c_str(), "peripheral", LifecycleState::Registered}), ...);
        ((snapshots[index++] = {DeviceTypes::Name::c_str(), "device", LifecycleState::Registered}), ...);
        ((snapshots[index++] = {FacilityTypes::Name::c_str(), "facility", LifecycleState::Registered}), ...);
        ((snapshots[index++] = {ServiceTypes::Name::c_str(), "service", LifecycleState::Registered}), ...);
        ((snapshots[index++] = {TaskTypes::Name::c_str(), "task", LifecycleState::Registered}), ...);
        ((snapshots[index++] = {ChannelTypes::Name::c_str(), "channel", LifecycleState::Registered}), ...);
        return snapshots;
    }

private:
    using SolarLog = detail::SolarInternalLog<LoggerT>;

    template <std::size_t... Indices>
    Status start_service_threads(std::index_sequence<Indices...>)
    {
        Status status = Status::Ok;
        ((status == Status::Ok
              ? status = std::get<Indices>(service_threads_).start(std::get<Indices>(services_), *this)
              : status),
         ...);
        return status;
    }

    template <std::size_t... Indices>
    Status stop_service_threads(std::index_sequence<Indices...>)
    {
        Status status = Status::Ok;
        ((status == Status::Ok
              ? status = std::get<Indices>(service_threads_).stop()
              : status),
         ...);
        return status;
    }

    Status fail(BootPhase phase, Status status, const char *component)
    {
        boot_report_.status = status;
        boot_report_.failure = {phase, status, component};
        if constexpr (Config::EnableBootLogs)
        {
            SolarLog::template error<detail::BootLogCategory>(
                "boot failed phase=%u status=%u component=%s",
                static_cast<unsigned>(phase),
                static_cast<unsigned>(status),
                component == nullptr ? "" : component);
        }
        return status;
    }

    BoardT board_{};
    std::tuple<PeripheralTypes...> peripherals_{};
    std::tuple<DeviceTypes...> devices_{};
    std::tuple<FacilityTypes...> facilities_{};
    std::tuple<ServiceTypes...> services_{};
    std::tuple<detail::ServiceThreadRuntime<ServiceTypes, ThisSystem>...> service_threads_{};
    std::tuple<TaskTypes...> tasks_{};
    std::tuple<ChannelTypes...> channels_{};
    BootReport boot_report_{};
};

template <typename SystemT>
class Context
{
public:
    using SystemType = SystemT;

    /**
     * @brief Construct a lightweight typed view over a system.
     */
    explicit Context(SystemT &system) : system_(system) {}

    /**
     * @brief Access the owning system object.
     */
    SystemT &System()
    {
        return system_;
    }

    /**
     * @brief Access the owning board.
     */
    auto &Board()
    {
        return system_.BoardObject();
    }

    template <typename NameT>
    /**
     * @brief Forwarding name lookup into the owning system.
     */
    decltype(auto) Get()
    {
        return system_.template Get<NameT>();
    }

    template <typename ServiceT>
    ServiceT &Service()
    {
        return system_.template Service<ServiceT>();
    }

    template <typename FacilityT>
    FacilityT &Facility()
    {
        return system_.template Facility<FacilityT>();
    }

    template <typename DeviceT>
    DeviceT &Device()
    {
        return system_.template Device<DeviceT>();
    }

    template <typename PeripheralT>
    PeripheralT &Peripheral()
    {
        return system_.template Peripheral<PeripheralT>();
    }

    template <typename TaskT>
    TaskT &Task()
    {
        return system_.template Task<TaskT>();
    }

private:
    SystemT &system_;
};

} // namespace solar
