#pragma once

#include <concepts>
#include <string_view>
#include <type_traits>

namespace solar
{

template <typename CatalogTag> struct catalog_traits;

template <typename CatalogTag, typename Declaration, typename = void> struct descriptor_override
{};

template <typename CatalogTag, typename Declaration, typename = void> struct descriptor_traits
{};

template <typename CatalogTag, typename Declaration>
struct descriptor_traits<CatalogTag, Declaration, std::void_t<decltype(Declaration::descriptor)>>
{
    static constexpr auto descriptor = [] {
        if constexpr (requires { descriptor_override<CatalogTag, Declaration>::descriptor; }) {
            return descriptor_override<CatalogTag, Declaration>::descriptor;
        } else {
            return Declaration::descriptor;
        }
    }();
};

template <typename CatalogTag, typename Declaration>
concept DescribedDeclaration = requires { descriptor_traits<CatalogTag, Declaration>::descriptor; };

template <typename CatalogTag>
concept CatalogTagType = requires {
    typename catalog_traits<CatalogTag>::Descriptor;
    typename catalog_traits<CatalogTag>::DescriptorView;
    typename catalog_traits<CatalogTag>::IdentityDomain;
};

namespace detail
{

template <typename Descriptor>
concept NamedDescriptor = requires(const Descriptor& descriptor) {
    { std::string_view{descriptor.name} } -> std::same_as<std::string_view>;
};

template <typename Descriptor>
[[nodiscard]] consteval std::string_view descriptor_name(const Descriptor& descriptor)
{
    if constexpr (NamedDescriptor<Descriptor>) {
        return std::string_view{descriptor.name};
    }
    return {};
}

} // namespace detail

namespace catalog
{

#if defined(CONFIG_SOLAR) && !defined(CONFIG_SOLAR_DESCRIPTOR_STRINGS)
inline constexpr bool descriptor_strings_enabled = false;
#else
inline constexpr bool descriptor_strings_enabled = true;
#endif

template <typename Descriptor>
[[nodiscard]] consteval Descriptor descriptor_for_view(Descriptor descriptor)
{
    if constexpr (!descriptor_strings_enabled) {
        if constexpr (requires { descriptor.name = {}; }) {
            descriptor.name = {};
        }
        if constexpr (requires { descriptor.description = {}; }) {
            descriptor.description = {};
        }
    }
    return descriptor;
}

} // namespace catalog

} // namespace solar
