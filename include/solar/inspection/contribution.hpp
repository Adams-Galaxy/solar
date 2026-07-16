#pragma once

#include <type_traits>

#include "solar/catalog/contribution.hpp"
#include "solar/inspection/catalog.hpp"

namespace solar::inspection
{
template <typename... Types> using Collections = Contribution<Tag, Types...>;
template <typename... Types> using Contribute = Collections<Types...>;
} // namespace solar::inspection

template <typename Component>
struct solar::contribution_source<solar::inspection::Tag, Component,
                                  std::void_t<typename Component::Inspections>>
{
    using type = typename Component::Inspections;
};
