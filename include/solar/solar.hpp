#pragma once

#include "solar/component.hpp"
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
#include "solar/events.hpp"
#include "solar/execution.hpp"
#include "solar/log.hpp"
#include "solar/metrics.hpp"
#include "solar/module.hpp"
#include "solar/parameters.hpp"
#include "solar/persistence.hpp"
#include "solar/supervisor.hpp"
#endif
#endif
