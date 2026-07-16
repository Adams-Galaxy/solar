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

template <typename Application = DefaultApplication> [[nodiscard]] auto boot() noexcept;

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
