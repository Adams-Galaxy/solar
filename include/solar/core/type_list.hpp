#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace solar
{

template <typename... Types>
struct TypeList
{
    static constexpr std::size_t size = sizeof...(Types);
};

/**
 * @brief Statically declared robot devices.
 *
 * Entries are graph components: each entry must expose `using Name = solar::Name<"...">`.
 */
template <typename... Types>
struct Devices : TypeList<Types...>
{
};

/**
 * @brief Statically declared hardware/peripheral objects owned by the system.
 */
template <typename... Types>
struct Peripherals : TypeList<Types...>
{
};

/**
 * @brief Active runtime actors. Services are expected to own runtime behavior.
 */
template <typename... Types>
struct Services : TypeList<Types...>
{
};

/**
 * @brief Explicit Kernel task graph entries.
 */
template <typename... Types>
struct Tasks : TypeList<Types...>
{
};

/**
 * @brief Typed bounded communication channels owned by the system.
 */
template <typename... Types>
struct Channels : TypeList<Types...>
{
};

/**
 * @brief Passive system capabilities. Facilities do not own threads.
 */
template <typename... Types>
struct Facilities : TypeList<Types...>
{
};

/**
 * @brief Required compile-time dependencies by component type.
 */
template <typename... ComponentTypes>
struct Dependencies : TypeList<ComponentTypes...>
{
};

namespace detail
{

template <typename... Lists>
struct ConcatLists;

template <>
struct ConcatLists<>
{
    using type = TypeList<>;
};

template <typename List>
struct ConcatLists<List>
{
    using type = List;
};

/**
 * @brief Concatenate lists that use the same variadic list template.
 */
template <template <typename...> typename ListT, typename... Left, typename... Right, typename... Rest>
struct ConcatLists<ListT<Left...>, ListT<Right...>, Rest...>
{
    using type = typename ConcatLists<ListT<Left..., Right...>, Rest...>::type;
};

template <typename... Lists>
using ConcatListsT = typename ConcatLists<Lists...>::type;

template <typename List>
struct ListSize;

template <template <typename...> typename ListT, typename... Types>
struct ListSize<ListT<Types...>> : std::integral_constant<std::size_t, sizeof...(Types)>
{
};

template <typename T, typename = void>
struct HasComponentName : std::false_type
{
};

template <typename T>
struct HasComponentName<T, std::void_t<typename T::Name>> : std::true_type
{
};

template <typename T>
inline constexpr bool HasComponentNameV = HasComponentName<T>::value;

template <typename T, typename = void>
struct DependenciesOf
{
    using type = Dependencies<>;
};

template <typename T>
struct DependenciesOf<T, std::void_t<typename T::Dependencies>>
{
    using type = typename T::Dependencies;
};

template <typename T>
using DependenciesOfT = typename DependenciesOf<T>::type;

template <typename Needle, typename... Types>
inline constexpr std::size_t TypeCountV = (std::size_t{0} + ... + (std::is_same_v<Needle, Types> ? 1U : 0U));

template <typename... Types>
struct UniqueTypes;

template <>
struct UniqueTypes<> : std::true_type
{
};

template <typename Head, typename... Tail>
struct UniqueTypes<Head, Tail...>
    : std::bool_constant<(TypeCountV<Head, Head, Tail...> == 1U) && UniqueTypes<Tail...>::value>
{
};

template <typename Needle, typename... Types>
inline constexpr bool ContainsTypeV = (std::is_same_v<Needle, Types> || ...);

template <typename Needle, typename... Types>
struct CountName;

template <typename Needle>
struct CountName<Needle> : std::integral_constant<std::size_t, 0>
{
};

template <typename Needle, typename Head, typename... Tail>
struct CountName<Needle, Head, Tail...>
    : std::integral_constant<std::size_t,
                             (std::is_same_v<typename Head::Name, Needle> ? 1U : 0U) +
                                 CountName<Needle, Tail...>::value>
{
};

template <typename... Types>
struct UniqueNames;

template <>
struct UniqueNames<> : std::true_type
{
};

template <typename Head, typename... Tail>
struct UniqueNames<Head, Tail...>
    : std::bool_constant<(CountName<typename Head::Name, Head, Tail...>::value == 1U) &&
                         UniqueNames<Tail...>::value>
{
};

template <typename Needle, typename... Types>
struct ContainsName : std::bool_constant<(CountName<Needle, Types...>::value > 0U)>
{
};

template <typename List>
struct UniqueNamesInList;

template <template <typename...> typename ListT, typename... Types>
struct UniqueNamesInList<ListT<Types...>> : UniqueNames<Types...>
{
};

template <typename List>
inline constexpr bool UniqueNamesInListV = UniqueNamesInList<List>::value;

template <std::uint32_t Needle, typename... Types>
struct CountId;

template <std::uint32_t Needle>
struct CountId<Needle> : std::integral_constant<std::size_t, 0>
{
};

template <std::uint32_t Needle, typename Head, typename... Tail>
struct CountId<Needle, Head, Tail...>
    : std::integral_constant<std::size_t, (Head::id == Needle ? 1U : 0U) + CountId<Needle, Tail...>::value>
{
};

template <typename... Types>
struct UniqueIds;

template <>
struct UniqueIds<> : std::true_type
{
};

template <typename Head, typename... Tail>
struct UniqueIds<Head, Tail...>
    : std::bool_constant<(CountId<Head::id, Head, Tail...>::value == 1U) && UniqueIds<Tail...>::value>
{
};

template <typename List>
struct UniqueIdsInList;

template <template <typename...> typename ListT, typename... Types>
struct UniqueIdsInList<ListT<Types...>> : UniqueIds<Types...>
{
};

template <typename List>
inline constexpr bool UniqueIdsInListV = UniqueIdsInList<List>::value;

template <std::uint32_t Needle, typename... Types>
struct CountTypeId;

template <std::uint32_t Needle>
struct CountTypeId<Needle> : std::integral_constant<std::size_t, 0>
{
};

template <std::uint32_t Needle, typename Head, typename... Tail>
struct CountTypeId<Needle, Head, Tail...>
    : std::integral_constant<std::size_t, (Head::Type == Needle ? 1U : 0U) + CountTypeId<Needle, Tail...>::value>
{
};

template <typename... Types>
struct UniqueTypeIds;

template <>
struct UniqueTypeIds<> : std::true_type
{
};

template <typename Head, typename... Tail>
struct UniqueTypeIds<Head, Tail...>
    : std::bool_constant<(CountTypeId<Head::Type, Head, Tail...>::value == 1U) && UniqueTypeIds<Tail...>::value>
{
};

template <typename List>
struct UniqueTypeIdsInList;

template <template <typename...> typename ListT, typename... Types>
struct UniqueTypeIdsInList<ListT<Types...>> : UniqueTypeIds<Types...>
{
};

template <typename List>
inline constexpr bool UniqueTypeIdsInListV = UniqueTypeIdsInList<List>::value;

template <typename Deps, typename... Components>
struct DependenciesAvailable;

template <typename... Deps, typename... Components>
struct DependenciesAvailable<Dependencies<Deps...>, Components...>
    : std::bool_constant<(ContainsTypeV<Deps, Components...> && ...)>
{
};

template <typename Components, typename... AllComponents>
struct AllDependenciesAvailable;

template <typename... Components, typename... AllComponents>
struct AllDependenciesAvailable<TypeList<Components...>, AllComponents...>
    : std::bool_constant<(DependenciesAvailable<DependenciesOfT<Components>, AllComponents...>::value && ...)>
{
};

template <typename List>
struct AllNamed;

template <typename... Types>
struct AllNamed<TypeList<Types...>> : std::bool_constant<(HasComponentNameV<Types> && ...)>
{
};

} // namespace detail

template <typename PeripheralsGroup, typename DevicesGroup, typename FacilitiesGroup, typename ServicesGroup, typename TasksGroup, typename ChannelsGroup>
struct GraphValid;

/**
 * @brief Compile-time graph validation for Solar component lists.
 *
 * Validation checks concrete type uniqueness, diagnostic-name uniqueness, and
 * required dependency presence. Cycle validation is performed by `System`,
 * where the board and complete component set are available.
 */
template <typename... PeripheralTypes, typename... DeviceTypes, typename... FacilityTypes, typename... ServiceTypes, typename... TaskTypes, typename... ChannelTypes>
struct GraphValid<Peripherals<PeripheralTypes...>, Devices<DeviceTypes...>, Facilities<FacilityTypes...>, Services<ServiceTypes...>, Tasks<TaskTypes...>, Channels<ChannelTypes...>>
    : std::bool_constant<
          detail::AllNamed<TypeList<PeripheralTypes..., DeviceTypes..., FacilityTypes..., ServiceTypes..., TaskTypes..., ChannelTypes...>>::value &&
          detail::UniqueTypes<PeripheralTypes..., DeviceTypes..., FacilityTypes..., ServiceTypes..., TaskTypes..., ChannelTypes...>::value &&
          detail::UniqueNames<PeripheralTypes..., DeviceTypes..., FacilityTypes..., ServiceTypes..., TaskTypes..., ChannelTypes...>::value &&
          detail::AllDependenciesAvailable<TypeList<PeripheralTypes..., DeviceTypes..., FacilityTypes..., ServiceTypes..., TaskTypes..., ChannelTypes...>,
                                           PeripheralTypes..., DeviceTypes..., FacilityTypes..., ServiceTypes..., TaskTypes..., ChannelTypes...>::value>
{
};

template <typename PeripheralsGroup, typename DevicesGroup, typename FacilitiesGroup, typename ServicesGroup, typename TasksGroup, typename ChannelsGroup>
inline constexpr bool GraphValidV = GraphValid<PeripheralsGroup, DevicesGroup, FacilitiesGroup, ServicesGroup, TasksGroup, ChannelsGroup>::value;

} // namespace solar
