#pragma once

#include <type_traits>

#include "solar/catalog/contribution.hpp"
#include "solar/remote/catalog.hpp"

namespace solar::remote
{
template <typename... Types> using ContributeSchemas = Contribution<SchemaTag, Types...>;

template <typename... Types> using ContributeData = Contribution<DataTag, Types...>;
template <typename... Types> using ContributeActions = Contribution<ActionTag, Types...>;
template <typename... Types> using ContributeTopics = Contribution<TopicTag, Types...>;
template <typename... Types> using ContributeStreams = Contribution<StreamTag, Types...>;
template <typename... Types> using ContributeLinks = Contribution<LinkTag, Types...>;
} // namespace solar::remote

template <typename Component>
struct solar::contribution_source<solar::remote::SchemaTag, Component,
                                  std::void_t<typename Component::RemoteSchemas>>
{
    using type = typename Component::RemoteSchemas;
};

template <typename Component>
struct solar::contribution_source<solar::remote::DataTag, Component,
                                  std::void_t<typename Component::RemoteData>>
{
    using type = typename Component::RemoteData;
};

template <typename Component>
struct solar::contribution_source<solar::remote::ActionTag, Component,
                                  std::void_t<typename Component::RemoteActions>>
{
    using type = typename Component::RemoteActions;
};

template <typename Component>
struct solar::contribution_source<solar::remote::TopicTag, Component,
                                  std::void_t<typename Component::RemoteTopics>>
{
    using type = typename Component::RemoteTopics;
};

template <typename Component>
struct solar::contribution_source<solar::remote::StreamTag, Component,
                                  std::void_t<typename Component::RemoteStreams>>
{
    using type = typename Component::RemoteStreams;
};

template <typename Component>
struct solar::contribution_source<solar::remote::LinkTag, Component,
                                  std::void_t<typename Component::RemoteLinks>>
{
    using type = typename Component::RemoteLinks;
};
