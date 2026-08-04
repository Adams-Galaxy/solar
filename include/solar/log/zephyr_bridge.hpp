#pragma once

#include <string_view>

#include "solar/core/status.hpp"
#include "solar/log/declaration.hpp"
#include "solar/log/types.hpp"

#if defined(CONFIG_SOLAR_LOG_ZEPHYR_BRIDGE)

namespace solar::log::bridge
{

/**
 * One native Zephyr log line, forwarded into whichever StaticLogger has
 * installed itself. `text` is Zephyr's own rendered message, already
 * prefixed with its source module name -- the same rendering the console
 * backend produces -- and is only valid for the duration of the call.
 */
using Sink = Result<Receipt, Error> (*)(Level level, std::string_view text) noexcept;

/**
 * Installs the single StaticLogger instance that receives bridged records.
 * Solar builds one Application per binary, so one global slot mirrors
 * `kernel::install_fatal_observer`'s pattern rather than needing a registry.
 */
[[nodiscard]] Result<void> install(Sink sink) noexcept;

/** Called by the Zephyr log backend for every processed message. */
void forward(Level level, std::string_view text) noexcept;

template <typename Logger>
[[nodiscard]] Result<Receipt, Error> forward_into(Level level, std::string_view text) noexcept
{
    return Logger::template capture_text<source::ZephyrBridge>(level, Origin::Zephyr, text);
}

/** Installs `Logger` as the target for `forward()`. Call once, at startup. */
template <typename Logger> [[nodiscard]] Result<void> install_for() noexcept
{
    return install(&forward_into<Logger>);
}

} // namespace solar::log::bridge

#endif // CONFIG_SOLAR_LOG_ZEPHYR_BRIDGE
