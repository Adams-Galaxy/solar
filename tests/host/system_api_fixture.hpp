#pragma once

#include <solar/system/frontend.hpp>

#include "catalog_fixture.hpp"

namespace system_fixture
{

struct ControlValue
{
    static constexpr catalog_fixture::alpha::Descriptor descriptor{
        .name = "system.control_value",
        .stable_id = catalog_fixture::alpha::Id{0x2001},
    };
};

struct AbsentValue
{
    static constexpr catalog_fixture::alpha::Descriptor descriptor{
        .name = "system.absent_value",
        .stable_id = catalog_fixture::alpha::Id{0x2002},
    };
};

struct ControlState;

struct ControlPolicy
{
    using CatalogTag = catalog_fixture::alpha::Tag;

    template <typename Declaration>
    using Signature = solar::Result<int, solar::frontend::Error>(int);

    template <typename System, typename Declaration>
    static solar::Result<int, solar::frontend::Error> invoke(int amount)
    {
        auto& value = System::template StateSlot<Declaration, ControlState, int>::value;
        value += amount;
        return value;
    }
};

struct RelaxedClient
{
#if defined(CONFIG_SOLAR_STRICT_CATALOG_BINDING)
    static solar::Result<int, solar::frontend::Error> increment(int amount);
#else
    static solar::Result<int, solar::frontend::Error> increment(int amount)
    {
        return solar::frontend::Operation<ControlPolicy, ControlValue>::call(amount);
    }
#endif
};

struct StrictClient
{
    static solar::Result<int, solar::frontend::Error> increment(int amount);
};

struct LazyClient
{
    template <typename Application = solar::DefaultApplication>
    static solar::Result<int, solar::frontend::Error> increment(int amount)
    {
        return solar::frontend::Operation<ControlPolicy, ControlValue, Application>::call(amount);
    }
};

} // namespace system_fixture
