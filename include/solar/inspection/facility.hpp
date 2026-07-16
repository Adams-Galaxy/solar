#pragma once

#include "solar/component.hpp"
#include "solar/inspection/contribution.hpp"
#include "solar/system/sections.hpp"

namespace solar::inspection
{

#if defined(CONFIG_SOLAR_INSPECTION)
inline constexpr bool enabled = true;
#else
inline constexpr bool enabled = false;
#endif

struct Facility
{
    using Dependencies = solar::Dependencies<>;

    static constexpr component::Descriptor descriptor{
        .name = "solar.inspection",
        .description = "Passive generic query adapters",
    };
};

} // namespace solar::inspection

template <> struct solar::builtin_traits<solar::inspection::Facility>
{
    static constexpr bool enabled = solar::inspection::enabled;
    static constexpr bool always_present = solar::inspection::enabled;
    using Requirements = solar::TypeList<>;

    template <typename> static constexpr bool demanded = solar::inspection::enabled;
};
