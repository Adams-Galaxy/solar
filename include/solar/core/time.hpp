#pragma once

#include <chrono>

namespace solar
{

/**
 * @brief Common duration aliases used by Solar public APIs.
 *
 * Platform-specific tick conversion is kept under `solar::rtos`; these aliases
 * are stable application vocabulary.
 */
using Duration = std::chrono::milliseconds;
using Milliseconds = std::chrono::milliseconds;
using Microseconds = std::chrono::microseconds;
using Seconds = std::chrono::seconds;

} // namespace solar
