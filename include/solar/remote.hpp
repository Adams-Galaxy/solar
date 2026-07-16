#pragma once

#include "solar/remote/catalog.hpp"
#include "solar/remote/codec.hpp"
#include "solar/remote/contribution.hpp"
#include "solar/remote/declaration.hpp"
#include "solar/remote/frame.hpp"
#include "solar/remote/facility.hpp"
#include "solar/remote/link.hpp"
#include "solar/remote/manifest.hpp"
#include "solar/remote/packed.hpp"
#include "solar/remote/protocol.hpp"
#include "solar/remote/runtime.hpp"
#include "solar/remote/service.hpp"
#include "solar/remote/types.hpp"

#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_REMOTE)
#include "solar/remote/api.hpp"
#endif
