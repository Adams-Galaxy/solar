#pragma once

#include <cstddef>
#include <type_traits>

#include "solar/application/fwd.hpp"
#include "solar/core/type_list.hpp"
#include "solar/execution/service_runner.hpp"
#include "solar/kernel/priority_level.hpp"
#include "solar/module.hpp"
#include "solar/parameters/store.hpp"
#include "solar/system/composer.hpp"
#include "solar/system/endpoints.hpp"

#if defined(SOLAR_HAS_GENERATED_APPLICATION)
#include <solar/generated/app.hpp>
#endif
#if defined(CONFIG_SOLAR_LOG)
#include "solar/log.hpp"
#endif
#if defined(SOLAR_HAS_GENERATED_APPLICATION) && defined(CONFIG_SOLAR_REMOTE)
#include "solar/remote.hpp"
#include <solar/generated/remote.hpp>
#endif

namespace solar
{

/** Semantic service priority levels for the high-level Application API. */
using PriorityLevel = kernel::PriorityLevel;

template <typename... Entries> struct Devices
{
    using EntriesType = TypeList<Entries...>;
};

template <std::size_t Bytes> struct Stack
{
    static constexpr std::size_t value = Bytes;
};

template <auto Value> struct Priority
{
    using ValueType = std::remove_cv_t<decltype(Value)>;
    static constexpr bool valid = [] {
        if constexpr (std::is_same_v<ValueType, kernel::PriorityLevel>) {
            return true;
        } else if constexpr (std::is_integral_v<ValueType> && !std::is_same_v<ValueType, bool>) {
            return Value >= 0;
        } else {
            return false;
        }
    }();
    static_assert(valid,
                  "SOLAR_APPLICATION_INVALID_SERVICE_PRIORITY: use a non-negative native level "
                  "or solar::kernel::PriorityLevel");
    static constexpr auto value = Value;
};

template <typename... Policies> struct Configure
{
    using Entries = TypeList<Policies...>;
};

template <std::size_t Records> struct Retain
{
    static constexpr std::size_t value = Records;
};

template <typename... Policies> struct Logging
{
    using PoliciesType = TypeList<Policies...>;
};

template <typename Logger> struct UseLogging
{
    using LoggerType = Logger;
};

template <typename Service, typename... Policies> struct Run
{
    using ServiceType = Service;
    using PoliciesType = TypeList<Policies...>;
};

template <typename... Entries> struct Services
{
    using EntriesType = TypeList<Entries...>;
};

namespace application
{
namespace detail
{

#if defined(CONFIG_SOLAR_APPLICATION_DEFAULT_SERVICE_STACK)
inline constexpr std::size_t default_service_stack = CONFIG_SOLAR_APPLICATION_DEFAULT_SERVICE_STACK;
#else
inline constexpr std::size_t default_service_stack = 2048;
#endif
#if defined(CONFIG_SOLAR_APPLICATION_DEFAULT_SERVICE_PRIORITY)
inline constexpr int default_service_priority = CONFIG_SOLAR_APPLICATION_DEFAULT_SERVICE_PRIORITY;
#else
inline constexpr int default_service_priority = 2;
#endif

template <typename Application, typename = void> struct ContractOf
{
    static_assert(GeneratedTraits<Application>::available,
                  "SOLAR_APPLICATION_MISSING_CONTRACT: provide Application::Contract or attach "
                  "a generated Solar application");
    using type = typename GeneratedTraits<Application>::Contract;
};
template <typename Application>
struct ContractOf<Application, std::void_t<typename Application::Contract>>
{
    using type = typename Application::Contract;
};

template <typename Application, typename = void> struct ParameterSchemaOf
{
    using type = std::conditional_t<GeneratedTraits<Application>::available,
                                    typename GeneratedTraits<Application>::ParameterSchema,
                                    parameters::Schema<>>;
};
template <typename Application>
struct ParameterSchemaOf<Application, std::void_t<typename Application::ParameterSchema>>
{
    using type = typename Application::ParameterSchema;
};

template <typename Application, typename = void> struct DevicesOf
{
    using type = TypeList<>;
};
template <typename Application>
struct DevicesOf<Application, std::void_t<typename Application::Devices>>
{
    using type = typename Application::Devices::EntriesType;
};

template <typename Application, typename = void> struct ServicesOf
{
    using type = TypeList<>;
};

template <typename Application, typename = void> struct ConfigurationOf
{
    using type = TypeList<>;
};
template <typename Application>
struct ConfigurationOf<Application, std::void_t<typename Application::Configuration>>
{
    using type = typename Application::Configuration::Entries;
};
template <typename Application>
struct ServicesOf<Application, std::void_t<typename Application::Services>>
{
    using type = typename Application::Services::EntriesType;
};

struct EmptyPlatform
{};
template <typename Application, typename = void> struct PlatformOf
{
    using type = EmptyPlatform;
};
template <typename Application>
struct PlatformOf<Application, std::void_t<typename Application::Platform>>
{
    using type = typename Application::Platform;
};

template <typename Platform, typename = void> struct PlatformDevicesOf
{
    using type = TypeList<>;
};
template <typename Platform>
struct PlatformDevicesOf<Platform, std::void_t<typename Platform::Devices>>
{
    using type = typename Platform::Devices::EntriesType;
};

template <typename Platform, typename = void> struct PlatformModulesOf
{
    using type = TypeList<>;
};
template <typename Platform>
struct PlatformModulesOf<Platform, std::void_t<typename Platform::Modules>>
{
    using type = typename Platform::Modules::Entries;
};

template <typename Platform, typename = void> struct PlatformConsoleOf
{
    using type = TypeList<>;
};
template <typename Platform>
struct PlatformConsoleOf<Platform, std::void_t<typename Platform::Console>>
{
    using type = TypeList<typename Platform::Console>;
};

template <typename Platform, typename = void> struct PlatformRemoteLinksOf
{
    using type = TypeList<>;
};
template <typename Platform>
struct PlatformRemoteLinksOf<Platform, std::void_t<typename Platform::RemoteLinks>>
{
    using type = typename Platform::RemoteLinks;
};

template <typename Device> struct AdaptDevice
{
    using type = std::conditional_t<Module<Device>, Device, AsModule<Device>>;
};

template <typename Entry> struct NormalizeRun
{
    using type = Run<Entry>;
};
template <typename Service, typename... Policies> struct NormalizeRun<Run<Service, Policies...>>
{
    using type = Run<Service, Policies...>;
};

template <typename Policy> struct IsStack : std::false_type
{};
template <std::size_t Bytes> struct IsStack<Stack<Bytes>> : std::true_type
{};
template <typename Policy> struct IsPriority : std::false_type
{};
template <auto Value> struct IsPriority<Priority<Value>> : std::true_type
{};

template <typename... Policies> struct PriorityPolicyOf
{
    using type = Priority<default_service_priority>;
};
template <typename Policy, typename... Policies> struct PriorityPolicyOf<Policy, Policies...>
{
    using type = std::conditional_t<IsPriority<Policy>::value, Policy,
                                    typename PriorityPolicyOf<Policies...>::type>;
};

template <typename Policies> struct RunPolicy;
template <typename... Policies> struct RunPolicy<TypeList<Policies...>>
{
    static_assert((std::size_t{0} + ... + (IsStack<Policies>::value ? 1U : 0U)) <= 1,
                  "SOLAR_APPLICATION_DUPLICATE_SERVICE_STACK: Run accepts one Stack policy");
    static_assert((std::size_t{0} + ... + (IsPriority<Policies>::value ? 1U : 0U)) <= 1,
                  "SOLAR_APPLICATION_DUPLICATE_SERVICE_PRIORITY: Run accepts one Priority policy");
    static_assert(((IsStack<Policies>::value || IsPriority<Policies>::value) && ...),
                  "SOLAR_APPLICATION_UNSUPPORTED_SERVICE_POLICY: unsupported Run policy");

    static constexpr std::size_t stack = [] {
        std::size_t value = default_service_stack;
        (([&] {
             if constexpr (IsStack<Policies>::value)
                 value = Policies::value;
         }()),
         ...);
        return value;
    }();
    using PriorityPolicy = typename PriorityPolicyOf<Policies...>::type;
    static constexpr auto priority = PriorityPolicy::value;
};

template <typename Service, typename = void> struct ServiceDependencies
{
    using type = TypeList<>;
};
template <typename Service>
struct ServiceDependencies<Service, std::void_t<typename Service::Dependencies>>
{
    using type = transform_t<typename Service::Dependencies, AdaptDevice>;
};

template <typename Application, typename RunType> struct RunnerOf;
template <typename Application, typename Service, typename... Policies>
struct RunnerOf<Application, Run<Service, Policies...>>
{
    using Configuration = RunPolicy<TypeList<Policies...>>;
    using type = execution::ServiceRunner<Application, Service, Configuration::stack,
                                          Configuration::priority,
                                          typename ServiceDependencies<Service>::type>;
};

template <typename RunType> struct ServiceOf;
template <typename Service, typename... Policies> struct ServiceOf<Run<Service, Policies...>>
{
    using type = Service;
};

template <typename Application> struct MakeRunner
{
    template <typename RunType> struct Apply : RunnerOf<Application, RunType>
    {};
};

template <typename Schema, typename Application,
          bool Empty = (list_size_v<typename Schema::Entries> == 0)>
struct ParameterModules;
template <typename Schema, typename Application> struct ParameterModules<Schema, Application, true>
{
    using type = TypeList<>;
    using Store = void;
};
template <typename Schema, typename Application> struct ParameterModules<Schema, Application, false>
{
    using Store = parameters::StaticStore<Application, Schema>;
    using type = TypeList<Store>;
};

template <typename List> struct AsOwn;
template <typename... Entries> struct AsOwn<TypeList<Entries...>>
{
    using type = Own<Entries...>;
};
template <typename List> struct AsComponents;
template <typename... Entries> struct AsComponents<TypeList<Entries...>>
{
    using type = Components<Entries...>;
};

#if defined(CONFIG_SOLAR_LOG)
using BuiltinLogDomains =
    TypeList<log::domain::Unclassified, log::domain::Lifecycle, log::domain::Transport,
             log::domain::Communication, log::domain::Storage, log::domain::Control,
             log::domain::Device, log::domain::Scheduling, log::domain::Security,
             log::domain::Resources>;
#if defined(CONFIG_SOLAR_APPLICATION_LOG_HISTORY_CAPACITY)
inline constexpr std::size_t default_log_history = CONFIG_SOLAR_APPLICATION_LOG_HISTORY_CAPACITY;
#else
inline constexpr std::size_t default_log_history = 24;
#endif

template <typename Policy> struct IsLoggingPolicy : std::false_type
{};
template <typename... Policies> struct IsLoggingPolicy<Logging<Policies...>> : std::true_type
{};
template <typename Logger> struct IsLoggingPolicy<UseLogging<Logger>> : std::true_type
{};

template <typename Policies> struct LoggingPolicyOf;
template <typename... Policies> struct LoggingPolicyOf<TypeList<Policies...>>
{
    static_assert((std::size_t{0} + ... + (IsLoggingPolicy<Policies>::value ? 1U : 0U)) <= 1,
                  "SOLAR_APPLICATION_DUPLICATE_LOGGING_POLICY: Configure accepts one logging "
                  "policy");

  private:
    template <typename Result, typename... Remaining> struct Find
    {
        using type = Result;
    };
    template <typename Result, typename Head, typename... Tail> struct Find<Result, Head, Tail...>
    {
        using Next = std::conditional_t<IsLoggingPolicy<Head>::value, Head, Result>;
        using type = typename Find<Next, Tail...>::type;
    };

  public:
    using type = typename Find<void, Policies...>::type;
};

template <typename Policy> struct LoggingCapacity;
template <> struct LoggingCapacity<void> : std::integral_constant<std::size_t, default_log_history>
{};
template <std::size_t Records>
struct LoggingCapacity<Logging<Retain<Records>>> : std::integral_constant<std::size_t, Records>
{
    static_assert(Records > 0, "SOLAR_APPLICATION_INVALID_LOG_RETENTION: Retain must be non-zero");
};

template <typename Application, typename Sources, typename Platform, typename Policy>
struct LoggingSynthesis
{
    using Module = log::StaticLogger<Application, Sources, BuiltinLogDomains,
                                     typename PlatformConsoleOf<Platform>::type,
                                     LoggingCapacity<Policy>::value>;
    using Modules = TypeList<Module>;
};
template <typename Application, typename Sources, typename Platform, typename Logger>
struct LoggingSynthesis<Application, Sources, Platform, UseLogging<Logger>>
{
    static_assert(Module<Logger>,
                  "SOLAR_APPLICATION_INVALID_LOGGER_OVERRIDE: UseLogging requires a Solar "
                  "lifecycle module");
    using Module = Logger;
    using Modules = TypeList<Logger>;
};
#else
template <typename Policies> struct LoggingPolicyOf
{
    using type = void;
};
template <typename Application, typename Sources, typename Platform, typename Policy>
struct LoggingSynthesis
{
    using Module = void;
    using Modules = TypeList<>;
};
#endif

#if defined(CONFIG_SOLAR_REMOTE) && defined(SOLAR_HAS_GENERATED_APPLICATION)
template <typename Application, typename Contract, typename Parameters, typename Components,
          typename Dependencies, typename Platform>
struct RemoteSynthesis
{
    static_assert(GeneratedRemoteTraits<Application>::available,
                  "SOLAR_APPLICATION_MISSING_REMOTE_BINDINGS: regenerate the application with "
                  "Remote support");
    using Links = typename PlatformRemoteLinksOf<Platform>::type;
    static_assert(list_size_v<Links> != 0,
                  "SOLAR_APPLICATION_MISSING_REMOTE_LINKS: Platform::RemoteLinks is required "
                  "when the generated application requests Remote");
    using Dispatch = system::Dispatch<Contract, Components>;
    using GeneratedContract =
        typename GeneratedRemoteTraits<Application>::template Contract<Parameters, Dispatch>;
    using Architecture =
        remote::Architecture<typename GeneratedContract::RemoteSchemas::Entries,
                             typename GeneratedContract::RemoteData::Entries,
                             typename GeneratedContract::RemoteActions::Entries, TypeList<>,
                             typename GeneratedContract::RemoteStreams::Entries, Links, Components,
                             TypeList<>>;
    using Runtime = remote::ByteRuntime<Application, Architecture, Dependencies>;
    using Modules = TypeList<Runtime>;
};

template <bool Enabled, typename Application, typename Contract, typename Parameters,
          typename Components, typename Dependencies, typename Platform>
struct RemoteSelection
{
    using Runtime = void;
    using Modules = TypeList<>;
};
template <typename Application, typename Contract, typename Parameters, typename Components,
          typename Dependencies, typename Platform>
struct RemoteSelection<true, Application, Contract, Parameters, Components, Dependencies, Platform>
    : RemoteSynthesis<Application, Contract, Parameters, Components, Dependencies, Platform>
{};
#endif

} // namespace detail

/** Fully normalized, inspectable high-level application specification. */
template <typename Application> struct Specification
{
    using Generated = GeneratedTraits<Application>;
    using Contract = typename detail::ContractOf<Application>::type;
    using ParameterSchema = typename detail::ParameterSchemaOf<Application>::type;
    using Parameters = detail::ParameterModules<ParameterSchema, Application>;
    using ParameterStore = typename Parameters::Store;
    using Platform = typename detail::PlatformOf<Application>::type;
    using DeclaredDevices = concat_t<typename detail::PlatformDevicesOf<Platform>::type,
                                     typename detail::DevicesOf<Application>::type>;
    using DeviceModules = transform_t<DeclaredDevices, detail::AdaptDevice>;
    using DeclaredServices = typename detail::ServicesOf<Application>::type;
    using Configuration = typename detail::ConfigurationOf<Application>::type;
    using Runs = transform_t<DeclaredServices, detail::NormalizeRun>;
    using ServiceTypes = transform_t<Runs, detail::ServiceOf>;
    using ServiceRunners = transform_t<Runs, detail::MakeRunner<Application>::template Apply>;
    using PlatformModules = typename detail::PlatformModulesOf<Platform>::type;
    using LogSources = unique_t<concat_t<DeclaredDevices, ServiceTypes>>;
    using LoggingPolicy = typename detail::LoggingPolicyOf<Configuration>::type;
    using Logging = detail::LoggingSynthesis<Application, LogSources, Platform, LoggingPolicy>;
    using Logger = typename Logging::Module;
    using BeforeRemote = concat_t<typename Logging::Modules, typename Parameters::type,
                                  PlatformModules, DeviceModules, ServiceRunners>;
#if defined(CONFIG_SOLAR_REMOTE) && defined(SOLAR_HAS_GENERATED_APPLICATION)
    static_assert(!Generated::requires_remote || GeneratedRemoteTraits<Application>::available,
                  "SOLAR_APPLICATION_MISSING_REMOTE_BINDINGS");
    using Remote = detail::RemoteSelection<Generated::requires_remote, Application, Contract,
                                           ParameterStore, ServiceTypes, BeforeRemote, Platform>;
    using RemoteRuntime = typename Remote::Runtime;
    using RemoteModules = typename Remote::Modules;
#else
#if defined(__ZEPHYR__)
    static_assert(!Generated::requires_remote,
                  "SOLAR_APPLICATION_REMOTE_DISABLED: generated project requires "
                  "CONFIG_SOLAR_REMOTE");
#endif
    using RemoteRuntime = void;
    using RemoteModules = TypeList<>;
#endif
    using OwnedModules = concat_t<BeforeRemote, RemoteModules>;
    using Owned = typename detail::AsOwn<OwnedModules>::type;
    using Components = typename detail::AsComponents<ServiceTypes>::type;
    using Composition = Compose<solar::Contract<Contract>, Owned, Components>;
};

/** Canonical public views of a normalized high-level application. */
template <typename Application>
using Composition = typename Specification<Application>::Composition;
template <typename Application>
using Parameters = typename Specification<Application>::ParameterStore;
template <typename Application> using Logger = typename Specification<Application>::Logger;
template <typename Application>
using RemoteRuntime = typename Specification<Application>::RemoteRuntime;

// Conventional metaprogramming spellings remain available to advanced code.
template <typename Application> using specification_t = Specification<Application>;
template <typename Application> using composition_t = Composition<Application>;
template <typename Application> using parameters_t = Parameters<Application>;

} // namespace application

namespace log
{
template <typename Application> struct For
{
#if defined(CONFIG_SOLAR_LOG)
#define SOLAR_APPLICATION_LOG_FORWARD(NAME)                                                        \
    template <typename Source, typename Domain = domain::Unclassified, typename... Arguments>      \
    [[nodiscard]] static Result<Receipt, Error> NAME(                                              \
        FormatString<std::type_identity_t<Arguments>...> format,                                   \
        Arguments&&... arguments) noexcept                                                         \
    {                                                                                              \
        using Backend = application::Logger<Application>;                                          \
        return Backend::template NAME<Source, Domain>(format,                                      \
                                                      std::forward<Arguments>(arguments)...);      \
    }
    SOLAR_APPLICATION_LOG_FORWARD(trace)
    SOLAR_APPLICATION_LOG_FORWARD(debug)
    SOLAR_APPLICATION_LOG_FORWARD(info)
    SOLAR_APPLICATION_LOG_FORWARD(notice)
    SOLAR_APPLICATION_LOG_FORWARD(warn)
    SOLAR_APPLICATION_LOG_FORWARD(error)
#undef SOLAR_APPLICATION_LOG_FORWARD
#endif
};
} // namespace log

/** Canonical single static System for a high-level Solar application. */
template <typename Application>
struct System : system::System<Application, application::Composition<Application>>
{};

} // namespace solar
