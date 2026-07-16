#pragma once

#include <cstdint>
#include <type_traits>

#include "solar/catalog.hpp"
#include "solar/core/type_list.hpp"

namespace solar
{

enum class SectionRole : std::uint8_t
{
    ComponentCatalog,
    SubsystemCatalog,
    SubsystemConfiguration,
    ExecutionRegistration,
    CompositeCatalog,
    BuiltinSelection,
    ExtensionTags,
};

template <typename Section, typename = void> struct section_traits
{};

template <typename SubsystemTag, typename Policy> struct subsystem_policy_traits
{
    static constexpr bool recognized = false;
};

template <typename SubsystemTag> struct subsystem_configuration_traits
{
    template <typename Policies> static constexpr bool validate = true;
};

template <typename Dependent, typename Candidate, typename AllComponents>
struct generated_component_dependency : std::false_type
{};

template <typename Builtin> struct builtin_traits
{
    static constexpr bool enabled = false;
    static constexpr bool always_present = false;
    using Requirements = TypeList<>;

    template <typename CatalogSet> static constexpr bool demanded = false;
};

struct NoPolicy
{};

template <typename Section>
concept BlueprintSection = requires {
    typename section_traits<Section>::Key;
    typename section_traits<Section>::Entries;
    { section_traits<Section>::role } -> std::convertible_to<SectionRole>;
};

namespace section_key
{
struct Devices;
struct Facilities;
struct Services;
struct Executors;
struct Execution;
struct Bus;
struct Builtins;
struct BuiltinCandidates;
struct ExtensionTags;

template <typename Tag> struct Catalog;

template <typename Tag> struct Configuration;
} // namespace section_key

namespace category
{
struct Device;
struct Facility;
struct Service;
struct Executor;
} // namespace category

template <typename... Types> struct Dependencies
{
    using Entries = TypeList<Types...>;
};

template <typename... Types> struct Devices
{
    using Entries = TypeList<Types...>;
};

template <typename... Types> struct Facilities
{
    using Entries = TypeList<Types...>;
};

template <typename... Types> struct Services
{
    using Entries = TypeList<Types...>;
};

template <typename... Types> struct Executors
{
    using Entries = TypeList<Types...>;
};

template <typename... Types> struct Execution
{
    using Entries = TypeList<Types...>;
};

template <typename... Groups> struct Bus
{
    using Entries = TypeList<Groups...>;
};

template <typename Tag, typename... Types> struct CatalogSection
{
    using CatalogTag = Tag;
    using Entries = TypeList<Types...>;
};

template <typename Tag, typename... Policies> struct SubsystemConfiguration
{
    using SubsystemTag = Tag;
    using Entries = TypeList<Policies...>;
};

template <typename... Types> struct Builtins
{
    using Entries = TypeList<Types...>;
};

template <typename... Types> struct BuiltinCandidates
{
    using Entries = TypeList<Types...>;
};

template <typename... Tags> struct ExtensionTags
{
    using Entries = TypeList<Tags...>;
};

#define SOLAR_DETAIL_COMPONENT_SECTION_TRAITS(SECTION, KEY, CATEGORY)                              \
    template <typename... Types> struct section_traits<SECTION<Types...>>                          \
    {                                                                                              \
        using Key = section_key::KEY;                                                              \
        using Entries = TypeList<Types...>;                                                        \
        using Category = category::CATEGORY;                                                       \
        static constexpr SectionRole role = SectionRole::ComponentCatalog;                         \
    }

SOLAR_DETAIL_COMPONENT_SECTION_TRAITS(Devices, Devices, Device);
SOLAR_DETAIL_COMPONENT_SECTION_TRAITS(Facilities, Facilities, Facility);
SOLAR_DETAIL_COMPONENT_SECTION_TRAITS(Services, Services, Service);
SOLAR_DETAIL_COMPONENT_SECTION_TRAITS(Executors, Executors, Executor);

#undef SOLAR_DETAIL_COMPONENT_SECTION_TRAITS

template <typename... Types> struct section_traits<Execution<Types...>>
{
    using Key = section_key::Execution;
    using Entries = TypeList<Types...>;
    static constexpr SectionRole role = SectionRole::ExecutionRegistration;
};

template <typename... Groups> struct section_traits<Bus<Groups...>>
{
    using Key = section_key::Bus;
    using Entries = TypeList<Groups...>;
    static constexpr SectionRole role = SectionRole::CompositeCatalog;
};

template <typename Tag, typename... Types> struct section_traits<CatalogSection<Tag, Types...>>
{
    using Key = section_key::Catalog<Tag>;
    using CatalogTag = Tag;
    using Entries = TypeList<Types...>;
    static constexpr SectionRole role = SectionRole::SubsystemCatalog;
};

template <typename Tag, typename... Policies>
struct section_traits<SubsystemConfiguration<Tag, Policies...>>
{
    using Key = section_key::Configuration<Tag>;
    using SubsystemTag = Tag;
    using Entries = TypeList<Policies...>;
    static constexpr SectionRole role = SectionRole::SubsystemConfiguration;
};

template <typename... Types> struct section_traits<Builtins<Types...>>
{
    using Key = section_key::Builtins;
    using Entries = TypeList<Types...>;
    static constexpr SectionRole role = SectionRole::BuiltinSelection;
};

template <typename... Types> struct section_traits<BuiltinCandidates<Types...>>
{
    using Key = section_key::BuiltinCandidates;
    using Entries = TypeList<Types...>;
    static constexpr SectionRole role = SectionRole::BuiltinSelection;
};

template <typename... Tags> struct section_traits<ExtensionTags<Tags...>>
{
    using Key = section_key::ExtensionTags;
    using Entries = TypeList<Tags...>;
    static constexpr SectionRole role = SectionRole::ExtensionTags;
};

template <typename... Types> using Parameters = CatalogSection<parameters::Tag, Types...>;

template <typename... Types> using Events = CatalogSection<events::Tag, Types...>;

template <typename... Types> using Metrics = CatalogSection<metrics::Tag, Types...>;

namespace bus
{
template <typename... Policies> using Configuration = SubsystemConfiguration<Tag, Policies...>;
} // namespace bus

namespace events
{
template <typename... Policies> using Configuration = SubsystemConfiguration<Tag, Policies...>;
} // namespace events

namespace metrics
{
template <typename... Policies> using Configuration = SubsystemConfiguration<Tag, Policies...>;
} // namespace metrics

namespace log
{
template <typename... Policies> using Configuration = SubsystemConfiguration<Tag, Policies...>;
} // namespace log

template <typename DeclarationPolicy, typename BlueprintPolicy, typename KconfigPolicy>
using resolve_policy_t = std::conditional_t<
    !std::is_same_v<DeclarationPolicy, NoPolicy>, DeclarationPolicy,
    std::conditional_t<!std::is_same_v<BlueprintPolicy, NoPolicy>, BlueprintPolicy, KconfigPolicy>>;

} // namespace solar
