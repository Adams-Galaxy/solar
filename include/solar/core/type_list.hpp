#pragma once

#include <cstddef>
#include <type_traits>
#include <utility>

namespace solar
{

template <typename... Types> struct TypeList
{
    static constexpr std::size_t size = sizeof...(Types);
};

template <typename T> struct IsTypeList : std::false_type
{};

template <typename... Types> struct IsTypeList<TypeList<Types...>> : std::true_type
{};

template <typename T> inline constexpr bool is_type_list_v = IsTypeList<T>::value;

template <typename T>
concept TypeListType = is_type_list_v<T>;

template <TypeListType List> inline constexpr std::size_t list_size_v = List::size;

namespace detail
{

template <typename... Lists> struct Concat;

template <> struct Concat<>
{
    using type = TypeList<>;
};

template <typename... Types> struct Concat<TypeList<Types...>>
{
    using type = TypeList<Types...>;
};

template <typename... Left, typename... Right, typename... Rest>
struct Concat<TypeList<Left...>, TypeList<Right...>, Rest...>
    : Concat<TypeList<Left..., Right...>, Rest...>
{};

template <typename Needle, typename... Types>
inline constexpr std::size_t count_v =
    (std::size_t{0} + ... + (std::is_same_v<Needle, Types> ? 1U : 0U));

template <typename Result, typename... Remaining> struct Unique;

template <typename... Result> struct Unique<TypeList<Result...>>
{
    using type = TypeList<Result...>;
};

template <typename... Result, typename Head, typename... Tail>
struct Unique<TypeList<Result...>, Head, Tail...>
    : std::conditional_t<(std::is_same_v<Head, Result> || ...),
                         Unique<TypeList<Result...>, Tail...>,
                         Unique<TypeList<Result..., Head>, Tail...>>
{};

template <typename List, template <typename> typename Predicate> struct Filter;

template <template <typename> typename Predicate> struct Filter<TypeList<>, Predicate>
{
    using type = TypeList<>;
};

template <typename Head, typename... Tail, template <typename> typename Predicate>
struct Filter<TypeList<Head, Tail...>, Predicate>
{
  private:
    using filtered_tail = typename Filter<TypeList<Tail...>, Predicate>::type;

  public:
    using type =
        std::conditional_t<Predicate<Head>::value,
                           typename Concat<TypeList<Head>, filtered_tail>::type, filtered_tail>;
};

template <typename List, template <typename> typename Operation> struct TransformList;

template <typename... Types, template <typename> typename Operation>
struct TransformList<TypeList<Types...>, Operation>
{
    using type = TypeList<typename Operation<Types>::type...>;
};

template <std::size_t Index, typename List> struct TypeAt;

template <typename Head, typename... Tail> struct TypeAt<0, TypeList<Head, Tail...>>
{
    using type = Head;
};

template <std::size_t Index, typename Head, typename... Tail>
struct TypeAt<Index, TypeList<Head, Tail...>> : TypeAt<Index - 1, TypeList<Tail...>>
{};

} // namespace detail

template <TypeListType... Lists> using concat_t = typename detail::Concat<Lists...>::type;

template <typename Needle, TypeListType List> struct ListCount;

template <typename Needle, typename... Types>
struct ListCount<Needle, TypeList<Types...>>
    : std::integral_constant<std::size_t, detail::count_v<Needle, Types...>>
{};

template <typename Needle, TypeListType List>
inline constexpr std::size_t list_count_v = ListCount<Needle, List>::value;

template <typename Needle, TypeListType List>
inline constexpr bool contains_v = list_count_v<Needle, List> != 0;

template <TypeListType List> struct UniqueTypes;

template <typename... Types>
struct UniqueTypes<TypeList<Types...>>
    : std::bool_constant<((detail::count_v<Types, Types...> == 1) && ...)>
{};

template <TypeListType List> inline constexpr bool unique_types_v = UniqueTypes<List>::value;

template <TypeListType List> struct MakeUnique;

template <typename... Types> struct MakeUnique<TypeList<Types...>>
{
    using type = typename detail::Unique<TypeList<>, Types...>::type;
};

template <TypeListType List> using unique_t = typename MakeUnique<List>::type;

template <TypeListType List, template <typename> typename Predicate>
using filter_t = typename detail::Filter<List, Predicate>::type;

template <TypeListType List, template <typename> typename Operation>
using transform_t = typename detail::TransformList<List, Operation>::type;

template <std::size_t Index, TypeListType List>
using type_at_t = typename detail::TypeAt<Index, List>::type;

template <TypeListType List, typename Function> constexpr void for_each_type(Function&& function)
{
    []<typename... Types>(TypeList<Types...>, Function&& callable) {
        (callable.template operator()<Types>(), ...);
    }(List{}, std::forward<Function>(function));
}

} // namespace solar
