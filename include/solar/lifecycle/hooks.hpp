#pragma once

#include <concepts>
#include <type_traits>

#include "solar/core/status.hpp"

namespace solar::lifecycle::detail
{

template <typename Return>
concept HookReturn = std::same_as<std::remove_cvref_t<Return>, Status> ||
                     std::same_as<std::remove_cvref_t<Return>, Result<void>>;

template <typename Component>
concept HasInit = requires { Component::init(); };

template <typename Component>
concept HasStart = requires { Component::start(); };

template <typename Component>
concept HasStop = requires { Component::stop(); };

template <typename Component>
concept HasDeinit = requires { Component::deinit(); };

template <typename Component>
inline constexpr bool valid_init_v = [] {
    if constexpr (HasInit<Component>) {
        return HookReturn<decltype(Component::init())>;
    }
    return true;
}();

template <typename Component>
inline constexpr bool valid_start_v = [] {
    if constexpr (HasStart<Component>) {
        return HookReturn<decltype(Component::start())>;
    }
    return true;
}();

template <typename Component>
inline constexpr bool valid_stop_v = [] {
    if constexpr (HasStop<Component>) {
        return HookReturn<decltype(Component::stop())>;
    }
    return true;
}();

template <typename Component>
inline constexpr bool valid_deinit_v = [] {
    if constexpr (HasDeinit<Component>) {
        return HookReturn<decltype(Component::deinit())>;
    }
    return true;
}();

template <typename Component> consteval bool validate_hooks()
{
    static_assert(valid_init_v<Component>,
                  "SOLAR_DIAGNOSTIC_INVALID_INIT_HOOK_RETURN: init() must return solar::Status or "
                  "solar::Result<void>");
    static_assert(valid_start_v<Component>,
                  "SOLAR_DIAGNOSTIC_INVALID_START_HOOK_RETURN: start() must return solar::Status "
                  "or solar::Result<void>");
    static_assert(valid_stop_v<Component>,
                  "SOLAR_DIAGNOSTIC_INVALID_STOP_HOOK_RETURN: stop() must return solar::Status or "
                  "solar::Result<void>");
    static_assert(valid_deinit_v<Component>,
                  "SOLAR_DIAGNOSTIC_INVALID_DEINIT_HOOK_RETURN: deinit() must return "
                  "solar::Status or solar::Result<void>");
    return true;
}

template <HookReturn Return> [[nodiscard]] Result<void> normalize(Return&& value) noexcept
{
    if constexpr (std::same_as<std::remove_cvref_t<Return>, Status>) {
        if (value == Status::Ok) {
            return {};
        }
        return fail(value);
    } else {
        return static_cast<Return&&>(value);
    }
}

template <typename Component> [[nodiscard]] Result<void> invoke_init() noexcept
{
    static_assert(validate_hooks<Component>());
    if constexpr (HasInit<Component>) {
        return normalize(Component::init());
    }
    return {};
}

template <typename Component> [[nodiscard]] Result<void> invoke_start() noexcept
{
    static_assert(validate_hooks<Component>());
    if constexpr (HasStart<Component>) {
        return normalize(Component::start());
    }
    return {};
}

template <typename Component> [[nodiscard]] Result<void> invoke_stop() noexcept
{
    static_assert(validate_hooks<Component>());
    if constexpr (HasStop<Component>) {
        return normalize(Component::stop());
    }
    return {};
}

template <typename Component> [[nodiscard]] Result<void> invoke_deinit() noexcept
{
    static_assert(validate_hooks<Component>());
    if constexpr (HasDeinit<Component>) {
        return normalize(Component::deinit());
    }
    return {};
}

} // namespace solar::lifecycle::detail
