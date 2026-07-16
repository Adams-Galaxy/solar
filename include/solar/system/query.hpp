#pragma once

#include <array>
#include <cstddef>

#include "solar/system/binding.hpp"

namespace solar::graph
{

namespace detail
{

template <typename System, typename... Dependencies>
[[nodiscard]] consteval auto dependency_descriptors(TypeList<Dependencies...>)
{
    using Catalog = typename System::Catalogs::template Of<component::Tag>;
    constexpr auto descriptors = Catalog::descriptors();
    return std::array<component::DescriptorView, sizeof...(Dependencies)>{
        descriptors[Catalog::template Entry<Dependencies>::local_id.index()]...};
}

} // namespace detail

template <typename Application = DefaultApplication> struct Of
{
    using System = bound_system_t<Application>;

    [[nodiscard]] static consteval auto components()
    {
        return System::catalog::components();
    }

    template <typename Component> [[nodiscard]] static consteval auto dependencies()
    {
        static_assert(contains_v<Component, typename System::Components>,
                      "SOLAR_DIAGNOSTIC_UNREGISTERED_GRAPH_COMPONENT: requested component is "
                      "absent from the effective System graph");
        using Dependencies = typename System::Graph::template DependenciesOf<Component>;
        return detail::dependency_descriptors<System>(Dependencies{});
    }
};

template <typename Application = DefaultApplication> [[nodiscard]] consteval auto components()
{
    return Of<Application>::components();
}

template <typename Component, typename Application = DefaultApplication>
[[nodiscard]] consteval auto dependencies()
{
    return Of<Application>::template dependencies<Component>();
}

} // namespace solar::graph
