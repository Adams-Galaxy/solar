#pragma once

#include <type_traits>

#include "solar/core/type_list.hpp"

namespace solar
{

template <typename CatalogTag, typename... Declarations> struct Contribution
{
    using Tag = CatalogTag;
    using Entries = TypeList<Declarations...>;
};

template <typename... Groups> struct Contributions
{
    using Entries = TypeList<Groups...>;
};

template <typename CatalogTag, typename Component, typename = void> struct contribution_source
{
    using type = Contribution<CatalogTag>;
};

template <typename CatalogTag, typename Component>
using contribution_source_t = typename contribution_source<CatalogTag, Component>::type;

template <typename CatalogTag, typename Component, typename Declaration>
struct contributed_declaration
{
    using type = Declaration;
};

template <typename T> struct IsContribution : std::false_type
{};

template <typename Tag, typename... Declarations>
struct IsContribution<Contribution<Tag, Declarations...>> : std::true_type
{};

template <typename T> inline constexpr bool is_contribution_v = IsContribution<T>::value;

template <typename T> struct IsContributions : std::false_type
{};

template <typename... Groups> struct IsContributions<Contributions<Groups...>> : std::true_type
{};

template <typename T> inline constexpr bool is_contributions_v = IsContributions<T>::value;

namespace detail
{

template <typename Component, typename = void> struct GenericContributions
{
    using type = Contributions<>;
};

template <typename Component>
struct GenericContributions<Component, std::void_t<typename Component::Contributions>>
{
  private:
    using Authored = typename Component::Contributions;

  public:
    static_assert(is_contribution_v<Authored> || is_contributions_v<Authored>,
                  "SOLAR_DIAGNOSTIC_MALFORMED_CONTRIBUTIONS: Contributions must be a Contribution "
                  "or Contributions group");

    using type = std::conditional_t<is_contribution_v<Authored>, Contributions<Authored>, Authored>;
};

template <typename Component>
using generic_contributions_t = typename GenericContributions<Component>::type;

template <typename Tag, typename Source, bool Valid = is_contribution_v<Source>>
struct NormalizeContributionSource;

template <typename Tag, typename Source> struct NormalizeContributionSource<Tag, Source, false>
{
    static_assert(is_contribution_v<Source>, "SOLAR_DIAGNOSTIC_MALFORMED_RESERVED_ALIAS: reserved "
                                             "aliases must name a Solar Contribution");
    using type = TypeList<>;
};

template <typename Tag, typename Source> struct NormalizeContributionSource<Tag, Source, true>
{
    static_assert(std::is_same_v<typename Source::Tag, Tag>,
                  "SOLAR_DIAGNOSTIC_WRONG_CONTRIBUTION_TAG: reserved alias contribution uses the "
                  "wrong catalog tag");
    using type = typename Source::Entries;
};

template <typename Tag, typename Groups> struct GenericEntriesFor;

template <typename Tag> struct GenericEntriesFor<Tag, Contributions<>>
{
    using type = TypeList<>;
};

template <typename Tag, typename Head, typename... Tail>
struct GenericEntriesFor<Tag, Contributions<Head, Tail...>>
{
    static_assert(is_contribution_v<Head>, "SOLAR_DIAGNOSTIC_UNKNOWN_CONTRIBUTION_GROUP: generic "
                                           "Contributions contains a malformed group");

  private:
    using Remaining = typename GenericEntriesFor<Tag, Contributions<Tail...>>::type;

    template <typename Group, bool Valid> struct Select
    {
        using type = TypeList<>;
    };

    template <typename Group> struct Select<Group, true>
    {
        using type = std::conditional_t<std::is_same_v<typename Group::Tag, Tag>,
                                        typename Group::Entries, TypeList<>>;
    };

    using Selected = typename Select<Head, is_contribution_v<Head>>::type;

  public:
    using type = concat_t<Selected, Remaining>;
};

template <typename Tag, typename Component> struct ContributedDeclarations
{
  private:
    using Conventional =
        typename NormalizeContributionSource<Tag, contribution_source_t<Tag, Component>>::type;
    using Generic = typename GenericEntriesFor<Tag, generic_contributions_t<Component>>::type;

    template <typename Declaration> struct BindOwner
    {
        using type = typename contributed_declaration<Tag, Component, Declaration>::type;
    };

  public:
    using type = transform_t<concat_t<Conventional, Generic>, BindOwner>;
};

template <typename Tag, typename Component>
using contributed_declarations_t = typename ContributedDeclarations<Tag, Component>::type;

} // namespace detail

} // namespace solar
