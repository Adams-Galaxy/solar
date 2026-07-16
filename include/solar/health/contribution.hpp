#pragma once

#include <type_traits>

#include "solar/catalog/contribution.hpp"
#include "solar/health/declaration.hpp"

namespace solar::health
{
template <typename... Types> using Checks = Contribution<CheckTag, Types...>;
} // namespace solar::health

template <typename Component>
struct solar::contribution_source<solar::health::CheckTag, Component,
                                  std::void_t<typename Component::Health::Checks>>
{
    using type = typename Component::Health::Checks;
};

template <typename Component, typename Declaration>
struct solar::contributed_declaration<solar::health::CheckTag, Component, Declaration>
{
    using type = solar::health::OwnedMonitor<Component, Declaration>;
};
