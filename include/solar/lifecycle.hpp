#pragma once

#if !defined(__ZEPHYR__) || !defined(CONFIG_SOLAR)
#error "SOLAR_DIAGNOSTIC_LIFECYCLE_DISABLED: lifecycle requires CONFIG_SOLAR=y in a Zephyr build"
#endif

#include "solar/lifecycle/engine.hpp"
#include "solar/lifecycle/hooks.hpp"
#include "solar/lifecycle/protocol.hpp"
#include "solar/lifecycle/types.hpp"
