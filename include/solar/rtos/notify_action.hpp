#pragma once

namespace solar::rtos
{

/**
 * @brief Portable direct-to-thread notification update operation.
 */
enum class NotifyAction
{
    SetBits,
    Increment,
    SetValueWithOverwrite,
    SetValueWithoutOverwrite,
};

} // namespace solar::rtos
