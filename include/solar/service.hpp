#pragma once

#include <cstddef>
#include <cstdint>

#include <zephyr/sys/util_macro.h>

#include "solar/core.hpp"
#include "solar/kernel/kernel.hpp"

namespace solar
{

/**
 * @brief Thread policy for an active Solar service.
 *
 * Services are active runtime actors. Each service declares a
 * `using Thread = solar::ServiceSpec<...>` alias and implements
 * `run(ctx, stop_token)`. Solar owns thread creation for threaded services and
 * provides cooperative shutdown.
 *
 * @tparam NameT Stable service name.
 * @tparam StackBytes Statically allocated stack size in bytes.
 * @tparam PriorityValue Portable Solar priority mapped onto Zephyr priority.
 */
template <typename NameT,
          std::size_t StackBytes,
          kernel::Priority PriorityValue = kernel::Priority::Normal,
          std::uint32_t StopTimeoutMs = CONFIG_SOLAR_SERVICE_STOP_TIMEOUT_MS,
          bool AbortOnStopTimeout = IS_ENABLED(CONFIG_SOLAR_SERVICE_ABORT_ON_STOP_TIMEOUT)>
struct ServiceSpec
{
    using Name = NameT;

    static_assert(StackBytes > 0, "Solar service threads require a non-zero stack");

    static constexpr std::size_t stack_bytes = StackBytes;
    static constexpr kernel::Priority priority = PriorityValue;
    static constexpr kernel::Milliseconds stop_timeout = kernel::Milliseconds{StopTimeoutMs};
    static constexpr bool abort_on_stop_timeout = AbortOnStopTimeout;
};

using StopToken = kernel::StopToken;

} // namespace solar
