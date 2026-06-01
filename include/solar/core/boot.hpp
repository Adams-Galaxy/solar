#pragma once

#include "solar/core/status.hpp"

namespace solar
{

/**
 * @brief Ordered boot phase labels used in `BootReport`.
 *
 * Solar records the first phase that fails during `System::Boot()`. The enum is
 * intentionally broad rather than component-specific so Remote and logs can
 * report boot state without knowing the graph's concrete types.
 */
enum class BootPhase
{
    BoardInit,
    PeripheralInit,
    PeripheralStart,
    DeviceInit,
    ServiceInit,
    DeviceStart,
    ServiceStart,
    TaskInit,
    TaskStart,
    App,
};

/**
 * @brief First failed boot component, phase, and status.
 */
struct BootFailure
{
    BootPhase phase = BootPhase::BoardInit;
    Status status = Status::Ok;
    const char *component = nullptr;
};

/**
 * @brief Result of the last Solar-owned boot attempt.
 */
struct BootReport
{
    Status status = Status::Ok;
    BootFailure failure{};

    constexpr bool ok() const
    {
        return status == Status::Ok;
    }
};

} // namespace solar
