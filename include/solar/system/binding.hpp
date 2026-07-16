#pragma once

#include <type_traits>

#include "solar/system/system.hpp"

#if defined(CONFIG_SOLAR_REMOTE)
#include "solar/remote/manifest.hpp"
#endif

namespace solar
{

struct DefaultApplication
{};

template <typename Application = DefaultApplication> struct system_binding
{
    static constexpr bool bound = false;
};

namespace detail
{

template <typename Application>
inline constexpr bool SOLAR_DIAGNOSTIC_DUPLICATE_SYSTEM_BINDING = false;

template <typename SystemT> struct BindingDefinition
{
    static_assert(SystemType<SystemT>, "SOLAR_DIAGNOSTIC_INVALID_BOUND_SYSTEM: application binding "
                                       "must select solar::System<Blueprint>");
    static constexpr bool bound = true;
    using System = SystemT;
};

template <typename Application, bool Bound = system_binding<Application>::bound> struct BoundSystem;

template <typename Application> struct BoundSystem<Application, false>
{
    static_assert(dependent_false_v<Application>, "SOLAR_DIAGNOSTIC_MISSING_SYSTEM_BINDING: no "
                                                  "Solar System is bound for this application tag");
};

template <typename Application> struct BoundSystem<Application, true>
{
    using type = typename system_binding<Application>::System;
    static_assert(SystemType<type>, "SOLAR_DIAGNOSTIC_INVALID_BOUND_SYSTEM: application binding "
                                    "must select solar::System<Blueprint>");
};

} // namespace detail

template <typename Application = DefaultApplication>
using bound_system_t = typename detail::BoundSystem<Application>::type;

} // namespace solar

#define SOLAR_BIND_SYSTEM_FOR(APPLICATION, SYSTEM)                                                 \
    template <>                                                                                    \
    inline constexpr bool solar::detail::SOLAR_DIAGNOSTIC_DUPLICATE_SYSTEM_BINDING<APPLICATION> =  \
        true;                                                                                      \
    template <>                                                                                    \
    struct solar::system_binding<APPLICATION> : solar::detail::BindingDefinition<SYSTEM>           \
    {                                                                                              \
        using SOLAR_DIAGNOSTIC_DUPLICATE_SYSTEM_BINDING = void;                                    \
    }

#if defined(CONFIG_SOLAR_REMOTE)
#define SOLAR_DETAIL_DEFAULT_BINDING_ARTIFACT(SYSTEM) SOLAR_REMOTE_EMIT_MANIFEST(SYSTEM)
#else
#define SOLAR_DETAIL_DEFAULT_BINDING_ARTIFACT(SYSTEM)
#endif

#define SOLAR_BIND_SYSTEM(SYSTEM)                                                              \
    SOLAR_BIND_SYSTEM_FOR(solar::DefaultApplication, SYSTEM);                                  \
    SOLAR_DETAIL_DEFAULT_BINDING_ARTIFACT(SYSTEM)
