#pragma once

/**
 * Declares a small, named logging trampoline for one internal Solar
 * subsystem (kernel, remote, ...), following the same host-noop / Zephyr-
 * native split as solar::log::system.
 *
 * Each module gets its own real Zephyr LOG_MODULE, which means it gets its
 * own auto-generated CONFIG_<NAME>_LOG_LEVEL Kconfig symbol -- filtered at
 * compile time (CONFIG_LOG_COMPILE_TIME_FILTER) independently of every other
 * module and of CONFIG_LOG_DEFAULT_LEVEL. That is the whole point: a
 * subsystem's debug traffic can be switched on with one integer, at zero
 * runtime or code-size cost everywhere else, without inventing a bespoke
 * Solar Kconfig bool per subsystem or per call site.
 *
 * Usage:
 *   - In a header included by the subsystem's implementation:
 *       SOLAR_LOG_DECLARE_MODULE(kernel)
 *     which declares solar::log::module::kernel::{error,warn,info,debug}.
 *   - In exactly one .cpp file:
 *       SOLAR_LOG_DEFINE_MODULE(kernel)
 *     which registers the Zephyr module and defines those four functions.
 *
 * On host builds the declared functions are inline no-ops, matching
 * solar::log::system, so subsystem code can call them unconditionally.
 * Application-bound code should still prefer the typed
 * solar::log::info<Source, Domain>() family; this exists for internal Solar
 * modules that predate (or sit outside) any Application binding.
 */

#if defined(__ZEPHYR__)

#include <zephyr/logging/log.h>

#define SOLAR_LOG_DECLARE_MODULE(name)                                                            \
    namespace solar::log::module::name                                                            \
    {                                                                                               \
    void error(const char* text) noexcept;                                                         \
    void warn(const char* text) noexcept;                                                           \
    void info(const char* text) noexcept;                                                           \
    void debug(const char* text) noexcept;                                                          \
    }

#define SOLAR_LOG_DEFINE_MODULE(name)                                                              \
    LOG_MODULE_REGISTER(solar_##name);                                                             \
    namespace solar::log::module::name                                                             \
    {                                                                                               \
    void error(const char* text) noexcept                                                           \
    {                                                                                                \
        LOG_ERR("%s", text);                                                                        \
    }                                                                                                \
    void warn(const char* text) noexcept                                                             \
    {                                                                                                \
        LOG_WRN("%s", text);                                                                        \
    }                                                                                                \
    void info(const char* text) noexcept                                                             \
    {                                                                                                \
        LOG_INF("%s", text);                                                                        \
    }                                                                                                \
    void debug(const char* text) noexcept                                                            \
    {                                                                                                \
        LOG_DBG("%s", text);                                                                        \
    }                                                                                                \
    }

#else

#define SOLAR_LOG_DECLARE_MODULE(name)                                                             \
    namespace solar::log::module::name                                                            \
    {                                                                                               \
    inline void error(const char*) noexcept {}                                                     \
    inline void warn(const char*) noexcept {}                                                       \
    inline void info(const char*) noexcept {}                                                       \
    inline void debug(const char*) noexcept {}                                                      \
    }

#define SOLAR_LOG_DEFINE_MODULE(name)

#endif
