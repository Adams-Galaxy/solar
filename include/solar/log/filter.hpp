#pragma once

#include "solar/log/level.hpp"
#include "solar/log/record.hpp"

namespace solar::log
{

template <Level MinimumLevel = Level::Debug>
struct Filter
{
    static constexpr Level minimum_level = MinimumLevel;

    static constexpr bool accepts(Record const &record)
    {
        return at_least(record.level, MinimumLevel);
    }
};

} // namespace solar::log
