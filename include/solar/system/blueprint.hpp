#pragma once

#include <type_traits>

#include "solar/bus/facility.hpp"
#include "solar/events/facility.hpp"
#include "solar/execution/contribution.hpp"
#include "solar/health/facility.hpp"
#include "solar/inspection/collections.hpp"
#include "solar/inspection/facility.hpp"
#include "solar/log/facility.hpp"
#include "solar/metrics/facility.hpp"
#include "solar/parameters/facility.hpp"
#include "solar/remote/service.hpp"
#include "solar/supervisor/service.hpp"
#include "solar/system/graph.hpp"

namespace solar
{

template <typename... Sections> struct Blueprint
{
    using SectionTypes = TypeList<Sections...>;
};

namespace detail
{

template <typename Section, bool Valid = BlueprintSection<Section>> struct ValidateSection;

template <typename Section> struct ValidateSection<Section, false>
{
    static_assert(dependent_false_v<Section>, "SOLAR_DIAGNOSTIC_UNKNOWN_BLUEPRINT_SECTION: "
                                              "Blueprint contains an unrecognized section type");
    static constexpr bool valid = false;
    using Key = Section;
};

template <typename Section> struct ValidateSection<Section, true>
{
    static constexpr bool valid = true;
    using Key = typename section_traits<Section>::Key;
};

template <typename Sections> struct ValidateSections;

template <typename... Sections> struct ValidateSections<TypeList<Sections...>>
{
    static_assert((ValidateSection<Sections>::valid && ...),
                  "SOLAR_DIAGNOSTIC_INVALID_BLUEPRINT: one or more sections are invalid");
    using Keys = TypeList<typename ValidateSection<Sections>::Key...>;
    static_assert(unique_types_v<Keys>, "SOLAR_DIAGNOSTIC_DUPLICATE_BLUEPRINT_SECTION: a singular "
                                        "Blueprint section key appears more than once");
    static constexpr bool valid = true;
};

template <typename Key, typename Sections, typename Default> struct FindSection
{
    using type = Default;
};

template <typename Key, typename Head, typename... Tail, typename Default>
struct FindSection<Key, TypeList<Head, Tail...>, Default>
{
    using type =
        std::conditional_t<BlueprintSection<Head> &&
                               std::is_same_v<Key, typename ValidateSection<Head>::Key>,
                           Head, typename FindSection<Key, TypeList<Tail...>, Default>::type>;
};

template <typename Key, typename Sections, typename Default>
using find_section_t = typename FindSection<Key, Sections, Default>::type;

template <typename Section> using section_entries_t = typename section_traits<Section>::Entries;

template <typename Sections, SectionRole Role> struct SectionsWithRole;

template <SectionRole Role> struct SectionsWithRole<TypeList<>, Role>
{
    using type = TypeList<>;
};

template <typename Head, typename... Tail, SectionRole Role>
struct SectionsWithRole<TypeList<Head, Tail...>, Role>
{
  private:
    using Remaining = typename SectionsWithRole<TypeList<Tail...>, Role>::type;

  public:
    using type = std::conditional_t<section_traits<Head>::role == Role,
                                    concat_t<TypeList<Head>, Remaining>, Remaining>;
};

template <typename CatalogSections> struct CatalogSectionTags;

template <typename... Sections> struct CatalogSectionTags<TypeList<Sections...>>
{
    using type = TypeList<typename section_traits<Sections>::CatalogTag...>;
};

template <typename CatalogSections> struct DirectFromCatalogSections;

template <typename... Sections> struct DirectFromCatalogSections<TypeList<Sections...>>
{
    template <typename Tag, typename Entries> struct RebindDirect;

    template <typename Tag, typename... Declarations>
    struct RebindDirect<Tag, TypeList<Declarations...>>
    {
        using type = DirectDeclarations<Tag, Declarations...>;
    };

    template <typename Section> struct ToDirect
    {
        using Traits = section_traits<Section>;
        using type =
            typename RebindDirect<typename Traits::CatalogTag, typename Traits::Entries>::type;
    };

    using type = DirectCatalogs<typename ToDirect<Sections>::type...>;
};

template <typename ComponentEntries, typename CatalogDirect> struct AddComponentDirect;

template <typename... Components, typename... Direct>
struct AddComponentDirect<TypeList<Components...>, DirectCatalogs<Direct...>>
{
    template <typename Domains> struct AddLogDomains;

    template <typename... Domains> struct AddLogDomains<TypeList<Domains...>>
    {
        using type = DirectCatalogs<DirectDeclarations<component::Tag, Components...>,
                                    DirectDeclarations<log::SourceTag, Components...>,
                                    DirectDeclarations<log::DomainTag, Domains...>, Direct...>;
    };

    using type = typename AddLogDomains<log::BuiltinDomains>::type;
};

template <typename ExecutionEntries, typename CatalogDirect> struct AddExecutionDirect;

template <typename... Registrations, typename... Direct>
struct AddExecutionDirect<TypeList<Registrations...>, DirectCatalogs<Direct...>>
{
    using type = DirectCatalogs<DirectDeclarations<execution::Tag, Registrations...>, Direct...>;
};

template <typename CollectionEntries, typename CatalogDirect> struct AddInspectionDirect;

template <typename... Collections, typename... Direct>
struct AddInspectionDirect<TypeList<Collections...>, DirectCatalogs<Direct...>>
{
    using type = DirectCatalogs<DirectDeclarations<inspection::Tag, Collections...>, Direct...>;
};

template <typename Tag, typename Groups> struct CompositeEntriesFor;

template <typename Tag> struct CompositeEntriesFor<Tag, TypeList<>>
{
    using type = TypeList<>;
};

template <typename Tag, typename Head, typename... Tail>
struct CompositeEntriesFor<Tag, TypeList<Head, Tail...>>
{
    static_assert(is_contribution_v<Head>,
                  "SOLAR_DIAGNOSTIC_MALFORMED_BUS_SECTION: Bus<...> entries must be "
                  "bus::Messages or bus::Subscriptions groups");
    static_assert(!is_contribution_v<Head> || std::is_same_v<typename Head::Tag, bus::MessageTag> ||
                      std::is_same_v<typename Head::Tag, bus::SubscriptionTag>,
                  "SOLAR_DIAGNOSTIC_WRONG_BUS_SECTION_TAG: Bus<...> accepts message and "
                  "subscription groups only");
    using Current =
        std::conditional_t<is_contribution_v<Head> && std::is_same_v<typename Head::Tag, Tag>,
                           typename Head::Entries, TypeList<>>;
    using type = concat_t<Current, typename CompositeEntriesFor<Tag, TypeList<Tail...>>::type>;
};

template <typename MessageEntries, typename SubscriptionEntries, typename CatalogDirect>
struct AddBusDirect;

template <typename... Messages, typename... Subscriptions, typename... Direct>
struct AddBusDirect<TypeList<Messages...>, TypeList<Subscriptions...>, DirectCatalogs<Direct...>>
{
    using type =
        DirectCatalogs<DirectDeclarations<bus::MessageTag, Messages...>,
                       DirectDeclarations<bus::SubscriptionTag, Subscriptions...>, Direct...>;
};

template <typename Candidate, typename CatalogSet, typename Explicit, typename Selected>
inline constexpr bool builtin_requested_v =
    contains_v<Candidate, Explicit> || builtin_traits<Candidate>::always_present ||
    builtin_traits<Candidate>::template demanded<CatalogSet> ||
    []<typename... Current>(TypeList<Current...>) {
        return (contains_v<Candidate, typename builtin_traits<Current>::Requirements> || ...);
    }(Selected{});

template <typename Candidates, typename CatalogSet, typename Explicit, typename Selected>
struct SelectBuiltinPass;

template <typename CatalogSet, typename Explicit, typename Selected>
struct SelectBuiltinPass<TypeList<>, CatalogSet, Explicit, Selected>
{
    using type = Selected;
};

template <typename Head, typename... Tail, typename CatalogSet, typename Explicit,
          typename Selected>
struct SelectBuiltinPass<TypeList<Head, Tail...>, CatalogSet, Explicit, Selected>
{
  private:
    using Next = std::conditional_t<builtin_requested_v<Head, CatalogSet, Explicit, Selected> &&
                                        !contains_v<Head, Selected>,
                                    concat_t<Selected, TypeList<Head>>, Selected>;

  public:
    using type = typename SelectBuiltinPass<TypeList<Tail...>, CatalogSet, Explicit, Next>::type;
};

template <typename Candidates, typename CatalogSet, typename Explicit, typename Selected,
          std::size_t Remaining>
struct CloseBuiltins;

template <typename Candidates, typename CatalogSet, typename Explicit, typename Selected,
          std::size_t Remaining, bool Done>
struct CloseBuiltinsStep;

template <typename Candidates, typename CatalogSet, typename Explicit, typename Selected,
          std::size_t Remaining>
struct CloseBuiltinsStep<Candidates, CatalogSet, Explicit, Selected, Remaining, true>
{
    using type = Selected;
};

template <typename Candidates, typename CatalogSet, typename Explicit, typename Selected,
          std::size_t Remaining>
struct CloseBuiltinsStep<Candidates, CatalogSet, Explicit, Selected, Remaining, false>
{
    static_assert(Remaining != 0,
                  "SOLAR_DIAGNOSTIC_BUILTIN_REQUIREMENT_CYCLE: built-in inclusion did not reach a "
                  "finite closure");
    using type =
        typename CloseBuiltins<Candidates, CatalogSet, Explicit, Selected, Remaining - 1>::type;
};

template <typename Candidates, typename CatalogSet, typename Explicit, typename Selected,
          std::size_t Remaining>
struct CloseBuiltins
{
  private:
    using Next = typename SelectBuiltinPass<Candidates, CatalogSet, Explicit, Selected>::type;

  public:
    using type = typename CloseBuiltinsStep<Candidates, CatalogSet, Explicit, Next, Remaining,
                                            std::is_same_v<Next, Selected>>::type;
};

template <typename Candidates, typename Selected> struct OrderSelectedBuiltins;

template <typename... Candidates, typename Selected>
struct OrderSelectedBuiltins<TypeList<Candidates...>, Selected>
{
    template <typename Candidate>
    struct IsSelected : std::bool_constant<contains_v<Candidate, Selected>>
    {};

    using type = filter_t<TypeList<Candidates...>, IsSelected>;
};

template <typename Requirements, typename Selected> struct RequirementsPresent;

template <typename... Requirements, typename Selected>
struct RequirementsPresent<TypeList<Requirements...>, Selected>
    : std::bool_constant<(contains_v<Requirements, Selected> && ...)>
{};

template <typename Selected> struct ValidateSelectedBuiltins;

template <typename... Selected> struct ValidateSelectedBuiltins<TypeList<Selected...>>
{
    static_assert((builtin_traits<Selected>::enabled && ...),
                  "SOLAR_DIAGNOSTIC_DISABLED_REQUIRED_BUILTIN: effective Blueprint requires a "
                  "disabled built-in facility");
    static_assert((RequirementsPresent<typename builtin_traits<Selected>::Requirements,
                                       TypeList<Selected...>>::value &&
                   ...),
                  "SOLAR_DIAGNOSTIC_MISSING_BUILTIN_REQUIREMENT: a selected built-in requires a "
                  "facility absent from the built-in candidate set");
    static constexpr bool valid = true;
};

template <typename SubsystemTag, typename Policy,
          bool Recognized = subsystem_policy_traits<SubsystemTag, Policy>::recognized>
struct ValidateSubsystemPolicy;

template <typename Traits> consteval bool policy_available()
{
    if constexpr (requires { Traits::available; }) {
        return Traits::available;
    }
    return true;
}

template <typename SubsystemTag, typename Policy>
struct ValidateSubsystemPolicy<SubsystemTag, Policy, false>
{
    static_assert(dependent_false_v<Policy>,
                  "SOLAR_DIAGNOSTIC_INVALID_SUBSYSTEM_POLICY: configuration contains a policy "
                  "not recognized by its subsystem");
    static constexpr bool valid = false;
    using Axis = Policy;
};

template <typename SubsystemTag, typename Policy>
struct ValidateSubsystemPolicy<SubsystemTag, Policy, true>
{
    using Traits = subsystem_policy_traits<SubsystemTag, Policy>;
    static_assert(
        requires { typename Traits::Axis; },
        "SOLAR_DIAGNOSTIC_INVALID_SUBSYSTEM_POLICY: recognized policy traits must name "
        "an exclusive policy Axis");
    static_assert(policy_available<Traits>(),
                  "SOLAR_DIAGNOSTIC_UNAVAILABLE_SUBSYSTEM_POLICY: configuration selects a policy "
                  "unavailable in this Kconfig build");
    using Axis = typename Traits::Axis;
    static constexpr bool valid = true;
};

template <typename Section> struct ValidateConfigurationSection
{
    using Traits = section_traits<Section>;
    using Tag = typename Traits::SubsystemTag;
    using Policies = typename Traits::Entries;

    template <typename List> struct Validate;

    template <typename... PolicyTypes> struct Validate<TypeList<PolicyTypes...>>
    {
        static_assert((ValidateSubsystemPolicy<Tag, PolicyTypes>::valid && ...));
        using Axes = TypeList<typename ValidateSubsystemPolicy<Tag, PolicyTypes>::Axis...>;
        static_assert(unique_types_v<Axes>,
                      "SOLAR_DIAGNOSTIC_DUPLICATE_SUBSYSTEM_POLICY_AXIS: configuration selects "
                      "more than one policy for an exclusive axis");
        static_assert(subsystem_configuration_traits<Tag>::template validate<Policies>,
                      "SOLAR_DIAGNOSTIC_INCOMPATIBLE_SUBSYSTEM_CONFIGURATION: subsystem policies "
                      "form an unsupported or contradictory combination");
        static constexpr bool valid = true;
    };

    static constexpr bool valid = Validate<Policies>::valid;
};

template <typename Sections> struct ValidateConfigurationSections;

template <typename... Sections> struct ValidateConfigurationSections<TypeList<Sections...>>
{
    static constexpr bool valid = (ValidateConfigurationSection<Sections>::valid && ...);
};

template <typename SubsystemTag, typename Axis, typename Policies> struct PolicyForAxis;

template <typename SubsystemTag, typename Axis> struct PolicyForAxis<SubsystemTag, Axis, TypeList<>>
{
    using type = NoPolicy;
};

template <typename SubsystemTag, typename Axis, typename Head, typename... Tail>
struct PolicyForAxis<SubsystemTag, Axis, TypeList<Head, Tail...>>
{
  private:
    using Remaining = typename PolicyForAxis<SubsystemTag, Axis, TypeList<Tail...>>::type;
    using HeadAxis = typename ValidateSubsystemPolicy<SubsystemTag, Head>::Axis;

  public:
    using type = std::conditional_t<std::is_same_v<Axis, HeadAxis>, Head, Remaining>;
};

template <typename BlueprintT> struct NormalizeBlueprint;

template <typename... Sections> struct NormalizeBlueprint<Blueprint<Sections...>>
{
    using UserSections = TypeList<Sections...>;
    static_assert(ValidateSections<UserSections>::valid);

    using DeviceSection = find_section_t<section_key::Devices, UserSections, Devices<>>;
    using FacilitySection = find_section_t<section_key::Facilities, UserSections, Facilities<>>;
    using ServiceSection = find_section_t<section_key::Services, UserSections, Services<>>;
    using ExecutorSection = find_section_t<section_key::Executors, UserSections, Executors<>>;
    using ExecutionSection = find_section_t<section_key::Execution, UserSections, Execution<>>;
    using BusSection = find_section_t<section_key::Bus, UserSections, Bus<>>;
    using ExplicitBuiltinSection = find_section_t<section_key::Builtins, UserSections, Builtins<>>;
    using BuiltinCandidateSection =
        find_section_t<section_key::BuiltinCandidates, UserSections, BuiltinCandidates<>>;
    using ExtensionTagSection =
        find_section_t<section_key::ExtensionTags, UserSections, ExtensionTags<>>;

    using UserDevices = section_entries_t<DeviceSection>;
    using UserFacilities = section_entries_t<FacilitySection>;
    using UserServices = section_entries_t<ServiceSection>;
    using UserExecutors = section_entries_t<ExecutorSection>;
    using ExecutionRegistrations = section_entries_t<ExecutionSection>;
    using BusGroups = section_entries_t<BusSection>;
    using RootBusMessages = typename CompositeEntriesFor<bus::MessageTag, BusGroups>::type;
    using RootBusSubscriptions =
        typename CompositeEntriesFor<bus::SubscriptionTag, BusGroups>::type;
    using UserComponents = concat_t<UserDevices, UserFacilities, UserServices, UserExecutors>;

    static_assert(unique_types_v<UserComponents>,
                  "SOLAR_DIAGNOSTIC_DUPLICATE_COMPONENT: component appears more than once or in "
                  "incompatible categories");

    using CatalogSections =
        typename SectionsWithRole<UserSections, SectionRole::SubsystemCatalog>::type;
    using CatalogTags = typename CatalogSectionTags<CatalogSections>::type;
    using ExtensionCatalogTags = section_entries_t<ExtensionTagSection>;
    using CandidateCatalogTags = unique_t<
        concat_t<TypeList<component::Tag, execution::Tag, bus::MessageTag, bus::SubscriptionTag,
                          parameters::Tag, parameters::ChangeTag, events::Tag, events::ProcessorTag,
                          metrics::Tag, log::SourceTag, log::DomainTag, inspection::Tag,
                          health::CheckTag, remote::SchemaTag, remote::DataTag, remote::ActionTag,
                          remote::TopicTag, remote::StreamTag, remote::LinkTag>,
                 CatalogTags, ExtensionCatalogTags>>;
    using UserCatalogDirect = typename DirectFromCatalogSections<CatalogSections>::type;
    using BusDirect =
        typename AddBusDirect<RootBusMessages, RootBusSubscriptions, UserCatalogDirect>::type;
    using ComponentDirect = typename AddComponentDirect<UserComponents, BusDirect>::type;
    using ExecutionDirect =
        typename AddExecutionDirect<ExecutionRegistrations, ComponentDirect>::type;
    using UserDirect =
        typename AddInspectionDirect<inspection::BuiltinCollections, ExecutionDirect>::type;
    using ProvisionalCatalogs =
        collect_catalog_set_t<CandidateCatalogTags, UserComponents, UserDirect>;

    using BusConfigurationSection = find_section_t<section_key::Configuration<bus::Tag>,
                                                   UserSections, SubsystemConfiguration<bus::Tag>>;
    using BusConfiguration = section_entries_t<BusConfigurationSection>;
    using BusMessageDeclarations = bus::detail::declarations_of_t<
        typename ProvisionalCatalogs::template Of<bus::MessageTag>::EntryTypes>;
    using BusSubscriptionDeclarations = bus::detail::declarations_of_t<
        typename ProvisionalCatalogs::template Of<bus::SubscriptionTag>::EntryTypes>;
    using BusArchitecture = bus::Architecture<BusMessageDeclarations, BusSubscriptionDeclarations,
                                              UserComponents, BusConfiguration>;
    using ParameterConfigurationSection =
        find_section_t<section_key::Configuration<parameters::Tag>, UserSections,
                       SubsystemConfiguration<parameters::Tag>>;
    using ParameterConfiguration = section_entries_t<ParameterConfigurationSection>;
    using ParameterDeclarations = parameters::detail::declarations_of_t<
        typename ProvisionalCatalogs::template Of<parameters::Tag>::EntryTypes>;
    using ParameterChangeDeclarations = parameters::detail::declarations_of_t<
        typename ProvisionalCatalogs::template Of<parameters::ChangeTag>::EntryTypes>;
    using ParameterArchitecture =
        parameters::Architecture<ParameterDeclarations, ParameterChangeDeclarations, UserComponents,
                                 ParameterConfiguration>;
    using EventConfigurationSection =
        find_section_t<section_key::Configuration<events::Tag>, UserSections,
                       SubsystemConfiguration<events::Tag>>;
    using EventConfiguration = section_entries_t<EventConfigurationSection>;
    using EventDeclarations = events::detail::declarations_of_t<
        typename ProvisionalCatalogs::template Of<events::Tag>::EntryTypes>;
    using EventProcessorDeclarations = events::detail::declarations_of_t<
        typename ProvisionalCatalogs::template Of<events::ProcessorTag>::EntryTypes>;
    using EventArchitecture = events::Architecture<EventDeclarations, EventProcessorDeclarations,
                                                   UserComponents, EventConfiguration>;
    using MetricConfigurationSection =
        find_section_t<section_key::Configuration<metrics::Tag>, UserSections,
                       SubsystemConfiguration<metrics::Tag>>;
    using MetricConfiguration = section_entries_t<MetricConfigurationSection>;
    using MetricDeclarations = metrics::detail::declarations_of_t<
        typename ProvisionalCatalogs::template Of<metrics::Tag>::EntryTypes>;
    using MetricArchitecture =
        metrics::Architecture<MetricDeclarations, UserComponents, MetricConfiguration>;
    using LogConfigurationSection = find_section_t<section_key::Configuration<log::Tag>,
                                                   UserSections, SubsystemConfiguration<log::Tag>>;
    using LogConfiguration = section_entries_t<LogConfigurationSection>;
    using LogArchitecture = log::Architecture<UserComponents, LogConfiguration>;
    using RemoteConfigurationSection =
        find_section_t<section_key::Configuration<remote::Tag>, UserSections,
                       SubsystemConfiguration<remote::Tag>>;
    using RemoteConfiguration = section_entries_t<RemoteConfigurationSection>;
    using RemoteSchemaDeclarations = remote::detail::declarations_of_t<
        typename ProvisionalCatalogs::template Of<remote::SchemaTag>::EntryTypes>;
    using RemoteDataDeclarations = remote::detail::declarations_of_t<
        typename ProvisionalCatalogs::template Of<remote::DataTag>::EntryTypes>;
    using RemoteActionDeclarations = remote::detail::declarations_of_t<
        typename ProvisionalCatalogs::template Of<remote::ActionTag>::EntryTypes>;
    using RemoteTopicDeclarations = remote::detail::declarations_of_t<
        typename ProvisionalCatalogs::template Of<remote::TopicTag>::EntryTypes>;
    using RemoteStreamDeclarations = remote::detail::declarations_of_t<
        typename ProvisionalCatalogs::template Of<remote::StreamTag>::EntryTypes>;
    using RemoteLinkDeclarations = remote::detail::declarations_of_t<
        typename ProvisionalCatalogs::template Of<remote::LinkTag>::EntryTypes>;
    using RemoteArchitecture =
        remote::Architecture<RemoteSchemaDeclarations, RemoteDataDeclarations,
                             RemoteActionDeclarations, RemoteTopicDeclarations,
                             RemoteStreamDeclarations, RemoteLinkDeclarations, UserComponents,
                             RemoteConfiguration>;
    using RemoteFacility = remote::Facility<RemoteArchitecture>;
    using RemoteService = remote::Service<RemoteArchitecture>;
    using SupervisorConfigurationSection =
        find_section_t<section_key::Configuration<supervisor::Tag>, UserSections,
                       SubsystemConfiguration<supervisor::Tag>>;
    using SupervisorConfiguration = section_entries_t<SupervisorConfigurationSection>;
    using SupervisorArchitecture =
        supervisor::Architecture<UserComponents, SupervisorConfiguration>;
    using SupervisorService = supervisor::Service<SupervisorArchitecture>;
    using GeneratedBuiltins =
        TypeList<bus::Facility<BusArchitecture>, parameters::Facility<ParameterArchitecture>,
                 events::Facility<EventArchitecture>, metrics::Facility<MetricArchitecture>,
                 log::Facility<LogArchitecture>, inspection::Facility, RemoteFacility,
                 health::Facility>;

    using ExplicitBuiltins = section_entries_t<ExplicitBuiltinSection>;
    using Candidates = unique_t<
        concat_t<section_entries_t<BuiltinCandidateSection>, GeneratedBuiltins, ExplicitBuiltins>>;
    using SelectedBuiltins =
        typename CloseBuiltins<Candidates, ProvisionalCatalogs, ExplicitBuiltins, TypeList<>,
                               list_size_v<Candidates> + 1>::type;
    using EffectiveBuiltins = typename OrderSelectedBuiltins<Candidates, SelectedBuiltins>::type;
    static_assert(ValidateSelectedBuiltins<EffectiveBuiltins>::valid);

    using EffectiveFacilities = concat_t<UserFacilities, EffectiveBuiltins>;
    using GeneratedRemoteServices =
        std::conditional_t<contains_v<RemoteFacility, EffectiveBuiltins>, TypeList<RemoteService>,
                           TypeList<>>;
    using GeneratedSupervisorServices =
        std::conditional_t<supervisor::enabled, TypeList<SupervisorService>, TypeList<>>;
    using GeneratedServices = concat_t<GeneratedRemoteServices, GeneratedSupervisorServices>;
    using EffectiveServices = concat_t<UserServices, GeneratedServices>;
    using Components = concat_t<UserDevices, EffectiveFacilities, EffectiveServices, UserExecutors>;
    static_assert(ValidateComponentGraph<Components>::valid);

    using FinalDirect = typename AddComponentDirect<Components, BusDirect>::type;
    using EffectiveExecutionDirect =
        typename AddExecutionDirect<ExecutionRegistrations, FinalDirect>::type;
    using EffectiveDirect = typename AddInspectionDirect<inspection::BuiltinCollections,
                                                         EffectiveExecutionDirect>::type;
    using Catalogs = collect_catalog_set_t<CandidateCatalogTags, Components, EffectiveDirect>;
    using ExecutionCatalog = typename Catalogs::template Of<execution::Tag>;
    using BusMessageCatalog = typename Catalogs::template Of<bus::MessageTag>;
    using BusSubscriptionCatalog = typename Catalogs::template Of<bus::SubscriptionTag>;
    using BusFacility = bus::Facility<BusArchitecture>;
    using ParameterCatalog = typename Catalogs::template Of<parameters::Tag>;
    using ParameterChangeCatalog = typename Catalogs::template Of<parameters::ChangeTag>;
    using ParameterFacility = parameters::Facility<ParameterArchitecture>;
    using EventCatalog = typename Catalogs::template Of<events::Tag>;
    using EventProcessorCatalog = typename Catalogs::template Of<events::ProcessorTag>;
    using EventFacility = events::Facility<EventArchitecture>;
    using MetricCatalog = typename Catalogs::template Of<metrics::Tag>;
    using MetricFacility = metrics::Facility<MetricArchitecture>;
    using InspectionCatalog = typename Catalogs::template Of<inspection::Tag>;
    using InspectionFacility = inspection::Facility;
    using HealthMonitorCatalog = typename Catalogs::template Of<health::CheckTag>;
    using HealthFacility = health::Facility;
    using LogSourceCatalog = typename Catalogs::template Of<log::SourceTag>;
    using LogDomainCatalog = typename Catalogs::template Of<log::DomainTag>;
    using LogFacility = log::Facility<LogArchitecture>;
    using RemoteSchemaCatalog = typename Catalogs::template Of<remote::SchemaTag>;
    using RemoteDataCatalog = typename Catalogs::template Of<remote::DataTag>;
    using RemoteActionCatalog = typename Catalogs::template Of<remote::ActionTag>;
    using RemoteTopicCatalog = typename Catalogs::template Of<remote::TopicTag>;
    using RemoteStreamCatalog = typename Catalogs::template Of<remote::StreamTag>;
    using RemoteLinkCatalog = typename Catalogs::template Of<remote::LinkTag>;
    static_assert(health::enabled || HealthMonitorCatalog::size == 0,
                  "SOLAR_DIAGNOSTIC_HEALTH_DISABLED: Health monitor declarations require "
                  "CONFIG_SOLAR_HEALTH");
    static_assert(remote::available ||
                      (RemoteSchemaCatalog::size == 0 && RemoteDataCatalog::size == 0 &&
                       RemoteActionCatalog::size == 0 && RemoteTopicCatalog::size == 0 &&
                       RemoteStreamCatalog::size == 0 && RemoteLinkCatalog::size == 0),
                  "SOLAR_DIAGNOSTIC_REMOTE_DISABLED: Remote declarations require "
                  "CONFIG_SOLAR_REMOTE");
#if defined(CONFIG_SOLAR_REMOTE)
    static_assert(RemoteSchemaCatalog::size <= CONFIG_SOLAR_REMOTE_MAX_SCHEMAS,
                  "SOLAR_DIAGNOSTIC_REMOTE_SCHEMA_CEILING: effective schema catalog exceeds "
                  "CONFIG_SOLAR_REMOTE_MAX_SCHEMAS");
    static_assert(RemoteDataCatalog::size <= CONFIG_SOLAR_REMOTE_MAX_ENDPOINTS &&
                      RemoteActionCatalog::size <= CONFIG_SOLAR_REMOTE_MAX_ENDPOINTS &&
                      RemoteTopicCatalog::size <= CONFIG_SOLAR_REMOTE_MAX_ENDPOINTS &&
                      RemoteStreamCatalog::size <= CONFIG_SOLAR_REMOTE_MAX_ENDPOINTS,
                  "SOLAR_DIAGNOSTIC_REMOTE_ENDPOINT_CEILING: effective endpoint catalog exceeds "
                  "CONFIG_SOLAR_REMOTE_MAX_ENDPOINTS");
    static_assert(RemoteDataCatalog::size * 2 + RemoteTopicCatalog::size <=
                      CONFIG_SOLAR_REMOTE_MAX_ENDPOINTS,
                  "SOLAR_DIAGNOSTIC_REMOTE_SUBSCRIPTION_CEILING: Data stream, Data watch, and "
                  "Topic subscription slots exceed CONFIG_SOLAR_REMOTE_MAX_ENDPOINTS");
#endif
    using EffectiveExecutionRegistrations = typename ExecutionCatalog::EntryTypes;
    static_assert(execution::enabled || ExecutionCatalog::size == 0,
                  "SOLAR_DIAGNOSTIC_EXECUTION_DISABLED: Blueprint registers execution work while "
                  "CONFIG_SOLAR_EXECUTION is disabled");
#if defined(CONFIG_SOLAR_EXECUTION)
    static_assert(ExecutionCatalog::size <= CONFIG_SOLAR_EXECUTION_MAX_REGISTRATIONS,
                  "SOLAR_DIAGNOSTIC_EXECUTION_REGISTRATION_CEILING: effective execution catalog "
                  "exceeds CONFIG_SOLAR_EXECUTION_MAX_REGISTRATIONS");
#endif
#if defined(CONFIG_SOLAR_EVENTS)
    static_assert(EventCatalog::size <= CONFIG_SOLAR_EVENTS_MAX_EVENTS,
                  "SOLAR_DIAGNOSTIC_EVENT_CEILING: effective event catalog exceeds "
                  "CONFIG_SOLAR_EVENTS_MAX_EVENTS");
    static_assert(EventProcessorCatalog::size <= CONFIG_SOLAR_EVENTS_MAX_PROCESSORS,
                  "SOLAR_DIAGNOSTIC_EVENT_PROCESSOR_CEILING: effective event processor catalog "
                  "exceeds CONFIG_SOLAR_EVENTS_MAX_PROCESSORS");
#endif
#if defined(CONFIG_SOLAR_INSPECTION)
    static_assert(InspectionCatalog::size <= CONFIG_SOLAR_INSPECTION_MAX_COLLECTIONS,
                  "SOLAR_DIAGNOSTIC_INSPECTION_COLLECTION_CEILING: effective Inspection catalog "
                  "exceeds CONFIG_SOLAR_INSPECTION_MAX_COLLECTIONS");
#if defined(CONFIG_SOLAR_INSPECTION_REMOTE)
    static_assert(CONFIG_SOLAR_INSPECTION_REMOTE_MAX_PAGE_RECORDS <=
                      CONFIG_SOLAR_INSPECTION_MAX_PAGE_RECORDS,
                  "SOLAR_DIAGNOSTIC_INSPECTION_REMOTE_PAGE_CEILING: Remote Inspection page "
                  "capacity exceeds CONFIG_SOLAR_INSPECTION_MAX_PAGE_RECORDS");
    static_assert(CONFIG_SOLAR_INSPECTION_REMOTE_RESPONSE_BYTES <=
                      CONFIG_SOLAR_REMOTE_MAX_MESSAGE_BYTES,
                  "SOLAR_DIAGNOSTIC_INSPECTION_REMOTE_RESPONSE_CEILING: Remote Inspection "
                  "response storage exceeds CONFIG_SOLAR_REMOTE_MAX_MESSAGE_BYTES");
#endif
#else
    static_assert(InspectionCatalog::size == 0,
                  "SOLAR_DIAGNOSTIC_INSPECTION_DISABLED: Inspection collections require "
                  "CONFIG_SOLAR_INSPECTION");
#endif
#if defined(CONFIG_SOLAR_BUS)
    static_assert(BusMessageCatalog::size <= CONFIG_SOLAR_BUS_MAX_MESSAGES,
                  "SOLAR_DIAGNOSTIC_BUS_MESSAGE_CEILING: effective Bus message catalog exceeds "
                  "CONFIG_SOLAR_BUS_MAX_MESSAGES");
    static_assert(BusSubscriptionCatalog::size <= CONFIG_SOLAR_BUS_MAX_SUBSCRIPTIONS,
                  "SOLAR_DIAGNOSTIC_BUS_SUBSCRIPTION_CEILING: effective Bus subscription catalog "
                  "exceeds CONFIG_SOLAR_BUS_MAX_SUBSCRIPTIONS");
#endif
#if defined(CONFIG_SOLAR_PARAMETERS)
    static_assert(ParameterCatalog::size <= CONFIG_SOLAR_PARAMETERS_MAX_PARAMETERS,
                  "SOLAR_DIAGNOSTIC_PARAMETER_CEILING: effective parameter catalog exceeds "
                  "CONFIG_SOLAR_PARAMETERS_MAX_PARAMETERS");
    static_assert(ParameterChangeCatalog::size <= CONFIG_SOLAR_PARAMETERS_MAX_CHANGE_HOOKS,
                  "SOLAR_DIAGNOSTIC_PARAMETER_CHANGE_CEILING: effective parameter change catalog "
                  "exceeds CONFIG_SOLAR_PARAMETERS_MAX_CHANGE_HOOKS");
#endif
#if defined(CONFIG_SOLAR_METRICS)
    static_assert(MetricCatalog::size <= CONFIG_SOLAR_METRICS_MAX_METRICS,
                  "SOLAR_DIAGNOSTIC_METRIC_CEILING: effective metric catalog exceeds "
                  "CONFIG_SOLAR_METRICS_MAX_METRICS");
#endif
#if defined(CONFIG_SOLAR_LOG)
    static_assert(LogSourceCatalog::size <= CONFIG_SOLAR_LOG_MAX_SOURCES,
                  "SOLAR_DIAGNOSTIC_LOG_SOURCE_CEILING: effective log source catalog exceeds "
                  "CONFIG_SOLAR_LOG_MAX_SOURCES");
    static_assert(LogDomainCatalog::size <= CONFIG_SOLAR_LOG_MAX_DOMAINS,
                  "SOLAR_DIAGNOSTIC_LOG_DOMAIN_CEILING: effective log domain catalog exceeds "
                  "CONFIG_SOLAR_LOG_MAX_DOMAINS");
#endif
    using Graph = ComponentGraph<Components>;
    using ConfigurationSections =
        typename SectionsWithRole<UserSections, SectionRole::SubsystemConfiguration>::type;
    static_assert(ValidateConfigurationSections<ConfigurationSections>::valid);

    template <typename SubsystemTag>
    using ConfigurationSection = find_section_t<section_key::Configuration<SubsystemTag>,
                                                UserSections, SubsystemConfiguration<SubsystemTag>>;

    template <typename SubsystemTag>
    using Configuration = section_entries_t<ConfigurationSection<SubsystemTag>>;

    template <typename SubsystemTag, typename Axis>
    using ConfigurationPolicy =
        typename PolicyForAxis<SubsystemTag, Axis, Configuration<SubsystemTag>>::type;

    template <typename Component>
    using CategoryOf = std::conditional_t<
        contains_v<Component, UserDevices>, category::Device,
        std::conditional_t<
            contains_v<Component, EffectiveFacilities>, category::Facility,
            std::conditional_t<contains_v<Component, EffectiveServices>, category::Service,
                               std::conditional_t<contains_v<Component, UserExecutors>,
                                                  category::Executor, void>>>>;

    static constexpr bool valid = true;
};

} // namespace detail

template <typename BlueprintT> using effective_blueprint_t = detail::NormalizeBlueprint<BlueprintT>;

} // namespace solar
