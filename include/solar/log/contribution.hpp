#pragma once

#include "solar/catalog/contribution.hpp"
#include "solar/log/declaration.hpp"

namespace solar::log
{

struct Tag;

template <typename... Types> using Sources = Contribution<SourceTag, Types...>;
template <typename... Types> using Domains = Contribution<DomainTag, Types...>;

} // namespace solar::log

template <typename Component>
struct solar::contribution_source<solar::log::SourceTag, Component,
                                  std::void_t<typename Component::LogSources>>
{
    using type = typename Component::LogSources;
};

template <typename Component>
struct solar::contribution_source<solar::log::DomainTag, Component,
                                  std::void_t<typename Component::LogDomains>>
{
    using type = typename Component::LogDomains;
};
