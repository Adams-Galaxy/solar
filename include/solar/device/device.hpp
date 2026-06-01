#pragma once

#include "solar/core.hpp"

namespace solar::device
{

/**
 * @brief Minimal device tag for simple graph entries.
 *
 * Real devices usually define their own type with `Name`, optional
 * `Dependencies`, lifecycle hooks, and contribution aliases. This helper covers
 * cases where only a named device marker is needed.
 */
template <typename NameT>
struct Device
{
    using Name = NameT;
};

} // namespace solar::device
