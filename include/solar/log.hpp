#pragma once

#include "solar/log/adapters.hpp"
#include "solar/log/declaration.hpp"
#include "solar/log/format.hpp"
#include "solar/log/static_logger.hpp"
#include "solar/log/store.hpp"
#include "solar/log/types.hpp"

#if defined(__ZEPHYR__)
#include "solar/log/sinks/zephyr_console.hpp"
#endif
