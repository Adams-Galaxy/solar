#pragma once

#include <cstddef>
#include <type_traits>

#include "solar/catalog/catalog.hpp"
#include "solar/catalog/contribution.hpp"

namespace solar
{

template <typename CatalogTag, typename... Declarations> struct DirectDeclarations
{
    using Tag = CatalogTag;
    using Entries = TypeList<Declarations...>;
};

template <typename... CatalogDeclarations> struct DirectCatalogs
{
    using Entries = TypeList<CatalogDeclarations...>;
};

namespace detail
{

template <typename CatalogTag, typename DeclarationT, typename OwnerT, typename OriginT,
          std::size_t OwnerIndex>
struct PendingCatalogEntry
{
    using Tag = CatalogTag;
    using Declaration = DeclarationT;
    using Owner = OwnerT;
    using Origin = OriginT;
    static constexpr std::size_t owner_index = OwnerIndex;
};

template <typename Tag, typename Declarations, typename Owner, typename Origin,
          std::size_t OwnerIndex>
struct WrapDeclarations;

template <typename Tag, typename... Declarations, typename Owner, typename Origin,
          std::size_t OwnerIndex>
struct WrapDeclarations<Tag, TypeList<Declarations...>, Owner, Origin, OwnerIndex>
{
    using type = TypeList<PendingCatalogEntry<Tag, Declarations, Owner, Origin, OwnerIndex>...>;
};

template <typename Tag, typename Declaration, typename = void> struct ExpansionFor
{
    static constexpr bool expands = false;
    using type = TypeList<Declaration>;
};

template <typename Tag, typename Declaration>
struct ExpansionFor<Tag, Declaration,
                    std::void_t<typename catalog_traits<Tag>::template expand<Declaration>>>
{
    static constexpr bool expands = true;
    using type = typename catalog_traits<Tag>::template expand<Declaration>;
    static_assert(
        is_type_list_v<type>,
        "SOLAR_DIAGNOSTIC_MALFORMED_CATALOG_EXPANSION: catalog expansion must produce a TypeList");
};

template <typename Pending, typename Seen> struct ExpandPending;

template <typename Pending, typename Seen, bool Expands> struct ExpandPendingImpl;

template <typename PendingList, typename Seen> struct ExpandPendingList;

template <typename Seen> struct ExpandPendingList<TypeList<>, Seen>
{
    using type = TypeList<>;
};

template <typename Head, typename... Tail, typename Seen>
struct ExpandPendingList<TypeList<Head, Tail...>, Seen>
{
    using type = concat_t<typename ExpandPending<Head, Seen>::type,
                          typename ExpandPendingList<TypeList<Tail...>, Seen>::type>;
};

template <typename Tag, typename Declaration, typename Owner, typename Origin,
          std::size_t OwnerIndex, typename Seen>
struct ExpandPendingImpl<PendingCatalogEntry<Tag, Declaration, Owner, Origin, OwnerIndex>, Seen,
                         false>
{
    using type = TypeList<PendingCatalogEntry<Tag, Declaration, Owner, Origin, OwnerIndex>>;
};

template <typename Tag, typename Declaration, typename Owner, typename Origin,
          std::size_t OwnerIndex, typename Seen>
struct ExpandPendingImpl<PendingCatalogEntry<Tag, Declaration, Owner, Origin, OwnerIndex>, Seen,
                         true>
{
    static_assert(
        !contains_v<Declaration, Seen>,
        "SOLAR_DIAGNOSTIC_RECURSIVE_CATALOG_EXPANSION: catalog expansion contains a cycle");

    using ExpandedOrigin = origin::Expansion<Origin, Declaration>;
    using Wrapped = typename WrapDeclarations<Tag, typename ExpansionFor<Tag, Declaration>::type,
                                              Owner, ExpandedOrigin, OwnerIndex>::type;

    using type = typename ExpandPendingList<Wrapped, concat_t<Seen, TypeList<Declaration>>>::type;
};

template <typename Tag, typename Declaration, typename Owner, typename Origin,
          std::size_t OwnerIndex, typename Seen>
struct ExpandPending<PendingCatalogEntry<Tag, Declaration, Owner, Origin, OwnerIndex>, Seen>
    : ExpandPendingImpl<PendingCatalogEntry<Tag, Declaration, Owner, Origin, OwnerIndex>, Seen,
                        ExpansionFor<Tag, Declaration>::expands>
{};

template <typename PendingList>
using expand_pending_t = typename ExpandPendingList<PendingList, TypeList<>>::type;

template <typename PendingList, std::size_t Index = 0> struct FinalizeEntries;

template <std::size_t Index> struct FinalizeEntries<TypeList<>, Index>
{
    using type = TypeList<>;
};

template <typename Head, typename... Tail, std::size_t Index>
struct FinalizeEntries<TypeList<Head, Tail...>, Index>
{
    using Finalized =
        CatalogEntry<typename Head::Tag, typename Head::Declaration, typename Head::Owner,
                     typename Head::Origin, Index, Head::owner_index>;
    using type =
        concat_t<TypeList<Finalized>, typename FinalizeEntries<TypeList<Tail...>, Index + 1>::type>;
};

template <typename Tag, typename Components, std::size_t ComponentIndex = 0>
struct ComponentContributionEntries;

template <typename Tag, std::size_t ComponentIndex>
struct ComponentContributionEntries<Tag, TypeList<>, ComponentIndex>
{
    using type = TypeList<>;
};

template <typename Tag, typename Component, typename... Tail, std::size_t ComponentIndex>
struct ComponentContributionEntries<Tag, TypeList<Component, Tail...>, ComponentIndex>
{
  private:
    using Declarations = contributed_declarations_t<Tag, Component>;
    using Current =
        typename WrapDeclarations<Tag, Declarations, Component, origin::Contribution<Component>,
                                  ComponentIndex>::type;
    using Remaining =
        typename ComponentContributionEntries<Tag, TypeList<Tail...>, ComponentIndex + 1>::type;

  public:
    using type = concat_t<Current, Remaining>;
};

template <typename Tag, typename Direct> struct DirectEntries;

template <typename Tag, typename... Declarations>
struct DirectEntries<Tag, DirectDeclarations<Tag, Declarations...>>
{
    using type =
        typename WrapDeclarations<Tag, TypeList<Declarations...>, ApplicationOwner, origin::Direct,
                                  LocalId<component::Tag>::invalid_value>::type;
};

template <typename Tag, typename DirectList> struct DirectFor;

template <typename Tag> struct DirectFor<Tag, DirectCatalogs<>>
{
    using type = DirectDeclarations<Tag>;
};

template <typename Tag, typename Head, typename... Tail>
struct DirectFor<Tag, DirectCatalogs<Head, Tail...>>
{
    static_assert(
        requires {
            typename Head::Tag;
            typename Head::Entries;
        }, "SOLAR_DIAGNOSTIC_MALFORMED_DIRECT_CATALOG: direct catalog source has an invalid shape");

  private:
    using Remaining = typename DirectFor<Tag, DirectCatalogs<Tail...>>::type;

    template <typename Source, bool Matches> struct Select
    {
        using type = TypeList<>;
    };

    template <typename Source> struct Select<Source, true>
    {
        using type = typename Source::Entries;
    };

    using Current = typename Select<Head, std::is_same_v<typename Head::Tag, Tag>>::type;

    template <typename RemainingDirect> struct RemainingEntries;

    template <typename RemainingTag, typename... RemainingDeclarations>
    struct RemainingEntries<DirectDeclarations<RemainingTag, RemainingDeclarations...>>
    {
        using type = TypeList<RemainingDeclarations...>;
    };

    using Combined = concat_t<Current, typename RemainingEntries<Remaining>::type>;

    template <typename List> struct Rebind;

    template <typename... Declarations> struct Rebind<TypeList<Declarations...>>
    {
        using type = DirectDeclarations<Tag, Declarations...>;
    };

  public:
    using type = typename Rebind<Combined>::type;
};

template <typename Tag, typename Direct, typename Components> struct CollectCatalog;

template <typename Tag, typename... DirectEntriesT, typename... Components>
struct CollectCatalog<Tag, DirectDeclarations<Tag, DirectEntriesT...>, TypeList<Components...>>
{
  private:
    using DirectPending =
        typename DirectEntries<Tag, DirectDeclarations<Tag, DirectEntriesT...>>::type;
    using ContributedPending =
        typename ComponentContributionEntries<Tag, TypeList<Components...>>::type;
    using Expanded = expand_pending_t<concat_t<DirectPending, ContributedPending>>;
    using Finalized = typename FinalizeEntries<Expanded>::type;

    template <typename List> struct Build;

    template <typename... Entries> struct Build<TypeList<Entries...>>
    {
        using type = Catalog<Tag, Entries...>;
    };

  public:
    using type = typename Build<Finalized>::type;
};

template <typename Group, typename CandidateTags, bool Valid = is_contribution_v<Group>>
struct ValidateGenericGroup;

template <typename Group, typename CandidateTags>
struct ValidateGenericGroup<Group, CandidateTags, false>
{
    static_assert(dependent_false_v<Group>, "SOLAR_DIAGNOSTIC_UNKNOWN_CONTRIBUTION_GROUP: generic "
                                            "Contributions contains a malformed group");
    static constexpr bool valid = false;
};

template <typename Group, typename CandidateTags>
struct ValidateGenericGroup<Group, CandidateTags, true>
{
    static_assert(contains_v<typename Group::Tag, CandidateTags>,
                  "SOLAR_DIAGNOSTIC_UNREGISTERED_EXTENSION_TAG: contribution uses a tag absent "
                  "from the candidate tag set");
    static constexpr bool valid = true;
};

template <typename Groups, typename CandidateTags> struct ValidateGenericGroups;

template <typename... Groups, typename CandidateTags>
struct ValidateGenericGroups<Contributions<Groups...>, CandidateTags>
{
    static constexpr bool valid = (ValidateGenericGroup<Groups, CandidateTags>::valid && ...);
};

template <typename Component, typename CandidateTags>
struct ValidateComponentGenericGroups
    : ValidateGenericGroups<generic_contributions_t<Component>, CandidateTags>
{};

template <typename CandidateTags, typename Components, typename Direct> struct CollectCatalogSet;

template <typename... Tags, typename... Components, typename Direct>
struct CollectCatalogSet<TypeList<Tags...>, TypeList<Components...>, Direct>
{
    static_assert(
        unique_types_v<TypeList<Tags...>>,
        "SOLAR_DIAGNOSTIC_DUPLICATE_CANDIDATE_TAG: candidate catalog tags must be unique");
    static_assert((ValidateComponentGenericGroups<Components, TypeList<Tags...>>::valid && ...));

    using type = CatalogSet<typename CollectCatalog<Tags, typename DirectFor<Tags, Direct>::type,
                                                    TypeList<Components...>>::type...>;
};

} // namespace detail

template <typename Tag, typename Direct, TypeListType Components>
using collect_catalog_t = typename detail::CollectCatalog<Tag, Direct, Components>::type;

template <TypeListType CandidateTags, TypeListType Components, typename Direct = DirectCatalogs<>>
using collect_catalog_set_t =
    typename detail::CollectCatalogSet<CandidateTags, Components, Direct>::type;

} // namespace solar
