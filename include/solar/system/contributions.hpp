#pragma once

#include <concepts>
#include <type_traits>
#include <utility>

#include "solar/catalog/contribution.hpp"
#include "solar/core/status.hpp"
#include "solar/core/type_list.hpp"

namespace solar
{

struct UsesTag
{};
struct ProvidesTag
{};
struct HandlesTag
{};
struct PublishesTag
{};
struct ConsumesTag
{};
struct EmitsTag
{};
struct ObservesTag
{};
struct RecordsTag
{};

template <typename Module, typename... Entries> struct Uses
{
    using Kind = UsesTag;
    using ModuleType = Module;
    using EntryTypes = TypeList<Entries...>;
};

template <typename... Data> struct Provides
{
    using Kind = ProvidesTag;
    using EntryTypes = TypeList<Data...>;
};

template <typename... Actions> struct Handles
{
    using Kind = HandlesTag;
    using EntryTypes = TypeList<Actions...>;
};

template <typename... Streams> struct Publishes
{
    using Kind = PublishesTag;
    using EntryTypes = TypeList<Streams...>;
};

template <typename... Streams> struct Consumes
{
    using Kind = ConsumesTag;
    using EntryTypes = TypeList<Streams...>;
};

template <typename... Events> struct Emits
{
    using Kind = EmitsTag;
    using EntryTypes = TypeList<Events...>;
};
template <typename... Events> struct Observes
{
    using Kind = ObservesTag;
    using EntryTypes = TypeList<Events...>;
};
template <typename... Metrics> struct Records
{
    using Kind = RecordsTag;
    using EntryTypes = TypeList<Metrics...>;
};

namespace system
{
namespace contribution_detail
{

template <typename> inline constexpr bool dependent_false_v = false;

template <typename Component, typename = void> struct ComponentRoles
{
    using type = TypeList<>;
};

template <typename Component>
struct ComponentRoles<Component, std::void_t<typename Component::Contributions>>
{
    using type = typename Component::Contributions::Entries;
};

template <typename Component>
    requires requires { typename Component::Participation; }
struct ComponentRoles<Component, void>
{
    using type = typename Component::Participation::Entries;
};

template <typename Role, typename = void> struct ValidRole : std::false_type
{};

template <typename Role>
struct ValidRole<Role, std::void_t<typename Role::Kind, typename Role::EntryTypes>>
    : std::bool_constant<std::is_same_v<typename Role::Kind, UsesTag> ||
                         std::is_same_v<typename Role::Kind, ProvidesTag> ||
                         std::is_same_v<typename Role::Kind, HandlesTag> ||
                         std::is_same_v<typename Role::Kind, PublishesTag> ||
                         std::is_same_v<typename Role::Kind, ConsumesTag> ||
                         std::is_same_v<typename Role::Kind, EmitsTag> ||
                         std::is_same_v<typename Role::Kind, ObservesTag> ||
                         std::is_same_v<typename Role::Kind, RecordsTag>>
{};

template <typename Entry, typename Kind, typename Roles> struct RoleCount;

template <typename Entry, typename Kind>
struct RoleCount<Entry, Kind, TypeList<>> : std::integral_constant<std::size_t, 0>
{};

template <typename Entry, typename Kind, typename Head, typename... Tail>
struct RoleCount<Entry, Kind, TypeList<Head, Tail...>>
    : std::integral_constant<std::size_t, (std::is_same_v<typename Head::Kind, Kind> &&
                                                   contains_v<Entry, typename Head::EntryTypes>
                                               ? 1U
                                               : 0U) +
                                              RoleCount<Entry, Kind, TypeList<Tail...>>::value>
{};

template <typename Entry, typename Kind, typename Components> struct OwnerCount;

template <typename Entry, typename Kind, typename... ComponentTypes>
struct OwnerCount<Entry, Kind, TypeList<ComponentTypes...>>
    : std::integral_constant<
          std::size_t,
          (std::size_t{0} + ... +
           RoleCount<Entry, Kind, typename ComponentRoles<ComponentTypes>::type>::value)>
{};

template <typename Entry, typename Kind, typename Component>
inline constexpr bool component_claims_v =
    RoleCount<Entry, Kind, typename ComponentRoles<Component>::type>::value != 0;

template <bool Claims, typename Entry, typename Kind, typename Head, typename Tail>
struct FindOwnerStep;

template <typename Entry, typename Kind, typename Head, typename Tail>
struct FindOwnerStep<true, Entry, Kind, Head, Tail>
{
    using type = Head;
};

template <typename Entry, typename Kind, typename Head, typename Tail>
struct FindOwnerStep<false, Entry, Kind, Head, Tail>;

template <typename Entry, typename Kind, typename Components> struct FindOwner;

template <typename Entry, typename Kind, typename Head, typename... Tail>
struct FindOwner<Entry, Kind, TypeList<Head, Tail...>>
    : FindOwnerStep<component_claims_v<Entry, Kind, Head>, Entry, Kind, Head, TypeList<Tail...>>
{};

template <typename Entry, typename Kind, typename Head, typename... Tail>
struct FindOwnerStep<false, Entry, Kind, Head, TypeList<Tail...>>
    : FindOwner<Entry, Kind, TypeList<Tail...>>
{};

template <typename Entry, typename Kind> struct FindOwner<Entry, Kind, TypeList<>>
{
    static_assert(dependent_false_v<Entry>,
                  "SOLAR_SYSTEM_MISSING_ENDPOINT_OWNER: no component claims the endpoint");
};

template <typename Entries, typename Catalog> struct EntriesBelong;

template <typename Catalog, typename... Entries>
struct EntriesBelong<TypeList<Entries...>, Catalog>
    : std::bool_constant<(contains_v<Entries, Catalog> && ...)>
{};

template <typename Role, typename Contract> struct RoleBelongs;

template <typename Module> struct ModuleSchemaEntries
{
    using type = typename Module::SchemaType::Entries;
};

template <typename Module, typename Contract, typename... Entries>
struct RoleBelongs<Uses<Module, Entries...>, Contract>
    : EntriesBelong<TypeList<Entries...>, typename ModuleSchemaEntries<Module>::type>
{};

template <typename Contract, typename... Entries>
struct RoleBelongs<Handles<Entries...>, Contract>
    : EntriesBelong<TypeList<Entries...>, typename Contract::Actions>
{};

template <typename Contract, typename = void> struct ContractData
{
    using type = TypeList<>;
};
template <typename Contract> struct ContractData<Contract, std::void_t<typename Contract::Data>>
{
    using type = typename Contract::Data;
};

template <typename Contract, typename... Entries>
struct RoleBelongs<Provides<Entries...>, Contract>
    : EntriesBelong<TypeList<Entries...>, typename ContractData<Contract>::type>
{};

template <typename Contract, typename... Entries>
struct RoleBelongs<Publishes<Entries...>, Contract>
    : EntriesBelong<TypeList<Entries...>, typename Contract::OutputStreams>
{};

template <typename Contract, typename... Entries>
struct RoleBelongs<Consumes<Entries...>, Contract>
    : EntriesBelong<TypeList<Entries...>, typename Contract::InputStreams>
{};

template <typename Contract, typename = void> struct ContractEvents
{
    using type = TypeList<>;
};
template <typename Contract> struct ContractEvents<Contract, std::void_t<typename Contract::Events>>
{
    using type = typename Contract::Events;
};
template <typename Contract, typename = void> struct ContractMetrics
{
    using type = TypeList<>;
};
template <typename Contract>
struct ContractMetrics<Contract, std::void_t<typename Contract::Metrics>>
{
    using type = typename Contract::Metrics;
};

template <typename Contract, typename... Entries>
struct RoleBelongs<Emits<Entries...>, Contract>
    : EntriesBelong<TypeList<Entries...>, typename ContractEvents<Contract>::type>
{};
template <typename Contract, typename... Entries>
struct RoleBelongs<Observes<Entries...>, Contract>
    : EntriesBelong<TypeList<Entries...>, typename ContractEvents<Contract>::type>
{};
template <typename Contract, typename... Entries>
struct RoleBelongs<Records<Entries...>, Contract>
    : EntriesBelong<TypeList<Entries...>, typename ContractMetrics<Contract>::type>
{};

template <typename Roles, typename Contract> struct ValidateRoles;

template <typename Contract, typename... Roles> struct ValidateRoles<TypeList<Roles...>, Contract>
{
    static_assert((ValidRole<Roles>::value && ...),
                  "SOLAR_SYSTEM_INVALID_CONTRIBUTION_ROLE: component contribution is invalid");
    static_assert((RoleBelongs<Roles, Contract>::value && ...),
                  "SOLAR_SYSTEM_UNDECLARED_PARTICIPATION: a component references an entry "
                  "absent from the generated contract");
    static constexpr bool value = true;
};

template <typename Components, typename Contract> struct ValidateAllRoles;

template <typename Contract, typename... ComponentTypes>
struct ValidateAllRoles<TypeList<ComponentTypes...>, Contract>
    : std::bool_constant<(
          ValidateRoles<typename ComponentRoles<ComponentTypes>::type, Contract>::value && ...)>
{};

template <typename Entries, typename Kind, typename Components> struct ExactOwners;

template <typename Kind, typename Components, typename... Entries>
struct ExactOwners<TypeList<Entries...>, Kind, Components>
    : std::bool_constant<((OwnerCount<Entries, Kind, Components>::value == 1) && ...)>
{};

template <typename Action, typename Components> struct ValidateActionSignature
{
    using Owner = typename FindOwner<Action, HandlesTag, Components>::type;
    static_assert(
        requires(const typename Action::Request& request) {
            { Owner::handle(Action{}, request) } -> std::same_as<typename Action::Response>;
        }, "SOLAR_SYSTEM_INVALID_ACTION_HANDLER: owner must implement static Response "
           "handle(Action, const Request&)");
    static constexpr bool value = true;
};

template <typename Data, typename Components> struct ValidateDataSignature
{
    using Owner = typename FindOwner<Data, ProvidesTag, Components>::type;
    static constexpr bool query_valid = !Data::query || requires {
        { Owner::read(Data{}) } -> std::same_as<typename Data::Value>;
    };
    static constexpr bool update_valid =
        !Data::update || requires(const typename Data::Value& value) {
            { Owner::write(Data{}, value) } -> std::same_as<Result<void>>;
        };
    static_assert(query_valid,
                  "SOLAR_SYSTEM_INVALID_DATA_QUERY: provider must implement static Value "
                  "read(Data)");
    static_assert(update_valid,
                  "SOLAR_SYSTEM_INVALID_DATA_UPDATE: provider must implement static Result<void> "
                  "write(Data, const Value&)");
    static constexpr bool value = query_valid && update_valid;
};

template <typename Stream, typename Components> struct ValidatePublisherSignature
{
    using Owner = typename FindOwner<Stream, PublishesTag, Components>::type;
    static_assert(
        requires {
            { Owner::publish(Stream{}) } -> std::same_as<typename Stream::Value>;
        }, "SOLAR_SYSTEM_INVALID_STREAM_PUBLISHER: owner must implement static Value "
           "publish(Stream)");
    static constexpr bool value = true;
};

template <typename Stream, typename Components> struct ValidateConsumerSignature
{
    using Owner = typename FindOwner<Stream, ConsumesTag, Components>::type;
    static_assert(
        requires(const typename Stream::Value& value) {
            { Owner::consume(Stream{}, value) } -> std::same_as<Result<void>>;
        }, "SOLAR_SYSTEM_INVALID_STREAM_CONSUMER: owner must implement static Result<void> "
           "consume(Stream, const Value&)");
    static constexpr bool value = true;
};

template <typename Event, typename Components> struct ValidateObserverSignatures
{
    template <typename Component> static consteval bool valid_one()
    {
        if constexpr (!component_claims_v<Event, ObservesTag, Component>)
            return true;
        return requires(const typename Event::Value& value) {
            { Component::observe(Event{}, value) } -> std::same_as<void>;
        };
    }
    static constexpr bool value = []<typename... ComponentTypes>(TypeList<ComponentTypes...>) {
        return (valid_one<ComponentTypes>() && ...);
    }(Components{});
    static_assert(value, "SOLAR_SYSTEM_INVALID_EVENT_OBSERVER: observer must implement static void "
                         "observe(Event, const Value&)");
};

template <typename Metric, typename Components> struct ValidateMetricSignature
{
    using Owner = typename FindOwner<Metric, RecordsTag, Components>::type;
    static_assert(
        requires(const typename Metric::Value& value) {
            { Owner::record(Metric{}, value) } -> std::same_as<Result<void>>;
        }, "SOLAR_SYSTEM_INVALID_METRIC_RECORDER: owner must implement static Result<void> "
           "record(Metric, const Value&)");
    static constexpr bool value = true;
};

template <typename Entries, typename Components, template <typename, typename> typename Check>
struct ValidateSignatures;

template <typename Components, template <typename, typename> typename Check, typename... Entries>
struct ValidateSignatures<TypeList<Entries...>, Components, Check>
    : std::bool_constant<(Check<Entries, Components>::value && ...)>
{};

} // namespace contribution_detail

/** Validate one generated contract against all handwritten components. */
template <typename ContractT, typename ComponentsT> struct Participation
{
    using Data = typename contribution_detail::ContractData<ContractT>::type;
    using Events = typename contribution_detail::ContractEvents<ContractT>::type;
    using Metrics = typename contribution_detail::ContractMetrics<ContractT>::type;
    static_assert(TypeListType<ComponentsT>);
    static_assert(contribution_detail::ValidateAllRoles<ComponentsT, ContractT>::value);
    static_assert(contribution_detail::ExactOwners<typename ContractT::Actions, HandlesTag,
                                                   ComponentsT>::value,
                  "SOLAR_SYSTEM_ACTION_HANDLER_CARDINALITY: every action requires exactly "
                  "one handler");
    static_assert(contribution_detail::ExactOwners<Data, ProvidesTag, ComponentsT>::value,
                  "SOLAR_SYSTEM_DATA_PROVIDER_CARDINALITY: every data endpoint requires exactly "
                  "one provider");
    static_assert(contribution_detail::ExactOwners<typename ContractT::OutputStreams, PublishesTag,
                                                   ComponentsT>::value,
                  "SOLAR_SYSTEM_STREAM_PUBLISHER_CARDINALITY: every output stream requires "
                  "exactly one publisher");
    static_assert(contribution_detail::ExactOwners<typename ContractT::InputStreams, ConsumesTag,
                                                   ComponentsT>::value,
                  "SOLAR_SYSTEM_STREAM_CONSUMER_CARDINALITY: every input stream requires "
                  "exactly one consumer");
    static_assert(contribution_detail::ExactOwners<Metrics, RecordsTag, ComponentsT>::value,
                  "SOLAR_SYSTEM_METRIC_RECORDER_CARDINALITY: every metric requires exactly "
                  "one recorder");
    static_assert(contribution_detail::ValidateSignatures<
                  typename ContractT::Actions, ComponentsT,
                  contribution_detail::ValidateActionSignature>::value);
    static_assert(contribution_detail::ValidateSignatures<
                  Data, ComponentsT, contribution_detail::ValidateDataSignature>::value);
    static_assert(contribution_detail::ValidateSignatures<
                  typename ContractT::OutputStreams, ComponentsT,
                  contribution_detail::ValidatePublisherSignature>::value);
    static_assert(contribution_detail::ValidateSignatures<
                  typename ContractT::InputStreams, ComponentsT,
                  contribution_detail::ValidateConsumerSignature>::value);
    static_assert(contribution_detail::ValidateSignatures<
                  Events, ComponentsT, contribution_detail::ValidateObserverSignatures>::value);
    static_assert(contribution_detail::ValidateSignatures<
                  Metrics, ComponentsT, contribution_detail::ValidateMetricSignature>::value);
    static constexpr bool value = true;
};

/** Direct endpoint dispatch resolved from contribution ownership at compile time. */
template <typename ContractT, typename ComponentsT> struct Dispatch
{
    static_assert(Participation<ContractT, ComponentsT>::value);

    template <typename Action>
    [[nodiscard]] static typename Action::Response call(const typename Action::Request& request)
    {
        using Owner =
            typename contribution_detail::FindOwner<Action, HandlesTag, ComponentsT>::type;
        return Owner::handle(Action{}, request);
    }

    template <typename Data> [[nodiscard]] static typename Data::Value query()
    {
        using Owner = typename contribution_detail::FindOwner<Data, ProvidesTag, ComponentsT>::type;
        return Owner::read(Data{});
    }

    template <typename Data>
    [[nodiscard]] static Result<void> update(const typename Data::Value& value)
    {
        using Owner = typename contribution_detail::FindOwner<Data, ProvidesTag, ComponentsT>::type;
        return Owner::write(Data{}, value);
    }

    template <typename Stream> [[nodiscard]] static typename Stream::Value publish()
    {
        using Owner =
            typename contribution_detail::FindOwner<Stream, PublishesTag, ComponentsT>::type;
        return Owner::publish(Stream{});
    }

    template <typename Stream>
    [[nodiscard]] static Result<void> consume(const typename Stream::Value& value)
    {
        using Owner =
            typename contribution_detail::FindOwner<Stream, ConsumesTag, ComponentsT>::type;
        return Owner::consume(Stream{}, value);
    }

    template <typename Stream, typename Context>
    [[nodiscard]] static Result<void> open(const Context& context)
    {
        using Owner =
            typename contribution_detail::FindOwner<Stream, ConsumesTag, ComponentsT>::type;
        if constexpr (requires {
                          { Owner::open(Stream{}, context) } -> std::same_as<Result<void>>;
                      }) {
            return Owner::open(Stream{}, context);
        } else {
            return {};
        }
    }

    template <typename Stream, typename Context> static void close(const Context& context)
    {
        using Owner =
            typename contribution_detail::FindOwner<Stream, ConsumesTag, ComponentsT>::type;
        if constexpr (requires { Owner::close(Stream{}, context); }) {
            Owner::close(Stream{}, context);
        }
    }

    template <typename Event> static void emit(const typename Event::Value& value)
    {
        []<typename... ComponentTypes>(const typename Event::Value& event,
                                       TypeList<ComponentTypes...>) {
            (([]<typename Component>(const typename Event::Value& item) {
                 if constexpr (contribution_detail::component_claims_v<Event, ObservesTag,
                                                                       Component>) {
                     Component::observe(Event{}, item);
                 }
             }.template operator()<ComponentTypes>(event)),
             ...);
        }(value, ComponentsT{});
    }

    template <typename Metric>
    [[nodiscard]] static Result<void> record(const typename Metric::Value& value)
    {
        using Owner =
            typename contribution_detail::FindOwner<Metric, RecordsTag, ComponentsT>::type;
        return Owner::record(Metric{}, value);
    }
};

} // namespace system
} // namespace solar
