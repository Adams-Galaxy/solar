#pragma once

namespace solar::log::system
{

/**
 * Portable, catalog-free logging for Solar code that is deliberately
 * decoupled from any Application (for example remote::ByteRuntime, which is
 * usable with no Application at all). Application-bound code should prefer
 * the typed `solar::log::info<Source, Domain>()` family instead -- this
 * exists only where that binding cannot be assumed.
 *
 * On Zephyr this is native Zephyr logging under one Solar-owned module, so
 * it is captured by the Zephyr backend bridge (CONFIG_SOLAR_LOG_ZEPHYR_BRIDGE)
 * exactly like any other Zephyr subsystem's log traffic. On host builds it is
 * a no-op, since this path is also exercised by plain host tests that link
 * no Zephyr logging subsystem at all.
 *
 * On Zephyr these are declared, not defined, here: Zephyr's LOG_INF family
 * expands into file-scope module state (LOG_MODULE_REGISTER/DECLARE) that
 * must be established exactly once per translation unit. Defining them
 * inline in this header would inject that state into every including TU,
 * colliding with any module the TU registers for itself -- only
 * src/log/system.cpp owns it.
 */

#if defined(__ZEPHYR__)

void error(const char* text);
void warn(const char* text);
void info(const char* text);
void debug(const char* text);

#else

inline void error(const char*) {}
inline void warn(const char*) {}
inline void info(const char*) {}
inline void debug(const char*) {}

#endif

} // namespace solar::log::system
