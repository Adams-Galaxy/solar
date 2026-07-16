#pragma once

#include "solar/catalog.hpp"
#include "solar/core.hpp"
#include "solar/remote.hpp"
#include "solar/system.hpp"
#include "solar/version.hpp"

#if defined(__ZEPHYR__)
#include "solar/kernel.hpp"
#if defined(CONFIG_SOLAR)
#if defined(CONFIG_SOLAR_HARDWARE)
#include "solar/hardware.hpp"
#endif
#if defined(CONFIG_SOLAR_HEALTH)
#include "solar/health.hpp"
#endif
#if defined(CONFIG_SOLAR_SUPERVISOR)
#include "solar/supervisor.hpp"
#endif
#include "solar/lifecycle.hpp"
#if defined(CONFIG_SOLAR_EXECUTION)
#include "solar/execution.hpp"
#endif
#if defined(CONFIG_SOLAR_BUS)
#include "solar/bus.hpp"
#endif
#if defined(CONFIG_SOLAR_PARAMETERS)
#include "solar/parameters.hpp"
#endif
#if defined(CONFIG_SOLAR_EVENTS)
#include "solar/events.hpp"
#endif
#if defined(CONFIG_SOLAR_METRICS)
#include "solar/metrics.hpp"
#endif
#if defined(CONFIG_SOLAR_LOG)
#include "solar/log.hpp"
#endif
#if defined(CONFIG_SOLAR_INSPECTION)
#include "solar/inspection.hpp"
#endif
#endif
#endif
