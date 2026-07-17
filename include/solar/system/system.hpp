#pragma once

#include <type_traits>

#include "solar/system/blueprint.hpp"

namespace solar
{

struct DefaultApplication;

namespace lifecycle
{
template <typename System> struct Engine;
template <typename System, typename Application> struct ApplicationProtocol;
} // namespace lifecycle

template <typename SystemT, typename Owner, typename Key, typename State> struct StaticStateSlot
{
    inline static State value{};
};

/** Static application System derived from a Blueprint.
 *
 * `System` is a type and is never instantiated. Its nested catalogs, graph,
 * facilities, services, and state slots identify canonical static owners.
 *
 * @tparam BlueprintT Valid compile-time application Blueprint.
 */
template <typename BlueprintT> struct System
{
    using Blueprint = BlueprintT;
    using Effective = effective_blueprint_t<BlueprintT>;
    using Components = typename Effective::Components;
    using Graph = typename Effective::Graph;
    using Catalogs = typename Effective::Catalogs;
    using ExecutionRegistrations = typename Effective::ExecutionRegistrations;
    using ExecutionCatalog = typename Effective::ExecutionCatalog;
    using EffectiveExecutionRegistrations = typename Effective::EffectiveExecutionRegistrations;
    using BusMessageCatalog = typename Effective::BusMessageCatalog;
    using BusSubscriptionCatalog = typename Effective::BusSubscriptionCatalog;
    using BusFacility = typename Effective::BusFacility;
    using ParameterCatalog = typename Effective::ParameterCatalog;
    using ParameterChangeCatalog = typename Effective::ParameterChangeCatalog;
    using ParameterArchitecture = typename Effective::ParameterArchitecture;
    using ParameterFacility = typename Effective::ParameterFacility;
    using EventCatalog = typename Effective::EventCatalog;
    using EventProcessorCatalog = typename Effective::EventProcessorCatalog;
    using EventArchitecture = typename Effective::EventArchitecture;
    using EventFacility = typename Effective::EventFacility;
    using MetricCatalog = typename Effective::MetricCatalog;
    using MetricArchitecture = typename Effective::MetricArchitecture;
    using MetricFacility = typename Effective::MetricFacility;
    using InspectionCatalog = typename Effective::InspectionCatalog;
    using InspectionFacility = typename Effective::InspectionFacility;
    using HealthMonitorCatalog = typename Effective::HealthMonitorCatalog;
    using HealthFacility = typename Effective::HealthFacility;
    using LogSourceCatalog = typename Effective::LogSourceCatalog;
    using LogDomainCatalog = typename Effective::LogDomainCatalog;
    using LogArchitecture = typename Effective::LogArchitecture;
    using LogFacility = typename Effective::LogFacility;
    using RemoteSchemaCatalog = typename Effective::RemoteSchemaCatalog;
    using RemoteDataCatalog = typename Effective::RemoteDataCatalog;
    using RemoteActionCatalog = typename Effective::RemoteActionCatalog;
    using RemoteTopicCatalog = typename Effective::RemoteTopicCatalog;
    using RemoteStreamCatalog = typename Effective::RemoteStreamCatalog;
    using RemoteLinkCatalog = typename Effective::RemoteLinkCatalog;
    using RemoteArchitecture = typename Effective::RemoteArchitecture;
    using RemoteFacility = typename Effective::RemoteFacility;
    using RemoteService = typename Effective::RemoteService;
    using SupervisorArchitecture = typename Effective::SupervisorArchitecture;
    using SupervisorService = typename Effective::SupervisorService;
    using ConfigurationSections = typename Effective::ConfigurationSections;
    using Builtins = typename Effective::EffectiveBuiltins;

    static constexpr log::Level log_global_compile_level = [] {
        using Global =
            typename Effective::template ConfigurationPolicy<log::Tag, log::CompileLevelAxis>;
        return log::detail::stricter(log::detail::kconfig_compile_level,
                                     log::detail::policy_level<Global>);
    }();

    template <typename Source>
    static constexpr log::Level log_source_compile_level = [] {
        using PerSource =
            typename Effective::template ConfigurationPolicy<log::Tag,
                                                             log::SourceLevelAxis<Source>>;
        return log::detail::stricter(log_global_compile_level,
                                     log::detail::policy_level<PerSource>);
    }();

    template <typename Domain>
    static constexpr log::Level log_domain_compile_level = [] {
        using PerDomain =
            typename Effective::template ConfigurationPolicy<log::Tag,
                                                             log::DomainLevelAxis<Domain>>;
        return log::detail::stricter(log_global_compile_level,
                                     log::detail::policy_level<PerDomain>);
    }();

    template <typename Source, typename Domain = log::domain::Unclassified>
    static constexpr log::Level log_compile_level = [] {
        return log::detail::stricter(log_source_compile_level<Source>,
                                     log_domain_compile_level<Domain>);
    }();

    using SolarSystemMarker = void;
    static constexpr bool valid = Effective::valid;

    template <typename Application = DefaultApplication> [[nodiscard]] static auto boot() noexcept
    {
#if defined(CONFIG_SOLAR_SUPERVISOR)
        SupervisorService::template bind<System>();
#endif
        lifecycle::ApplicationProtocol<System, Application>::bind();
        return lifecycle::Engine<System>::boot();
    }

    [[nodiscard]] static auto stop() noexcept
    {
        return lifecycle::Engine<System>::stop();
    }

    struct catalog
    {
        template <typename Tag> using Of = typename Catalogs::template Of<Tag>;

        [[nodiscard]] static constexpr auto components() noexcept
        {
            return Of<component::Tag>::descriptors();
        }
    };

    struct graph
    {
        using Components = typename Effective::Components;

        template <typename Component>
        using Dependencies = typename Graph::template DependenciesOf<Component>;

        template <typename Component>
        using Category = typename Effective::template CategoryOf<Component>;
    };

    struct configuration
    {
        template <typename SubsystemTag>
        using Of = typename Effective::template Configuration<SubsystemTag>;

        template <typename SubsystemTag, typename Axis>
        using Policy = typename Effective::template ConfigurationPolicy<SubsystemTag, Axis>;

        template <typename SubsystemTag, typename Axis, typename DeclarationPolicy,
                  typename KconfigPolicy>
        using EffectivePolicy =
            resolve_policy_t<DeclarationPolicy, Policy<SubsystemTag, Axis>, KconfigPolicy>;
    };

    template <typename Owner, typename Key, typename State>
    using StateSlot = StaticStateSlot<System<BlueprintT>, Owner, Key, State>;
};

template <typename T>
concept SystemType = requires {
    typename T::SolarSystemMarker;
    typename T::Blueprint;
    typename T::Effective;
    { T::valid } -> std::convertible_to<bool>;
};

} // namespace solar
