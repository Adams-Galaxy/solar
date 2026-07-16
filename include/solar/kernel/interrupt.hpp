#pragma once

#include <zephyr/kernel.h>

namespace solar::kernel
{

[[nodiscard]] inline bool in_isr() noexcept
{
    return k_is_in_isr();
}

} // namespace solar::kernel
