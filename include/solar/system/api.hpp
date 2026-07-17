#pragma once

#include <cstddef>
#include <span>

#include "solar/system/binding.hpp"

namespace solar::lifecycle
{
struct ComponentRecord;
}

namespace solar
{

/** Boot the System bound for an application tag.
 *
 * Initializes and starts components in dependency order. On failure, the
 * lifecycle engine performs bounded rollback and retains a boot report.
 */
template <typename Application = DefaultApplication> [[nodiscard]] auto boot() noexcept;

/** Stop the bound System in reverse dependency order and retain a stop report. */
template <typename Application = DefaultApplication> [[nodiscard]] auto stop() noexcept;

namespace lifecycle
{

template <typename Application = DefaultApplication> struct Of;

template <typename Application = DefaultApplication> [[nodiscard]] auto state() noexcept;

template <typename Application = DefaultApplication> [[nodiscard]] auto components() noexcept;

template <typename Application = DefaultApplication>
[[nodiscard]] auto component_page(std::span<ComponentRecord> destination,
                                  std::size_t offset) noexcept;

template <typename Application = DefaultApplication>
[[nodiscard]] auto component_page(std::span<ComponentRecord> destination) noexcept;

template <typename Component, typename Application = DefaultApplication>
[[nodiscard]] auto record() noexcept;

template <typename Application = DefaultApplication> [[nodiscard]] auto boot_report() noexcept;

template <typename Application = DefaultApplication> [[nodiscard]] auto stop_report() noexcept;

} // namespace lifecycle

} // namespace solar
