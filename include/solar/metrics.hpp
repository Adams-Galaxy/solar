#pragma once

#include "solar/metrics/api.hpp"
#include "solar/metrics/catalog.hpp"
#include "solar/metrics/contribution.hpp"
#include "solar/metrics/declaration.hpp"
#include "solar/metrics/facility.hpp"
#include "solar/metrics/policy.hpp"
#include "solar/metrics/reducer.hpp"
#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_METRICS)
#include "solar/metrics/storage.hpp"
#endif
#include "solar/metrics/types.hpp"
#include "solar/metrics/units.hpp"

#if defined(CONFIG_SOLAR_METRICS) && defined(CONFIG_SOLAR_EVENTS)
#include "solar/events/metrics.hpp"
#endif
