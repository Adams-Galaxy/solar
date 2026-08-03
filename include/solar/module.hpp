#pragma once

#include <concepts>
#include <string_view>
#include <type_traits>

#include "solar/core/status.hpp"
#include "solar/core/type_list.hpp"

namespace solar::module
{

/** Stable compile-time identity for a framework or application module. */
template <typename T>
concept Identified = requires {
    { T::name } -> std::convertible_to<std::string_view>;
};

/** Optional module capabilities are ordinary types collected in a TypeList. */
template <typename T>
concept CapabilityProvider =
    requires { typename T::Capabilities; } && TypeListType<typename T::Capabilities>;

template <typename T>
concept CatalogProvider = requires { typename T::Catalog; } && TypeListType<typename T::Catalog>;

template <typename T>
concept InspectionProvider =
    requires { typename T::Inspection; } && TypeListType<typename T::Inspection>;

template <typename T> [[nodiscard]] Result<void> initialize(T& value) noexcept
{
    if constexpr (requires {
                      { value.initialize() } -> std::same_as<Result<void>>;
                  }) {
        return value.initialize();
    }
    return {};
}

template <typename T> [[nodiscard]] Result<void> start(T& value) noexcept
{
    if constexpr (requires {
                      { value.start() } -> std::same_as<Result<void>>;
                  }) {
        return value.start();
    }
    return {};
}

template <typename T> [[nodiscard]] Result<void> stop(T& value) noexcept
{
    if constexpr (requires {
                      { value.stop() } -> std::same_as<Result<void>>;
                  }) {
        return value.stop();
    }
    return {};
}

template <typename T> [[nodiscard]] Result<void> deinitialize(T& value) noexcept
{
    if constexpr (requires {
                      { value.deinitialize() } -> std::same_as<Result<void>>;
                  }) {
        return value.deinitialize();
    }
    return {};
}

/**
 * Own exactly one ordinary object for an application and module identity.
 *
 * Specialized facades should forward their typed API to `instance()` rather
 * than duplicate storage or behavior.
 */
template <typename Application, typename Identity, typename Owner> struct StaticOwner
{
    using ApplicationType = Application;
    using IdentityType = Identity;
    using OwnerType = Owner;

    inline static Owner storage{};

    [[nodiscard]] static Owner& instance() noexcept
    {
        return storage;
    }
    [[nodiscard]] static const Owner& instance_const() noexcept
    {
        return storage;
    }
    [[nodiscard]] static Result<void> initialize() noexcept
    {
        return module::initialize(storage);
    }
    [[nodiscard]] static Result<void> start() noexcept
    {
        return module::start(storage);
    }
    [[nodiscard]] static Result<void> stop() noexcept
    {
        return module::stop(storage);
    }
    [[nodiscard]] static Result<void> deinitialize() noexcept
    {
        return module::deinitialize(storage);
    }
};

} // namespace solar::module
