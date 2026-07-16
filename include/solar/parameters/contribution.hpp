#pragma once

#include <type_traits>

#include "solar/catalog/contribution.hpp"
#include "solar/parameters/change.hpp"

namespace solar::parameters
{
template <typename... Types> using Parameters = Contribution<Tag, Types...>;
template <typename... Types> using Changes = Contribution<ChangeTag, Types...>;
template <typename... Types> using Contribute = Parameters<Types...>;
template <typename... Types> using ContributeChanges = Changes<Types...>;
} // namespace solar::parameters

template <typename Component>
struct solar::contribution_source<solar::parameters::Tag, Component,
                                  std::void_t<typename Component::Parameters>>
{
    using type = typename Component::Parameters;
};

template <typename Component>
struct solar::contribution_source<solar::parameters::ChangeTag, Component,
                                  std::void_t<typename Component::ParameterChanges>>
{
    using type = typename Component::ParameterChanges;
};
