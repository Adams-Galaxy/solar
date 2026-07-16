#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace solar
{

template <typename CatalogTag, std::unsigned_integral Rep = std::uint16_t> struct LocalId
{
    using Tag = CatalogTag;
    using Representation = Rep;

    static constexpr Rep invalid_value = std::numeric_limits<Rep>::max();

    Rep value{invalid_value};

    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return value != invalid_value;
    }

    [[nodiscard]] constexpr std::size_t index() const noexcept
    {
        return static_cast<std::size_t>(value);
    }

    constexpr explicit operator bool() const noexcept
    {
        return valid();
    }

    constexpr bool operator==(const LocalId&) const = default;
};

template <typename IdentityDomain, std::unsigned_integral Rep = std::uint32_t> struct StableId
{
    using Domain = IdentityDomain;
    using Representation = Rep;

    Rep value{};

    [[nodiscard]] constexpr std::uint64_t raw() const noexcept
    {
        return static_cast<std::uint64_t>(value);
    }

    constexpr bool operator==(const StableId&) const = default;
};

template <typename T> struct IsLocalId : std::false_type
{};

template <typename Tag, std::unsigned_integral Rep>
struct IsLocalId<LocalId<Tag, Rep>> : std::true_type
{};

template <typename T>
inline constexpr bool is_local_id_v = IsLocalId<std::remove_cvref_t<T>>::value;

template <typename T> struct IsStableId : std::false_type
{};

template <typename Domain, std::unsigned_integral Rep>
struct IsStableId<StableId<Domain, Rep>> : std::true_type
{};

template <typename T>
inline constexpr bool is_stable_id_v = IsStableId<std::remove_cvref_t<T>>::value;

} // namespace solar
