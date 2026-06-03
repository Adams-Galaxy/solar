#pragma once

#include <zephyr/kernel.h>

namespace solar::kernel
{

using NativeThread = k_tid_t;
using NativeTimeout = k_timeout_t;

} // namespace solar::kernel
