#pragma once

#include <cstddef>
#include <cstdint>

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
          kernel::Priority PriorityValue = kernel::Priority::Normal>
struct ServiceSpec
{
    using Name = NameT;

    static_assert(StackBytes > 0, "Solar service threads require a non-zero stack");

    static constexpr std::size_t stack_bytes = StackBytes;
    static constexpr kernel::Priority priority = PriorityValue;
    static constexpr kernel::Milliseconds stop_timeout = kernel::Milliseconds{100};
};

using StopToken = kernel::StopToken;

} // namespace solar
