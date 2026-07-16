#pragma once

#include <type_traits>

#include "solar/catalog/contribution.hpp"

namespace solar::metrics
{
struct Tag;
template <typename... Types> using Metrics = Contribution<Tag, Types...>;
template <typename... Types> using Contribute = Metrics<Types...>;
} // namespace solar::metrics

template <typename Component>
struct solar::contribution_source<solar::metrics::Tag, Component,
                                  std::void_t<typename Component::Metrics>>
{
    using type = typename Component::Metrics;
};
