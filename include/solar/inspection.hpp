#pragma once

#if !defined(__ZEPHYR__) || !defined(CONFIG_SOLAR_INSPECTION)
#error "SOLAR_DIAGNOSTIC_INSPECTION_DISABLED: Inspection requires CONFIG_SOLAR_INSPECTION=y in a Zephyr build"
#endif

#include "solar/inspection/api.hpp"
#include "solar/inspection/cbor.hpp"
#include "solar/inspection/catalog.hpp"
#include "solar/inspection/collections.hpp"
#include "solar/inspection/contribution.hpp"
#include "solar/inspection/declaration.hpp"
#include "solar/inspection/facility.hpp"
#include "solar/inspection/format.hpp"
#include "solar/inspection/provider.hpp"
#include "solar/inspection/types.hpp"
