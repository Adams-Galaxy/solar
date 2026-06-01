#pragma once

#include <cstddef>

#include "low_level/rtos/thread.hpp"

namespace solar::rtos
{

using ThreadId = low_level::rtos::ThreadId;
using Thread = low_level::rtos::Thread;

template <std::size_t StackWords>
using ThreadStorage = low_level::rtos::ThreadStorage<StackWords>;

} // namespace solar::rtos
