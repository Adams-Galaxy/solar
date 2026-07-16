#pragma once

#if !defined(__ZEPHYR__) || !defined(CONFIG_SOLAR) || !defined(CONFIG_SOLAR_EXECUTION)
#error                                                                                             \
    "SOLAR_DIAGNOSTIC_EXECUTION_DISABLED: execution requires CONFIG_SOLAR=y and CONFIG_SOLAR_EXECUTION=y in a Zephyr build"
#endif

#include "solar/execution/api.hpp"
#include "solar/execution/contribution.hpp"
#include "solar/execution/policy.hpp"
#include "solar/execution/protocol.hpp"
#include "solar/execution/registration.hpp"
#include "solar/execution/runtime.hpp"
#include "solar/execution/service.hpp"
#include "solar/execution/service_runtime.hpp"
#include "solar/execution/types.hpp"
#include "solar/execution/work_queue.hpp"
