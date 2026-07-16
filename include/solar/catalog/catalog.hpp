#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

#include "solar/catalog/descriptor.hpp"
#include "solar/catalog/identity.hpp"
#include "solar/catalog/provenance.hpp"
#include "solar/core/status.hpp"
#include "solar/core/type_list.hpp"

namespace solar
{

template <typename CatalogTag, typename Declaration> struct CatalogReference
{
    using Tag = CatalogTag;
    using Type = Declaration;
};

namespace catalog
{

enum class LookupError
{
    UnknownLocalId,
    UnknownStableId,
    Unavailable,
    MalformedIdentifier,
};

template <typename Tag, typename DescriptorT> struct BasicDescriptorView
{
    LocalId<Tag> local_id{};
    DescriptorT descriptor{};
    OwnerView owner{};
    OriginKind origin{OriginKind::Direct};
};

} // namespace catalog

template <typename CatalogTag, typename DeclarationT, typename OwnerT, typename OriginT,
          std::size_t LocalIndex, std::size_t OwnerIndex = LocalId<component::Tag>::invalid_value>
struct CatalogEntry
{
    using Tag = CatalogTag;
    using Declaration = DeclarationT;
    using Owner = OwnerT;
    using Origin = OriginT;

    static_assert(LocalIndex < LocalId<CatalogTag>::invalid_value);
    static_assert(OwnerIndex <= LocalId<component::Tag>::invalid_value);

    static constexpr LocalId<CatalogTag> local_id{
        static_cast<typename LocalId<CatalogTag>::Representation>(LocalIndex)};
    static constexpr LocalId<component::Tag> owner_id{
        static_cast<typename LocalId<component::Tag>::Representation>(OwnerIndex)};
    static constexpr OriginKind origin_kind = detail::origin_kind_v<OriginT>;

    static constexpr OwnerView owner_view()
    {
        if constexpr (std::is_same_v<OwnerT, ApplicationOwner>) {
            return {.kind = OwnerKind::Application};
        } else if constexpr (detail::is_builtin_owner_v<OwnerT>) {
            return {.kind = OwnerKind::Builtin, .component = owner_id};
        } else {
            return {.kind = OwnerKind::Component, .component = owner_id};
        }
    }
};

namespace detail
{

template <typename T> inline constexpr bool dependent_false_v = false;

template <typename T> struct StableIdInfo
{
    static constexpr bool valid = false;
    static constexpr bool optional = false;
};

template <typename Domain, std::unsigned_integral Rep> struct StableIdInfo<StableId<Domain, Rep>>
{
    using IdentityDomain = Domain;
    static constexpr bool valid = true;
    static constexpr bool optional = false;

    static constexpr std::optional<std::uint64_t> value(const StableId<Domain, Rep>& id)
    {
        return id.raw();
    }
};

template <typename Value, bool Valid = StableIdInfo<Value>::valid> struct OptionalStableIdInfo
{
    static constexpr bool valid = false;
    static constexpr bool optional = true;
};

template <typename Value> struct OptionalStableIdInfo<Value, true>
{
    using ValueInfo = StableIdInfo<Value>;
    using IdentityDomain = typename ValueInfo::IdentityDomain;
    static constexpr bool valid = true;
    static constexpr bool optional = true;

    static constexpr std::optional<std::uint64_t> value(const std::optional<Value>& id)
    {
        return id ? ValueInfo::value(*id) : std::nullopt;
    }
};

template <typename Value> struct StableIdInfo<std::optional<Value>> : OptionalStableIdInfo<Value>
{};

template <typename Descriptor>
concept HasStableIdMember = requires(const Descriptor& descriptor) { descriptor.stable_id; };

template <typename Descriptor>
using stable_id_member_t =
    std::remove_cvref_t<decltype(std::declval<const Descriptor&>().stable_id)>;

template <typename Descriptor>
[[nodiscard]] consteval std::optional<std::uint64_t>
descriptor_stable_id(const Descriptor& descriptor)
{
    if constexpr (HasStableIdMember<Descriptor>) {
        using Info = StableIdInfo<stable_id_member_t<Descriptor>>;
        if constexpr (Info::valid) {
            return Info::value(descriptor.stable_id);
        }
    }
    return std::nullopt;
}

template <typename Tag, typename Declaration>
inline constexpr bool descriptor_shape_valid_v = [] {
    if constexpr (!DescribedDeclaration<Tag, Declaration> || !CatalogTagType<Tag>) {
        return false;
    } else {
        using Actual =
            std::remove_cvref_t<decltype(descriptor_traits<Tag, Declaration>::descriptor)>;
        using Expected = typename catalog_traits<Tag>::Descriptor;
        return std::is_same_v<Actual, Expected> && NamedDescriptor<Actual>;
    }
}();

template <typename Tag, typename Declaration> [[nodiscard]] consteval bool custom_descriptor_valid()
{
    if constexpr (descriptor_shape_valid_v<Tag, Declaration>) {
        constexpr auto descriptor = descriptor_traits<Tag, Declaration>::descriptor;
        if constexpr (requires { catalog_traits<Tag>::validate(descriptor); }) {
            return catalog_traits<Tag>::validate(descriptor);
        }
    }
    return true;
}

template <typename Tag, typename Declaration> [[nodiscard]] consteval bool custom_declaration_valid()
{
    if constexpr (requires { catalog_traits<Tag>::template validate_declaration<Declaration>(); }) {
        return catalog_traits<Tag>::template validate_declaration<Declaration>();
    }
    return true;
}

template <typename Tag, typename Declaration> [[nodiscard]] consteval bool stable_id_domain_valid()
{
    if constexpr (descriptor_shape_valid_v<Tag, Declaration>) {
        using Descriptor = typename catalog_traits<Tag>::Descriptor;
        if constexpr (HasStableIdMember<Descriptor>) {
            using Info = StableIdInfo<stable_id_member_t<Descriptor>>;
            if constexpr (Info::valid) {
                return std::is_same_v<typename Info::IdentityDomain,
                                      typename catalog_traits<Tag>::IdentityDomain>;
            }
            return false;
        }
    }
    return true;
}

template <typename Tag, typename Declaration> [[nodiscard]] consteval bool stable_id_required()
{
    if constexpr (requires { catalog_traits<Tag>::template requires_stable_id<Declaration>; }) {
        return catalog_traits<Tag>::template requires_stable_id<Declaration>;
    }
    return false;
}

template <typename Tag, typename Declaration> [[nodiscard]] consteval bool has_stable_id()
{
    if constexpr (descriptor_shape_valid_v<Tag, Declaration>) {
        constexpr auto descriptor = descriptor_traits<Tag, Declaration>::descriptor;
        return descriptor_stable_id(descriptor).has_value();
    }
    return false;
}

template <typename Tag, typename Entry,
          bool Described = DescribedDeclaration<Tag, typename Entry::Declaration>>
struct DescriptorValidation;

template <typename Tag, typename Entry> struct DescriptorValidation<Tag, Entry, false>
{
    static_assert(dependent_false_v<Entry>, "SOLAR_DIAGNOSTIC_MISSING_DESCRIPTOR: declaration "
                                            "requires descriptor metadata for this catalog tag");
    static constexpr bool valid = false;
};

template <typename Tag, typename Entry> struct DescriptorValidation<Tag, Entry, true>
{
    using Declaration = typename Entry::Declaration;
    using Actual = std::remove_cvref_t<decltype(descriptor_traits<Tag, Declaration>::descriptor)>;
    using Expected = typename catalog_traits<Tag>::Descriptor;

    static_assert(std::is_same_v<Actual, Expected>,
                  "SOLAR_DIAGNOSTIC_WRONG_DESCRIPTOR_KIND: descriptor type does not belong to this "
                  "catalog tag");
    static_assert(NamedDescriptor<Actual>, "SOLAR_DIAGNOSTIC_DESCRIPTOR_REQUIRES_NAME: descriptor "
                                           "must expose string-view-compatible name metadata");
    static_assert(!NamedDescriptor<Actual> ||
                      !descriptor_name(descriptor_traits<Tag, Declaration>::descriptor).empty(),
                  "SOLAR_DIAGNOSTIC_EMPTY_DESCRIPTOR_NAME: descriptor name must not be empty");
    static_assert(stable_id_domain_valid<Tag, Declaration>(),
                  "SOLAR_DIAGNOSTIC_WRONG_STABLE_ID_DOMAIN: descriptor stable ID uses the wrong "
                  "identity domain");
    static_assert(
        !stable_id_required<Tag, Declaration>() || has_stable_id<Tag, Declaration>(),
        "SOLAR_DIAGNOSTIC_MISSING_STABLE_ID: declaration policy requires an explicit stable ID");
    static_assert(
        custom_descriptor_valid<Tag, Declaration>(),
        "SOLAR_DIAGNOSTIC_INVALID_DESCRIPTOR: descriptor violates catalog-specific validation");
    static_assert(custom_declaration_valid<Tag, Declaration>(),
                  "SOLAR_DIAGNOSTIC_INVALID_DECLARATION: declaration violates catalog-specific "
                  "validation");

    static constexpr bool valid = true;
};

template <typename Left, typename Right> [[nodiscard]] consteval bool duplicate_name()
{
    using Tag = typename Left::Tag;
    if constexpr (!std::is_same_v<Tag, typename Right::Tag> ||
                  !descriptor_shape_valid_v<Tag, typename Left::Declaration> ||
                  !descriptor_shape_valid_v<Tag, typename Right::Declaration>) {
        return false;
    } else {
        constexpr auto left = descriptor_traits<Tag, typename Left::Declaration>::descriptor;
        constexpr auto right = descriptor_traits<Tag, typename Right::Declaration>::descriptor;
        return descriptor_name(left) == descriptor_name(right);
    }
}

template <typename Tag> [[nodiscard]] consteval bool unique_descriptor_names()
{
    if constexpr (requires { catalog_traits<Tag>::unique_names; }) {
        return catalog_traits<Tag>::unique_names;
    }
    return true;
}

template <typename Left, typename Right> [[nodiscard]] consteval bool duplicate_stable_id()
{
    using LeftTag = typename Left::Tag;
    using RightTag = typename Right::Tag;
    if constexpr (!CatalogTagType<LeftTag> || !CatalogTagType<RightTag> ||
                  !std::is_same_v<typename catalog_traits<LeftTag>::IdentityDomain,
                                  typename catalog_traits<RightTag>::IdentityDomain> ||
                  !descriptor_shape_valid_v<LeftTag, typename Left::Declaration> ||
                  !descriptor_shape_valid_v<RightTag, typename Right::Declaration>) {
        return false;
    } else {
        constexpr auto left_descriptor =
            descriptor_traits<LeftTag, typename Left::Declaration>::descriptor;
        constexpr auto right_descriptor =
            descriptor_traits<RightTag, typename Right::Declaration>::descriptor;
        constexpr auto left = descriptor_stable_id(left_descriptor);
        constexpr auto right = descriptor_stable_id(right_descriptor);
        return left && right && *left == *right;
    }
}

template <typename... Entries> struct PairValidation;

template <> struct PairValidation<>
{
    static constexpr bool valid = true;
};

template <typename Entry> struct PairValidation<Entry>
{
    static constexpr bool valid = true;
};

template <typename Head, typename... Tail> struct PairValidation<Head, Tail...>
{
    static_assert(
        ((!std::is_same_v<typename Head::Tag, typename Tail::Tag> ||
          !std::is_same_v<typename Head::Declaration, typename Tail::Declaration>) &&
         ...),
        "SOLAR_DIAGNOSTIC_DUPLICATE_DECLARATION: declaration is registered more than once");
    static_assert(
        ((!unique_descriptor_names<typename Head::Tag>() || !duplicate_name<Head, Tail>()) && ...),
        "SOLAR_DIAGNOSTIC_DUPLICATE_DESCRIPTOR_NAME: catalog declarations must have unique names");
    static_assert(
        (!duplicate_stable_id<Head, Tail>() && ...),
        "SOLAR_DIAGNOSTIC_DUPLICATE_STABLE_ID: stable ID is reused within one identity domain");

    static constexpr bool valid = PairValidation<Tail...>::valid;
};

template <typename Declaration, typename... Entries> struct EntryLookup;

template <typename Declaration> struct EntryLookup<Declaration>
{
    static_assert(dependent_false_v<Declaration>,
                  "SOLAR_DIAGNOSTIC_UNREGISTERED_CATALOG_DECLARATION: declaration is not "
                  "registered in this catalog");
};

template <typename Declaration, typename Head, typename... Tail>
struct EntryLookup<Declaration, Head, Tail...>
    : std::conditional_t<std::is_same_v<Declaration, typename Head::Declaration>,
                         std::type_identity<Head>, EntryLookup<Declaration, Tail...>>
{};

template <typename Tag, typename Entry> [[nodiscard]] consteval auto make_descriptor_view()
{
    if constexpr (requires { catalog_traits<Tag>::template make_view<Entry>(); }) {
        return catalog_traits<Tag>::template make_view<Entry>();
    } else {
        static_assert(dependent_false_v<Entry>, "SOLAR_DIAGNOSTIC_MISSING_DESCRIPTOR_VIEW_FACTORY: "
                                                "catalog traits must materialize descriptor views");
    }
}

template <typename Entry> [[nodiscard]] consteval std::optional<std::uint64_t> entry_stable_id()
{
    using Tag = typename Entry::Tag;
    if constexpr (descriptor_shape_valid_v<Tag, typename Entry::Declaration>) {
        constexpr auto descriptor = descriptor_traits<Tag, typename Entry::Declaration>::descriptor;
        return descriptor_stable_id(descriptor);
    }
    return std::nullopt;
}

} // namespace detail

template <typename CatalogTag, typename... Entries> class Catalog
{
    static_assert(CatalogTagType<CatalogTag>,
                  "SOLAR_DIAGNOSTIC_UNREGISTERED_CATALOG_TAG: catalog_traits must be specialized "
                  "for every catalog tag");
    static_assert(
        (std::is_same_v<typename Entries::Tag, CatalogTag> && ...),
        "SOLAR_DIAGNOSTIC_CATALOG_ENTRY_TAG_MISMATCH: catalog entry belongs to another tag");
    static_assert(sizeof...(Entries) <= LocalId<CatalogTag>::invalid_value,
                  "SOLAR_DIAGNOSTIC_LOCAL_ID_CAPACITY_OVERFLOW: catalog exceeds the configured "
                  "local ID representation");
    static_assert((detail::DescriptorValidation<CatalogTag, Entries>::valid && ...));
    static_assert(detail::PairValidation<Entries...>::valid);

    using View = typename catalog_traits<CatalogTag>::DescriptorView;

    inline static constexpr std::array<View, sizeof...(Entries)> descriptor_views_{
        detail::make_descriptor_view<CatalogTag, Entries>()...};
    inline static constexpr std::array<std::optional<std::uint64_t>, sizeof...(Entries)>
        stable_ids_{detail::entry_stable_id<Entries>()...};

  public:
    using Tag = CatalogTag;
    using IdentityDomain = typename catalog_traits<CatalogTag>::IdentityDomain;
    using Descriptor = typename catalog_traits<CatalogTag>::Descriptor;
    using DescriptorView = View;
    using EntryTypes = TypeList<Entries...>;

    static constexpr std::size_t size = sizeof...(Entries);

    template <typename Declaration>
    static constexpr bool contains =
        (std::is_same_v<Declaration, typename Entries::Declaration> || ...);

    template <typename Declaration>
    using Entry = typename detail::EntryLookup<Declaration, Entries...>::type;

    template <typename Declaration> [[nodiscard]] static consteval auto entry()
    {
        return Entry<Declaration>{};
    }

    template <typename Declaration> [[nodiscard]] static consteval auto descriptor()
    {
        using Found = Entry<Declaration>;
        return descriptor_traits<CatalogTag, typename Found::Declaration>::descriptor;
    }

    [[nodiscard]] static constexpr std::span<const View> descriptors() noexcept
    {
        return descriptor_views_;
    }

    [[nodiscard]] static Result<std::reference_wrapper<const View>, catalog::LookupError>
    find(LocalId<CatalogTag> id) noexcept
    {
        if (!id.valid() || id.index() >= size) {
            return fail(catalog::LookupError::UnknownLocalId);
        }
        return std::cref(descriptor_views_[id.index()]);
    }

    template <std::unsigned_integral Rep>
    [[nodiscard]] static Result<std::reference_wrapper<const View>, catalog::LookupError>
    find(StableId<IdentityDomain, Rep> id) noexcept
    {
        for (std::size_t index = 0; index < size; ++index) {
            if (stable_ids_[index] && *stable_ids_[index] == id.raw()) {
                return std::cref(descriptor_views_[index]);
            }
        }
        return fail(catalog::LookupError::UnknownStableId);
    }
};

namespace detail
{

template <typename Tag, typename... Catalogs> struct CatalogLookup;

template <typename Tag> struct CatalogLookup<Tag>
{
    static_assert(
        dependent_false_v<Tag>,
        "SOLAR_DIAGNOSTIC_UNAVAILABLE_CATALOG: requested catalog tag is not in this catalog set");
};

template <typename Tag, typename Head, typename... Tail>
struct CatalogLookup<Tag, Head, Tail...>
    : std::conditional_t<std::is_same_v<Tag, typename Head::Tag>, std::type_identity<Head>,
                         CatalogLookup<Tag, Tail...>>
{};

template <typename Catalog> struct CatalogEntries;

template <typename Tag, typename... Entries> struct CatalogEntries<Catalog<Tag, Entries...>>
{
    using type = TypeList<Entries...>;
};

template <typename Catalog> using catalog_entries_t = typename CatalogEntries<Catalog>::type;

template <typename Tag, typename = void> struct CatalogDependencies
{
    using type = TypeList<>;
};

template <typename Tag>
struct CatalogDependencies<Tag, std::void_t<typename catalog_traits<Tag>::Dependencies>>
{
    using type = typename catalog_traits<Tag>::Dependencies;
    static_assert(
        is_type_list_v<type>,
        "SOLAR_DIAGNOSTIC_MALFORMED_CATALOG_DEPENDENCIES: catalog dependencies must be a TypeList");
};

template <typename Tag> using catalog_dependencies_t = typename CatalogDependencies<Tag>::type;

template <typename Tag, typename CatalogList> struct FindCatalog;

template <typename Tag> struct FindCatalog<Tag, TypeList<>>
{
    static constexpr bool found = false;
};

template <typename Tag, typename Head, typename... Tail>
struct FindCatalog<Tag, TypeList<Head, Tail...>>
    : std::conditional_t<std::is_same_v<Tag, typename Head::Tag>, std::type_identity<Head>,
                         FindCatalog<Tag, TypeList<Tail...>>>
{
    static constexpr bool found =
        std::is_same_v<Tag, typename Head::Tag> || FindCatalog<Tag, TypeList<Tail...>>::found;
};

template <typename Tag, typename CatalogList, typename Path, bool Available>
struct ValidateCatalogDependency;

template <typename Tag, typename CatalogList, typename Path, bool Cycle>
struct ValidateAvailableCatalogDependency;

template <typename Tag, typename CatalogList, typename Path>
struct ValidateCatalogDependency<Tag, CatalogList, Path, false>
{
    static_assert(dependent_false_v<Tag>, "SOLAR_DIAGNOSTIC_UNREGISTERED_CATALOG_DEPENDENCY: "
                                          "catalog depends on a tag absent from CatalogSet");
    static constexpr bool valid = false;
};

template <typename Dependencies, typename CatalogList, typename Path>
struct ValidateCatalogDependencies;

template <typename CatalogList, typename Path>
struct ValidateCatalogDependencies<TypeList<>, CatalogList, Path>
{
    static constexpr bool valid = true;
};

template <typename Head, typename... Tail, typename CatalogList, typename Path>
struct ValidateCatalogDependencies<TypeList<Head, Tail...>, CatalogList, Path>
{
    static constexpr bool valid =
        ValidateCatalogDependency<Head, CatalogList, Path,
                                  FindCatalog<Head, CatalogList>::found>::valid &&
        ValidateCatalogDependencies<TypeList<Tail...>, CatalogList, Path>::valid;
};

template <typename Tag, typename CatalogList, typename Path>
struct ValidateAvailableCatalogDependency<Tag, CatalogList, Path, true>
{
    static_assert(
        dependent_false_v<Tag>,
        "SOLAR_DIAGNOSTIC_CATALOG_DEPENDENCY_CYCLE: catalog dependency graph contains a cycle");
    static constexpr bool valid = false;
};

template <typename Tag, typename CatalogList, typename Path>
struct ValidateAvailableCatalogDependency<Tag, CatalogList, Path, false>
{
    static constexpr bool valid =
        ValidateCatalogDependencies<catalog_dependencies_t<Tag>, CatalogList,
                                    concat_t<Path, TypeList<Tag>>>::valid;
};

template <typename Tag, typename CatalogList, typename Path>
struct ValidateCatalogDependency<Tag, CatalogList, Path, true>
    : ValidateAvailableCatalogDependency<Tag, CatalogList, Path, contains_v<Tag, Path>>
{};

template <typename CatalogList> struct ValidateAllCatalogDependencies;

template <typename... Catalogs> struct ValidateAllCatalogDependencies<TypeList<Catalogs...>>
{
    using CatalogList = TypeList<Catalogs...>;
    static constexpr bool valid =
        (ValidateCatalogDependency<typename Catalogs::Tag, CatalogList, TypeList<>, true>::valid &&
         ...);
};

template <typename List> struct ValidateEntryPairs;

template <typename... Entries>
struct ValidateEntryPairs<TypeList<Entries...>> : PairValidation<Entries...>
{};

} // namespace detail

template <typename... Catalogs> class CatalogSet
{
    static_assert(
        unique_types_v<TypeList<typename Catalogs::Tag...>>,
        "SOLAR_DIAGNOSTIC_DUPLICATE_CATALOG_TAG: CatalogSet contains the same tag more than once");

    using AllEntries = concat_t<detail::catalog_entries_t<Catalogs>...>;
    static_assert(detail::ValidateEntryPairs<AllEntries>::valid);
    static_assert(detail::ValidateAllCatalogDependencies<TypeList<Catalogs...>>::valid);

  public:
    using CatalogTypes = TypeList<Catalogs...>;
    static constexpr std::size_t size = sizeof...(Catalogs);

    template <typename Tag>
    static constexpr bool contains = (std::is_same_v<Tag, typename Catalogs::Tag> || ...);

    template <typename Tag> using Of = typename detail::CatalogLookup<Tag, Catalogs...>::type;
};

namespace detail
{

template <typename CatalogSetT, typename Reference> struct ResolveCatalogReference;

template <typename... Catalogs, typename Tag, typename Declaration>
struct ResolveCatalogReference<CatalogSet<Catalogs...>, CatalogReference<Tag, Declaration>>
{
    using CatalogType = typename CatalogSet<Catalogs...>::template Of<Tag>;
    using type = typename CatalogType::template Entry<Declaration>;
};

template <typename Catalog, template <typename> typename Predicate> struct FilterCatalog;

template <typename Tag, typename... Entries, template <typename> typename Predicate>
struct FilterCatalog<Catalog<Tag, Entries...>, Predicate>
{
    using type = filter_t<TypeList<Entries...>, Predicate>;
};

} // namespace detail

template <typename CatalogSetT, typename Reference>
using resolve_catalog_reference_t =
    typename detail::ResolveCatalogReference<CatalogSetT, Reference>::type;

template <typename Catalog, template <typename> typename Predicate>
using filter_catalog_t = typename detail::FilterCatalog<Catalog, Predicate>::type;

} // namespace solar
