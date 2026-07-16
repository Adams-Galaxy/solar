#pragma once

#include <type_traits>

#include "solar/catalog/contribution.hpp"
#include "solar/execution/types.hpp"

namespace solar::execution
{
#if defined(CONFIG_SOLAR) && !defined(CONFIG_SOLAR_EXECUTION)
inline constexpr bool enabled = false;
#else
inline constexpr bool enabled = true;
#endif

template <typename... Types> using Tasks = Contribution<Tag, Types...>;
template <typename... Types> using ContributeRegistrations = Contribution<Tag, Types...>;
} // namespace solar::execution

template <typename Component>
struct solar::contribution_source<solar::execution::Tag, Component,
                                  std::void_t<typename Component::Tasks>>
{
    using type = typename Component::Tasks;
};
