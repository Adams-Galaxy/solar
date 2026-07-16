#pragma once

#include <type_traits>

#include "solar/catalog/contribution.hpp"
#include "solar/parameters/types.hpp"

namespace solar::parameters
{

struct DefaultChangeRouteTag
{};

template <typename RouteTag, typename Parameter, typename Handler> struct ChangeRoute
{
    using RouteTagType = RouteTag;
    using ParameterType = Parameter;
    using HandlerType = Handler;
};

template <typename Observer, typename Parameter, typename Handler = Observer,
          typename RouteTag = DefaultChangeRouteTag>
struct BoundChange
{
    using ObserverType = Observer;
    using ParameterType = Parameter;
    using HandlerType = Handler;
    using RouteTagType = RouteTag;
};

template <typename Declaration> struct change_traits
{
    static constexpr bool valid = false;
};

template <typename Observer, typename Parameter, typename Handler, typename RouteTag>
struct change_traits<BoundChange<Observer, Parameter, Handler, RouteTag>>
{
    static constexpr bool valid = true;
    using ObserverType = Observer;
    using ParameterType = Parameter;
    using HandlerType = Handler;
    using RouteTagType = RouteTag;
};

template <typename Observer, typename Declaration> struct BindChange
{
    using type = BoundChange<Observer, Declaration>;
};

template <typename Observer, typename RouteTag, typename Parameter, typename Handler>
struct BindChange<Observer, ChangeRoute<RouteTag, Parameter, Handler>>
{
    using type = BoundChange<Observer, Parameter, Handler, RouteTag>;
};

template <typename ChangeDeclaration>
inline constexpr ChangeDescriptor change_descriptor{
    .name =
        descriptor_traits<Tag, typename change_traits<ChangeDeclaration>::ParameterType>::descriptor
            .name,
};

} // namespace solar::parameters

template <typename Component, typename Declaration>
struct solar::contributed_declaration<solar::parameters::ChangeTag, Component, Declaration>
{
    using type = typename solar::parameters::BindChange<Component, Declaration>::type;
};

template <typename ChangeDeclaration>
    requires solar::parameters::change_traits<ChangeDeclaration>::valid
struct solar::descriptor_traits<solar::parameters::ChangeTag, ChangeDeclaration>
{
    static constexpr auto descriptor = solar::parameters::change_descriptor<ChangeDeclaration>;
};
