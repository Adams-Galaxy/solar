#pragma once

#include "low_level/rtos/queue.hpp"

namespace solar::rtos
{

template <typename T, std::size_t Depth>
using Queue = low_level::rtos::Queue<T, Depth>;

} // namespace solar::rtos

