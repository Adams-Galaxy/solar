#pragma once

#include <zephyr/kernel.h>

#include "solar/kernel/critical_section.hpp"

namespace solar::kernel
{

inline bool in_interrupt()
{
    return k_is_in_isr();
}

} // namespace solar::kernel
