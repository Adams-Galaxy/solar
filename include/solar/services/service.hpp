#pragma once

#include "solar/service.hpp"

namespace solar::services
{

/**
 * @brief Legacy include shim for the active service thread specification.
 */
template <typename NameT>
using Service = solar::ServiceSpec<NameT, 1024>;

template <typename NameT,
          std::size_t StackWords,
          rtos::Priority PriorityValue = rtos::Priority::Normal>
using ServiceSpec = solar::ServiceSpec<NameT, StackWords, PriorityValue>;

using StopToken = solar::StopToken;

} // namespace solar::services
