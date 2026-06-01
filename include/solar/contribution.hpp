#pragma once

#include <type_traits>

#include "solar/core/type_list.hpp"
#include "solar/events/catalog.hpp"
#include "solar/metrics/catalog.hpp"
#include "solar/remote/schema.hpp"

namespace solar
{

namespace detail
{

template <typename T, typename = void>
struct MetricsOf
{
    using type = solar::metrics::List<>;
};

template <typename T>
struct MetricsOf<T, std::void_t<typename T::Metrics::SolarCatalogKind>>
{
    using type = typename T::Metrics;
};

template <typename T, typename = void>
struct EventsOf
{
    using type = solar::events::List<>;
};

template <typename T>
struct EventsOf<T, std::void_t<typename T::Events::SolarCatalogKind>>
{
    using type = typename T::Events;
};

template <typename T, typename = void>
struct RemoteTypesOf
{
    using type = solar::remote::Types<>;
};

template <typename T>
struct RemoteTypesOf<T, std::void_t<typename T::RemoteTypes::SolarCatalogKind>>
{
    using type = typename T::RemoteTypes;
};

template <typename T, typename = void>
struct RemoteMethodsOf
{
    using type = solar::remote::Methods<>;
};

template <typename T>
struct RemoteMethodsOf<T, std::void_t<typename T::RemoteMethods::SolarCatalogKind>>
{
    using type = typename T::RemoteMethods;
};

template <typename T, typename = void>
struct RemoteTopicsOf
{
    using type = solar::remote::Topics<>;
};

template <typename T>
struct RemoteTopicsOf<T, std::void_t<typename T::RemoteTopics::SolarCatalogKind>>
{
    using type = typename T::RemoteTopics;
};

template <typename T, typename = void>
struct RemoteObservablesOf
{
    using type = solar::remote::Observables<>;
};

template <typename T>
struct RemoteObservablesOf<T, std::void_t<typename T::RemoteObservables::SolarCatalogKind>>
{
    using type = typename T::RemoteObservables;
};

/**
 * @brief Optional contribution aliases exposed by a graph component.
 *
 * A contribution means "this component owns/provides this vocabulary." It does
 * not mean the component merely consumes or observes another component's data.
 */
template <typename T>
struct ContributionsOf
{
    using Metrics = typename MetricsOf<T>::type;
    using Events = typename EventsOf<T>::type;
    using RemoteTypes = typename RemoteTypesOf<T>::type;
    using RemoteMethods = typename RemoteMethodsOf<T>::type;
    using RemoteTopics = typename RemoteTopicsOf<T>::type;
    using RemoteObservables = typename RemoteObservablesOf<T>::type;
};

template <typename List>
struct MetricsContributionList;

template <typename... Types>
struct MetricsContributionList<TypeList<Types...>>
{
    using type = ConcatListsT<typename ContributionsOf<Types>::Metrics...>;
};

template <typename List>
struct EventsContributionList;

template <typename... Types>
struct EventsContributionList<TypeList<Types...>>
{
    using type = ConcatListsT<typename ContributionsOf<Types>::Events...>;
};

template <typename List>
struct RemoteTypesContributionList;

template <typename... Types>
struct RemoteTypesContributionList<TypeList<Types...>>
{
    using type = ConcatListsT<typename ContributionsOf<Types>::RemoteTypes...>;
};

template <typename List>
struct RemoteMethodsContributionList;

template <typename... Types>
struct RemoteMethodsContributionList<TypeList<Types...>>
{
    using type = ConcatListsT<typename ContributionsOf<Types>::RemoteMethods...>;
};

template <typename List>
struct RemoteTopicsContributionList;

template <typename... Types>
struct RemoteTopicsContributionList<TypeList<Types...>>
{
    using type = ConcatListsT<typename ContributionsOf<Types>::RemoteTopics...>;
};

template <typename List>
struct RemoteObservablesContributionList;

template <typename... Types>
struct RemoteObservablesContributionList<TypeList<Types...>>
{
    using type = ConcatListsT<typename ContributionsOf<Types>::RemoteObservables...>;
};

} // namespace detail

/**
 * @brief Merge metric contributions from each provided component type.
 *
 * Components opt in by declaring `using Metrics = solar::metrics::List<...>`.
 * Missing aliases contribute an empty list.
 */
template <typename... Types>
using CollectMetrics = typename detail::MetricsContributionList<TypeList<Types...>>::type;

/**
 * @brief Merge event contributions from each provided component type.
 *
 * Components opt in by declaring `using Events = solar::events::List<...>`.
 */
template <typename... Types>
using CollectEvents = typename detail::EventsContributionList<TypeList<Types...>>::type;

/**
 * @brief Merge Remote type contributions from each provided component type.
 *
 * These catalogs feed static Remote descriptor generation and validation.
 */
template <typename... Types>
using CollectRemoteTypes = typename detail::RemoteTypesContributionList<TypeList<Types...>>::type;

/**
 * @brief Merge Remote method contributions from each provided component type.
 */
template <typename... Types>
using CollectRemoteMethods = typename detail::RemoteMethodsContributionList<TypeList<Types...>>::type;

/**
 * @brief Merge Remote topic contributions from each provided component type.
 */
template <typename... Types>
using CollectRemoteTopics = typename detail::RemoteTopicsContributionList<TypeList<Types...>>::type;

/**
 * @brief Merge Remote observable contributions from each provided component type.
 */
template <typename... Types>
using CollectRemoteObservables = typename detail::RemoteObservablesContributionList<TypeList<Types...>>::type;

} // namespace solar
