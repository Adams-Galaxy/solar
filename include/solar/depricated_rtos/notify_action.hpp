#pragma once

namespace solar::kernel
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

} // namespace solar::kernel
