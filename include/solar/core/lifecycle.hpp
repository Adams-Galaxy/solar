#pragma once

namespace solar
{

/**
 * @brief Coarse component state used by graph snapshots and Remote inspection.
 *
 * The current system snapshot is intentionally lightweight. More detailed
 * lifecycle accounting can layer onto this without changing component identity.
 */
enum class LifecycleState
{
    Registered,
    Initialized,
    Running,
    Stopped,
    Failed,
    Disabled,
};

} // namespace solar
