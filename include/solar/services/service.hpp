#pragma once

#include "solar/service.hpp"

namespace solar::services
{

/**
 * @brief Service thread specification alias for service-local includes.
 */
template <typename NameT>
using Service = solar::ServiceSpec<NameT, 1024>;

template <typename NameT,
          std::size_t StackBytes,
          kernel::Priority PriorityValue = kernel::Priority::Normal>
using ServiceSpec = solar::ServiceSpec<NameT, StackBytes, PriorityValue>;

using StopToken = solar::StopToken;

} // namespace solar::services
