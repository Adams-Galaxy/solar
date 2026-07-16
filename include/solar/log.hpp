#pragma once

#include "solar/log/api.hpp"
#include "solar/log/contribution.hpp"
#include "solar/log/declaration.hpp"
#include "solar/log/facility.hpp"
#include "solar/log/format.hpp"
#include "solar/log/policy.hpp"
#include "solar/log/platform.hpp"
#include "solar/log/types.hpp"

#if defined(CONFIG_SOLAR_LOG) && defined(CONFIG_SOLAR_EVENTS)
#include "solar/events/log.hpp"
#endif
