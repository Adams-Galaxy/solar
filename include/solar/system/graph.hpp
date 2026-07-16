#pragma once

#include <type_traits>

#include "solar/system/sections.hpp"

namespace solar
{

namespace detail
{

template <typename T> struct IsDependencies : std::false_type
{};

template <typename... Types> struct IsDependencies<Dependencies<Types...>> : std::true_type
{};

template <typename Component, typename = void> struct DependenciesOf
{
    using type = TypeList<>;
};

template <typename Component>
struct DependenciesOf<Component, std::void_t<typename Component::Dependencies>>
{
    using Authored = typename Component::Dependencies;
    static_assert(IsDependencies<Authored>::value,
                  "SOLAR_DIAGNOSTIC_MALFORMED_COMPONENT_DEPENDENCIES: Dependencies must use "
                  "solar::Dependencies");
    using type = typename Authored::Entries;
};

template <typename Component> using dependencies_of_t = typename DependenciesOf<Component>::type;

template <typename Component, typename Candidates, typename AllComponents>
struct GeneratedDependencies;

template <typename Component, typename AllComponents>
struct GeneratedDependencies<Component, TypeList<>, AllComponents>
{
    using type = TypeList<>;
};

template <typename Component, typename Head, typename... Tail, typename AllComponents>
struct GeneratedDependencies<Component, TypeList<Head, Tail...>, AllComponents>
{
  private:
    using Remaining =
        typename GeneratedDependencies<Component, TypeList<Tail...>, AllComponents>::type;

  public:
    using type =
        std::conditional_t<generated_component_dependency<Component, Head, AllComponents>::value,
                           concat_t<TypeList<Head>, Remaining>, Remaining>;
};

template <typename Component, typename AllComponents>
using effective_dependencies_of_t = unique_t<
    concat_t<dependencies_of_t<Component>,
             typename GeneratedDependencies<Component, AllComponents, AllComponents>::type>>;

template <typename Component, typename AllComponents> struct ValidateComponentDependencies;

template <typename Component, typename... AllComponents>
struct ValidateComponentDependencies<Component, TypeList<AllComponents...>>
{
    template <typename Dependencies> struct Validate;

    template <typename... Required> struct Validate<TypeList<Required...>>
    {
        static_assert((contains_v<Required, TypeList<AllComponents...>> && ...),
                      "SOLAR_DIAGNOSTIC_MISSING_COMPONENT_DEPENDENCY: component requires an "
                      "unregistered component");
        static constexpr bool valid = true;
    };

    static constexpr bool valid =
        Validate<effective_dependencies_of_t<Component, TypeList<AllComponents...>>>::valid;
};

template <typename Component, typename AllComponents, typename Path, bool Cycle>
struct ValidateComponentVisit;

template <typename Component, typename AllComponents, typename Path>
struct ValidateComponentVisit<Component, AllComponents, Path, true>
{
    static_assert(
        dependent_false_v<Component>,
        "SOLAR_DIAGNOSTIC_COMPONENT_DEPENDENCY_CYCLE: component dependency graph contains a cycle");
    static constexpr bool valid = false;
};

template <typename Dependencies, typename AllComponents, typename Path>
struct ValidateDependencyVisits;

template <typename AllComponents, typename Path>
struct ValidateDependencyVisits<TypeList<>, AllComponents, Path>
{
    static constexpr bool valid = true;
};

template <typename Head, typename... Tail, typename AllComponents, typename Path>
struct ValidateDependencyVisits<TypeList<Head, Tail...>, AllComponents, Path>
{
    static constexpr bool valid =
        ValidateComponentVisit<Head, AllComponents, Path, contains_v<Head, Path>>::valid &&
        ValidateDependencyVisits<TypeList<Tail...>, AllComponents, Path>::valid;
};

template <typename Component, typename AllComponents, typename Path>
struct ValidateComponentVisit<Component, AllComponents, Path, false>
{
    static constexpr bool valid =
        ValidateDependencyVisits<effective_dependencies_of_t<Component, AllComponents>,
                                 AllComponents, concat_t<Path, TypeList<Component>>>::valid;
};

template <typename Components> struct ValidateComponentGraph;

template <typename... Components> struct ValidateComponentGraph<TypeList<Components...>>
{
    using All = TypeList<Components...>;
    static_assert(unique_types_v<All>, "SOLAR_DIAGNOSTIC_DUPLICATE_COMPONENT: component appears "
                                       "more than once or in incompatible categories");
    static constexpr bool valid =
        (ValidateComponentDependencies<Components, All>::valid && ...) &&
        (ValidateComponentVisit<Components, All, TypeList<>, false>::valid && ...);
};

template <typename Dependencies, typename Sorted> struct DependenciesSatisfied;

template <typename... Dependencies, typename Sorted>
struct DependenciesSatisfied<TypeList<Dependencies...>, Sorted>
    : std::bool_constant<(contains_v<Dependencies, Sorted> && ...)>
{};

template <bool Ready, typename Head, typename Tail, typename Sorted, typename AllComponents>
struct FirstReadyStep;

template <typename Head, typename Tail, typename Sorted, typename AllComponents>
struct FirstReadyStep<true, Head, Tail, Sorted, AllComponents>
{
    using type = Head;
};

template <typename Remaining, typename Sorted, typename AllComponents> struct FirstReady;

template <typename Head, typename... Tail, typename Sorted, typename AllComponents>
struct FirstReady<TypeList<Head, Tail...>, Sorted, AllComponents>
    : FirstReadyStep<
          DependenciesSatisfied<effective_dependencies_of_t<Head, AllComponents>, Sorted>::value,
          Head, TypeList<Tail...>, Sorted, AllComponents>
{};

template <typename Head, typename Tail, typename Sorted, typename AllComponents>
struct FirstReadyStep<false, Head, Tail, Sorted, AllComponents>
    : FirstReady<Tail, Sorted, AllComponents>
{};

template <typename Needle, typename List> struct RemoveFirst;

template <typename Needle> struct RemoveFirst<Needle, TypeList<>>
{
    using type = TypeList<>;
};

template <typename Needle, typename Head, typename... Tail>
struct RemoveFirst<Needle, TypeList<Head, Tail...>>
{
  private:
    using Remaining = typename RemoveFirst<Needle, TypeList<Tail...>>::type;

  public:
    using type = std::conditional_t<std::is_same_v<Needle, Head>, TypeList<Tail...>,
                                    concat_t<TypeList<Head>, Remaining>>;
};

template <typename Remaining, typename Sorted, typename AllComponents> struct TopologicalSort;

template <typename Sorted, typename AllComponents>
struct TopologicalSort<TypeList<>, Sorted, AllComponents>
{
    using type = Sorted;
};

template <typename... Remaining, typename Sorted, typename AllComponents>
struct TopologicalSort<TypeList<Remaining...>, Sorted, AllComponents>
{
    using Ready = typename FirstReady<TypeList<Remaining...>, Sorted, AllComponents>::type;
    using Rest = typename RemoveFirst<Ready, TypeList<Remaining...>>::type;
    using type =
        typename TopologicalSort<Rest, concat_t<Sorted, TypeList<Ready>>, AllComponents>::type;
};

template <typename List> struct ReverseList;

template <> struct ReverseList<TypeList<>>
{
    using type = TypeList<>;
};

template <typename Head, typename... Tail> struct ReverseList<TypeList<Head, Tail...>>
{
    using type = concat_t<typename ReverseList<TypeList<Tail...>>::type, TypeList<Head>>;
};

} // namespace detail

template <typename Components> struct ComponentGraph
{
    static_assert(TypeListType<Components>);
    static_assert(detail::ValidateComponentGraph<Components>::valid);

    using ComponentTypes = Components;
    using TopologicalOrder =
        typename detail::TopologicalSort<Components, TypeList<>, Components>::type;
    using ReverseTopologicalOrder = typename detail::ReverseList<TopologicalOrder>::type;
    static constexpr std::size_t size = list_size_v<Components>;

    template <typename Component>
    using DependenciesOf = detail::effective_dependencies_of_t<Component, Components>;
};

} // namespace solar
