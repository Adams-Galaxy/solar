#pragma once

#include <type_traits>

#include "solar/execution/registration.hpp"
#include "solar/log/types.hpp"
#include "solar/system/sections.hpp"

namespace solar::log
{

struct Tag;
struct CompileLevelAxis;
template <typename Source> struct SourceLevelAxis;
template <typename Domain> struct DomainLevelAxis;
struct ProcessorExecutorAxis;
struct RoutesAxis;
struct StopAxis;

template <Level Value> struct CompileLevel
{
    static constexpr Level value = Value;
};

template <typename Source, Level Value> struct SourceLevel
{
    using SourceType = Source;
    static constexpr Level value = Value;
};

template <typename Domain, Level Value> struct DomainLevel
{
    using DomainType = Domain;
    static constexpr Level value = Value;
};

template <typename Executor> struct ProcessorOn
{
    using ExecutorType = Executor;
};

namespace stop
{
struct Drain
{};
struct CancelPending
{};
} // namespace stop

template <typename Policy> struct StopWith
{
    using PolicyType = Policy;
};

template <Level Value> struct MinimumLevel
{
    static constexpr Level value = Value;
};

template <Level Value> struct MaximumLevel
{
    static constexpr Level value = Value;
};

namespace panic
{
struct Safe
{};
struct Unsafe
{};
} // namespace panic

namespace format
{
struct Compact
{};
struct Detailed
{};
struct Encoded
{};
} // namespace format

template <typename SinkT, typename... Policies> struct To
{
    using Sink = SinkT;
    using PolicyTypes = TypeList<Policies...>;
};

template <typename... Routes> struct Sinks
{
    using RouteTypes = TypeList<Routes...>;
};

namespace detail
{

inline constexpr Level kconfig_compile_level =
#if defined(CONFIG_SOLAR_LOG_COMPILE_LEVEL_TRACE)
    Level::Trace;
#elif defined(CONFIG_SOLAR_LOG_COMPILE_LEVEL_INFO)
    Level::Info;
#elif defined(CONFIG_SOLAR_LOG_COMPILE_LEVEL_NOTICE)
    Level::Notice;
#elif defined(CONFIG_SOLAR_LOG_COMPILE_LEVEL_WARNING)
    Level::Warning;
#elif defined(CONFIG_SOLAR_LOG_COMPILE_LEVEL_ERROR)
    Level::Error;
#else
    Level::Debug;
#endif

[[nodiscard]] constexpr Level stricter(Level first, Level second) noexcept
{
    return at_least(first, second) ? first : second;
}

template <typename Policy> inline constexpr Level policy_level = Level::Trace;

template <Level Value> inline constexpr Level policy_level<CompileLevel<Value>> = Value;

template <typename Source, Level Value>
inline constexpr Level policy_level<SourceLevel<Source, Value>> = Value;

template <typename Domain, Level Value>
inline constexpr Level policy_level<DomainLevel<Domain, Value>> = Value;

template <typename List> struct FindRoutes
{
    using type = Sinks<>;
};

template <typename... Routes, typename... Tail>
struct FindRoutes<TypeList<Sinks<Routes...>, Tail...>>
{
    using type = Sinks<Routes...>;
};

template <typename Head, typename... Tail> struct FindRoutes<TypeList<Head, Tail...>>
    : FindRoutes<TypeList<Tail...>>
{};

template <typename List> using routes_t = typename FindRoutes<List>::type;

template <typename Policy, typename = void> struct RouteMinimum
{
    static constexpr bool present = false;
    static constexpr Level value = Level::Trace;
};

template <Level Value> struct RouteMinimum<MinimumLevel<Value>>
{
    static constexpr bool present = true;
    static constexpr Level value = Value;
};

template <typename Policy> struct RoutePanicSafety : std::false_type
{};

template <> struct RoutePanicSafety<panic::Safe> : std::true_type
{};

template <typename Policy> struct RouteEncoding : std::false_type
{};

template <> struct RouteEncoding<format::Encoded> : std::true_type
{};

template <typename Policies> struct MinimumFrom
{
    static constexpr Level value = Level::Trace;
};

template <typename Head, typename... Tail> struct MinimumFrom<TypeList<Head, Tail...>>
{
    static constexpr Level value =
        RouteMinimum<Head>::present ? RouteMinimum<Head>::value
                                    : MinimumFrom<TypeList<Tail...>>::value;
};

template <typename Route> struct route_traits;

template <typename SinkT, typename... Policies> struct route_traits<To<SinkT, Policies...>>
{
    using Sink = SinkT;
    using PoliciesList = TypeList<Policies...>;
    static constexpr Level minimum = MinimumFrom<PoliciesList>::value;
    static constexpr bool panic_safe = (RoutePanicSafety<Policies>::value || ... || false);
    static constexpr bool encoded = (RouteEncoding<Policies>::value || ... || false);
};

} // namespace detail

} // namespace solar::log

template <solar::log::Level Value>
struct solar::subsystem_policy_traits<solar::log::Tag, solar::log::CompileLevel<Value>>
{
    static constexpr bool recognized = true;
    using Axis = solar::log::CompileLevelAxis;
};

template <typename Source, solar::log::Level Value>
struct solar::subsystem_policy_traits<solar::log::Tag, solar::log::SourceLevel<Source, Value>>
{
    static constexpr bool recognized = true;
    using Axis = solar::log::SourceLevelAxis<Source>;
};

template <typename Domain, solar::log::Level Value>
struct solar::subsystem_policy_traits<solar::log::Tag, solar::log::DomainLevel<Domain, Value>>
{
    static constexpr bool recognized = true;
    using Axis = solar::log::DomainLevelAxis<Domain>;
};

template <typename Executor>
struct solar::subsystem_policy_traits<solar::log::Tag, solar::log::ProcessorOn<Executor>>
{
    static constexpr bool recognized = true;
    using Axis = solar::log::ProcessorExecutorAxis;
};

template <typename... Routes>
struct solar::subsystem_policy_traits<solar::log::Tag, solar::log::Sinks<Routes...>>
{
    static constexpr bool recognized = true;
    using Axis = solar::log::RoutesAxis;
};

template <typename Policy>
struct solar::subsystem_policy_traits<solar::log::Tag, solar::log::StopWith<Policy>>
{
    static constexpr bool recognized = true;
    using Axis = solar::log::StopAxis;
};
