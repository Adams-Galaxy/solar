#pragma once

#include <array>
#include <concepts>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "solar/core/status.hpp"
#include "solar/core/type_list.hpp"
#include "solar/module.hpp"
#include "solar/system/contributions.hpp"

namespace solar
{

template <typename... Modules> struct Own
{
    using Entries = TypeList<Modules...>;
};

template <typename Adapter, typename... Endpoints> struct Connect
{
    using AdapterType = Adapter;
    using EndpointTypes = TypeList<Endpoints...>;
};

template <typename... ComponentTypes> struct Components
{
    using Entries = TypeList<ComponentTypes...>;
};

template <typename ContractT> struct Contract
{
    using Type = ContractT;
};

template <typename... Sections> struct Compose
{
    using SectionTypes = TypeList<Sections...>;
};

namespace system
{
enum class LifecycleStage : std::uint8_t
{
    Idle,
    Initializing,
    Connecting,
    Starting,
    Active,
    Stopping,
    Disconnecting,
    Deinitializing,
    Failed,
};

namespace detail
{

template <typename> inline constexpr bool dependent_false_v = false;

template <typename Section> struct OwnedFrom
{
    using type = TypeList<>;
};

template <typename... Modules> struct OwnedFrom<Own<Modules...>>
{
    using type = TypeList<Modules...>;
};

template <typename Section> struct ConnectionsFrom
{
    using type = TypeList<>;
};

template <typename Adapter, typename... Endpoints>
struct ConnectionsFrom<Connect<Adapter, Endpoints...>>
{
    using type = TypeList<Connect<Adapter, Endpoints...>>;
};

template <typename Section> struct ComponentsFrom
{
    using type = TypeList<>;
};

template <typename... Entries> struct ComponentsFrom<Components<Entries...>>
{
    using type = TypeList<Entries...>;
};

template <typename Section> struct ContractFrom
{
    using type = TypeList<>;
};

template <typename ContractT> struct ContractFrom<Contract<ContractT>>
{
    using type = TypeList<ContractT>;
};

template <typename Composition> struct CompositionTraits;

template <typename... Sections> struct CompositionTraits<Compose<Sections...>>
{
    using Modules = concat_t<typename OwnedFrom<Sections>::type...>;
    using Connections = concat_t<typename ConnectionsFrom<Sections>::type...>;
    using ComponentTypes = concat_t<typename ComponentsFrom<Sections>::type...>;
    using Contracts = concat_t<typename ContractFrom<Sections>::type...>;
};

template <typename Contracts, typename ComponentTypes> struct ValidateContracts;

template <typename ComponentTypes>
struct ValidateContracts<TypeList<>, ComponentTypes> : std::true_type
{};

template <typename ContractT, typename ComponentTypes>
struct ValidateContracts<TypeList<ContractT>, ComponentTypes>
    : std::bool_constant<Participation<ContractT, ComponentTypes>::value>
{};

template <typename First, typename Second, typename... Rest, typename ComponentTypes>
struct ValidateContracts<TypeList<First, Second, Rest...>, ComponentTypes>
{
    static_assert(dependent_false_v<First>,
                  "SOLAR_SYSTEM_MULTIPLE_CONTRACTS: one application System accepts one "
                  "generated contract");
    static constexpr bool value = false;
};

template <typename Module, typename = void> struct ModuleDependencies
{
    using type = TypeList<>;
};

template <typename Module>
struct ModuleDependencies<Module, std::void_t<typename Module::Dependencies>>
{
    using type = typename Module::Dependencies;
    static_assert(TypeListType<type>,
                  "SOLAR_SYSTEM_MALFORMED_DEPENDENCIES: Dependencies must be a TypeList");
};

template <typename Dependencies, typename Modules> struct DependenciesPresent;

template <typename... Dependencies, typename Modules>
struct DependenciesPresent<TypeList<Dependencies...>, Modules>
    : std::bool_constant<(contains_v<Dependencies, Modules> && ...)>
{};

template <typename Module, typename = void> struct ModuleIdentity
{
    static constexpr std::string_view name{};
};

template <typename Module> struct ModuleIdentity<Module, std::void_t<decltype(Module::name)>>
{
    static constexpr std::string_view name = Module::name;
};

template <typename... Modules> consteval bool unique_module_identities(TypeList<Modules...>)
{
    constexpr std::array<std::string_view, sizeof...(Modules)> names{
        ModuleIdentity<Modules>::name...};
    for (std::size_t left{}; left < names.size(); ++left) {
        if (names[left].empty())
            continue;
        for (std::size_t right = left + 1; right < names.size(); ++right) {
            if (names[left] == names[right])
                return false;
        }
    }
    return true;
}

template <typename Modules> struct ValidateModules;

template <typename... Modules> struct ValidateModules<TypeList<Modules...>>
{
    using All = TypeList<Modules...>;
    static_assert(unique_types_v<All>,
                  "SOLAR_SYSTEM_DUPLICATE_MODULE_OWNERSHIP: a module may be owned once");
    static_assert(unique_module_identities(All{}),
                  "SOLAR_SYSTEM_CONFLICTING_MODULE_IDENTITY: owned modules share an identity");
    static_assert((DependenciesPresent<typename ModuleDependencies<Modules>::type, All>::value &&
                   ...),
                  "SOLAR_SYSTEM_MISSING_MODULE_DEPENDENCY: an owned dependency is absent");
    static constexpr bool valid = true;
};

template <typename Module, typename All, typename Path, bool Cycle> struct Visit;

template <typename Module, typename All, typename Path> struct Visit<Module, All, Path, true>
{
    static_assert(dependent_false_v<Module>,
                  "SOLAR_SYSTEM_MODULE_DEPENDENCY_CYCLE: module dependency graph contains a "
                  "cycle");
    static constexpr bool value = false;
};

template <typename Dependencies, typename All, typename Path> struct VisitDependencies;

template <typename All, typename Path>
struct VisitDependencies<TypeList<>, All, Path> : std::true_type
{};

template <typename Head, typename... Tail, typename All, typename Path>
struct VisitDependencies<TypeList<Head, Tail...>, All, Path>
    : std::bool_constant<Visit<Head, All, Path, contains_v<Head, Path>>::value &&
                         VisitDependencies<TypeList<Tail...>, All, Path>::value>
{};

template <typename Module, typename All, typename Path>
struct Visit<Module, All, Path, false>
    : VisitDependencies<typename ModuleDependencies<Module>::type, All,
                        concat_t<Path, TypeList<Module>>>
{};

template <typename Modules> struct ValidateCycles;

template <typename... Modules>
struct ValidateCycles<TypeList<Modules...>>
    : std::bool_constant<(Visit<Modules, TypeList<Modules...>, TypeList<>, false>::value && ...)>
{};

template <typename Dependencies, typename Sorted> struct DependenciesSatisfied;

template <typename... Dependencies, typename Sorted>
struct DependenciesSatisfied<TypeList<Dependencies...>, Sorted>
    : std::bool_constant<(contains_v<Dependencies, Sorted> && ...)>
{};

template <typename Remaining, typename Sorted> struct FirstReady;

template <bool Ready, typename Head, typename Tail, typename Sorted> struct FirstReadyStep;

template <typename Head, typename Tail, typename Sorted>
struct FirstReadyStep<true, Head, Tail, Sorted>
{
    using type = Head;
};

template <typename Head, typename Tail, typename Sorted>
struct FirstReadyStep<false, Head, Tail, Sorted> : FirstReady<Tail, Sorted>
{};

template <typename Head, typename... Tail, typename Sorted>
struct FirstReady<TypeList<Head, Tail...>, Sorted>
    : FirstReadyStep<DependenciesSatisfied<typename ModuleDependencies<Head>::type, Sorted>::value,
                     Head, TypeList<Tail...>, Sorted>
{};

template <typename Sorted> struct FirstReady<TypeList<>, Sorted>
{
    static_assert(dependent_false_v<Sorted>,
                  "SOLAR_SYSTEM_MODULE_DEPENDENCY_CYCLE: no module is ready");
};

template <typename Needle, typename List> struct Remove;

template <typename Needle> struct Remove<Needle, TypeList<>>
{
    using type = TypeList<>;
};

template <typename Needle, typename Head, typename... Tail>
struct Remove<Needle, TypeList<Head, Tail...>>
{
    using type = std::conditional_t<
        std::is_same_v<Needle, Head>, TypeList<Tail...>,
        concat_t<TypeList<Head>, typename Remove<Needle, TypeList<Tail...>>::type>>;
};

template <typename Remaining, typename Sorted> struct Sort;

template <typename Sorted> struct Sort<TypeList<>, Sorted>
{
    using type = Sorted;
};

template <typename... Remaining, typename Sorted> struct Sort<TypeList<Remaining...>, Sorted>
{
    using Ready = typename FirstReady<TypeList<Remaining...>, Sorted>::type;
    using Rest = typename Remove<Ready, TypeList<Remaining...>>::type;
    using type = typename Sort<Rest, concat_t<Sorted, TypeList<Ready>>>::type;
};

template <typename List> struct Reverse;

template <> struct Reverse<TypeList<>>
{
    using type = TypeList<>;
};

template <typename Head, typename... Tail> struct Reverse<TypeList<Head, Tail...>>
{
    using type = concat_t<typename Reverse<TypeList<Tail...>>::type, TypeList<Head>>;
};

template <typename Module, typename = void> struct CatalogsFrom
{
    using type = TypeList<>;
};
template <typename Module> struct CatalogsFrom<Module, std::void_t<typename Module::Catalog>>
{
    using type = typename Module::Catalog;
};

template <typename Module, typename = void> struct CapabilitiesFrom
{
    using type = TypeList<>;
};
template <typename Module>
struct CapabilitiesFrom<Module, std::void_t<typename Module::Capabilities>>
{
    using type = typename Module::Capabilities;
};

template <typename Module, typename = void> struct InspectionFrom
{
    using type = TypeList<>;
};
template <typename Module> struct InspectionFrom<Module, std::void_t<typename Module::Inspection>>
{
    using type = typename Module::Inspection;
};

template <typename Modules> struct FoldModuleMetadata;
template <typename... Modules> struct FoldModuleMetadata<TypeList<Modules...>>
{
    static_assert((TypeListType<typename CatalogsFrom<Modules>::type> && ...),
                  "SOLAR_SYSTEM_INVALID_MODULE_CATALOG: Catalog must be a TypeList");
    static_assert((TypeListType<typename CapabilitiesFrom<Modules>::type> && ...),
                  "SOLAR_SYSTEM_INVALID_MODULE_CAPABILITIES: Capabilities must be a TypeList");
    static_assert((TypeListType<typename InspectionFrom<Modules>::type> && ...),
                  "SOLAR_SYSTEM_INVALID_MODULE_INSPECTION: Inspection must be a TypeList");
    using Catalogs = unique_t<concat_t<typename CatalogsFrom<Modules>::type...>>;
    using Capabilities = unique_t<concat_t<typename CapabilitiesFrom<Modules>::type...>>;
    using Inspection = unique_t<concat_t<typename InspectionFrom<Modules>::type...>>;
};

template <typename Module> [[nodiscard]] Result<void> initialize()
{
    if constexpr (requires {
                      { Module::initialize() } -> std::same_as<Result<void>>;
                  }) {
        return Module::initialize();
    }
    return {};
}

template <typename Module> [[nodiscard]] Result<void> start()
{
    if constexpr (requires {
                      { Module::start() } -> std::same_as<Result<void>>;
                  }) {
        return Module::start();
    }
    return {};
}

template <typename Module> [[nodiscard]] Result<void> stop()
{
    if constexpr (requires {
                      { Module::stop() } -> std::same_as<Result<void>>;
                  }) {
        return Module::stop();
    }
    return {};
}

template <typename Module> [[nodiscard]] Result<void> deinitialize()
{
    if constexpr (requires {
                      { Module::deinitialize() } -> std::same_as<Result<void>>;
                  }) {
        return Module::deinitialize();
    }
    return {};
}

template <typename List> struct ModuleLifecycle;

template <> struct ModuleLifecycle<TypeList<>>
{
    [[nodiscard]] static Result<void> initialize_all()
    {
        return {};
    }
    [[nodiscard]] static Result<void> start_all()
    {
        return {};
    }
    [[nodiscard]] static Result<void> stop_all()
    {
        return {};
    }
    [[nodiscard]] static Result<void> deinitialize_all()
    {
        return {};
    }
};

template <typename Head, typename... Tail> struct ModuleLifecycle<TypeList<Head, Tail...>>
{
    using Rest = ModuleLifecycle<TypeList<Tail...>>;

    [[nodiscard]] static Result<void> initialize_all()
    {
        if (auto result = initialize<Head>(); !result) {
            return result;
        }
        if (auto result = Rest::initialize_all(); !result) {
            (void)deinitialize<Head>();
            return result;
        }
        return {};
    }

    [[nodiscard]] static Result<void> start_all()
    {
        if (auto result = start<Head>(); !result) {
            return result;
        }
        if (auto result = Rest::start_all(); !result) {
            (void)stop<Head>();
            return result;
        }
        return {};
    }

    [[nodiscard]] static Result<void> stop_all()
    {
        auto result = stop<Head>();
        auto rest = Rest::stop_all();
        return result ? rest : result;
    }

    [[nodiscard]] static Result<void> deinitialize_all()
    {
        auto result = deinitialize<Head>();
        auto rest = Rest::deinitialize_all();
        return result ? rest : result;
    }
};

template <typename Connection> struct ConnectionLifecycle;

template <typename Adapter, typename... Endpoints>
struct ConnectionLifecycle<Connect<Adapter, Endpoints...>>
{
    [[nodiscard]] static Result<void> connect()
    {
        static_assert(
            requires {
                { Adapter::template connect<Endpoints...>() } -> std::same_as<Result<void>>;
            }, "SOLAR_SYSTEM_INVALID_CONNECT_ADAPTER: adapter must expose connect<Endpoint...>()");
        return Adapter::template connect<Endpoints...>();
    }

    [[nodiscard]] static Result<void> disconnect()
    {
        if constexpr (requires {
                          {
                              Adapter::template disconnect<Endpoints...>()
                          } -> std::same_as<Result<void>>;
                      }) {
            return Adapter::template disconnect<Endpoints...>();
        }
        return {};
    }
};

template <typename List> struct ConnectionsLifecycle;

template <> struct ConnectionsLifecycle<TypeList<>>
{
    [[nodiscard]] static Result<void> connect_all()
    {
        return {};
    }
    [[nodiscard]] static Result<void> disconnect_all()
    {
        return {};
    }
};

template <typename Head, typename... Tail> struct ConnectionsLifecycle<TypeList<Head, Tail...>>
{
    using Rest = ConnectionsLifecycle<TypeList<Tail...>>;

    [[nodiscard]] static Result<void> connect_all()
    {
        if (auto result = ConnectionLifecycle<Head>::connect(); !result) {
            return result;
        }
        if (auto result = Rest::connect_all(); !result) {
            (void)ConnectionLifecycle<Head>::disconnect();
            return result;
        }
        return {};
    }

    [[nodiscard]] static Result<void> disconnect_all()
    {
        auto result = ConnectionLifecycle<Head>::disconnect();
        auto rest = Rest::disconnect_all();
        return result ? rest : result;
    }
};

} // namespace detail

/** One non-instantiated, application-owned, generic System composer. */
template <typename Application, typename Composition> struct System
{
    using ApplicationType = Application;
    using Traits = detail::CompositionTraits<Composition>;
    using Modules = typename Traits::Modules;
    using Connections = typename Traits::Connections;
    using ComponentTypes = typename Traits::ComponentTypes;
    using Contracts = typename Traits::Contracts;

    static_assert(detail::ValidateModules<Modules>::valid);
    static_assert(detail::ValidateCycles<Modules>::value);
    static_assert(detail::ValidateContracts<Contracts, ComponentTypes>::value);

    using ModuleOrder = typename detail::Sort<Modules, TypeList<>>::type;
    using ReverseModuleOrder = typename detail::Reverse<ModuleOrder>::type;
    using ModuleMetadata = detail::FoldModuleMetadata<Modules>;
    using Catalogs = typename ModuleMetadata::Catalogs;
    using Capabilities = typename ModuleMetadata::Capabilities;
    using Inspection = typename ModuleMetadata::Inspection;
    using ConnectionOrder = Connections;
    using ReverseConnectionOrder = typename detail::Reverse<Connections>::type;

    System() = delete;

    [[nodiscard]] static Result<void> boot()
    {
        if (active_) {
            return fail<Error>({.status = Status::Already});
        }
        stage_ = LifecycleStage::Initializing;
        if (auto result = detail::ModuleLifecycle<ModuleOrder>::initialize_all(); !result) {
            stage_ = LifecycleStage::Failed;
            return result;
        }
        stage_ = LifecycleStage::Connecting;
        if (auto result = detail::ConnectionsLifecycle<ConnectionOrder>::connect_all(); !result) {
            (void)detail::ModuleLifecycle<ReverseModuleOrder>::deinitialize_all();
            stage_ = LifecycleStage::Failed;
            return result;
        }
        stage_ = LifecycleStage::Starting;
        if (auto result = detail::ModuleLifecycle<ModuleOrder>::start_all(); !result) {
            (void)detail::ConnectionsLifecycle<ReverseConnectionOrder>::disconnect_all();
            (void)detail::ModuleLifecycle<ReverseModuleOrder>::deinitialize_all();
            stage_ = LifecycleStage::Failed;
            return result;
        }
        active_ = true;
        stage_ = LifecycleStage::Active;
        return {};
    }

    [[nodiscard]] static Result<void> shutdown()
    {
        if (!active_) {
            return {};
        }
        stage_ = LifecycleStage::Stopping;
        auto stopped = detail::ModuleLifecycle<ReverseModuleOrder>::stop_all();
        stage_ = LifecycleStage::Disconnecting;
        auto disconnected = detail::ConnectionsLifecycle<ReverseConnectionOrder>::disconnect_all();
        stage_ = LifecycleStage::Deinitializing;
        auto deinitialized = detail::ModuleLifecycle<ReverseModuleOrder>::deinitialize_all();
        active_ = false;
        stage_ = (!stopped || !disconnected || !deinitialized) ? LifecycleStage::Failed
                                                               : LifecycleStage::Idle;
        if (!stopped) {
            return stopped;
        }
        if (!disconnected) {
            return disconnected;
        }
        return deinitialized;
    }

    [[nodiscard]] static bool active()
    {
        return active_;
    }
    [[nodiscard]] static LifecycleStage stage()
    {
        return stage_;
    }

  private:
    inline static bool active_{};
    inline static LifecycleStage stage_{LifecycleStage::Idle};
};

} // namespace system
} // namespace solar
