#pragma once

#include <cstdint>
#include <type_traits>

#include "solar/catalog/identity.hpp"

namespace solar
{

namespace component
{
struct Tag;
}

struct ApplicationOwner
{};

template <typename Provider> struct BuiltinOwner
{
    using Type = Provider;
};

namespace origin
{

struct Direct
{};

template <typename Component> struct Contribution
{
    using Source = Component;
};

template <typename Provider> struct Builtin
{
    using Source = Provider;
};

template <typename Rule, typename SourceDeclaration> struct Derived
{
    using Derivation = Rule;
    using Source = SourceDeclaration;
};

template <typename Manifest> struct Generated
{
    using Source = Manifest;
};

template <typename ParentOrigin, typename GroupDeclaration> struct Expansion
{
    using Parent = ParentOrigin;
    using Group = GroupDeclaration;
};

} // namespace origin

enum class OriginKind : std::uint8_t
{
    Direct,
    Contribution,
    Builtin,
    Derived,
    Generated,
    Expansion,
};

enum class OwnerKind : std::uint8_t
{
    Application,
    Component,
    Builtin,
};

struct OwnerView
{
    OwnerKind kind{OwnerKind::Application};
    LocalId<component::Tag> component{};

    constexpr bool operator==(const OwnerView&) const = default;
};

namespace detail
{

template <typename T> struct IsBuiltinOwner : std::false_type
{};

template <typename Provider> struct IsBuiltinOwner<BuiltinOwner<Provider>> : std::true_type
{};

template <typename T> inline constexpr bool is_builtin_owner_v = IsBuiltinOwner<T>::value;

template <typename T> struct OriginKindOf;

template <>
struct OriginKindOf<origin::Direct> : std::integral_constant<OriginKind, OriginKind::Direct>
{};

template <typename Component>
struct OriginKindOf<origin::Contribution<Component>>
    : std::integral_constant<OriginKind, OriginKind::Contribution>
{};

template <typename Provider>
struct OriginKindOf<origin::Builtin<Provider>>
    : std::integral_constant<OriginKind, OriginKind::Builtin>
{};

template <typename Rule, typename Source>
struct OriginKindOf<origin::Derived<Rule, Source>>
    : std::integral_constant<OriginKind, OriginKind::Derived>
{};

template <typename Manifest>
struct OriginKindOf<origin::Generated<Manifest>>
    : std::integral_constant<OriginKind, OriginKind::Generated>
{};

template <typename Parent, typename Group>
struct OriginKindOf<origin::Expansion<Parent, Group>>
    : std::integral_constant<OriginKind, OriginKind::Expansion>
{};

template <typename Origin> inline constexpr OriginKind origin_kind_v = OriginKindOf<Origin>::value;

} // namespace detail

} // namespace solar
