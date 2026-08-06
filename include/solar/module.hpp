#pragma once

#include <concepts>
#include <string_view>
#include <type_traits>
#include <utility>

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

template <typename T> [[nodiscard]] Result<void> initialize(T& value)
{
    if constexpr (requires {
                      { value.initialize() } -> std::same_as<Result<void>>;
                  }) {
        return value.initialize();
    }
    return {};
}

template <typename T> [[nodiscard]] Result<void> start(T& value)
{
    if constexpr (requires {
                      { value.start() } -> std::same_as<Result<void>>;
                  }) {
        return value.start();
    }
    return {};
}

template <typename T> [[nodiscard]] Result<void> stop(T& value)
{
    if constexpr (requires {
                      { value.stop() } -> std::same_as<Result<void>>;
                  }) {
        return value.stop();
    }
    return {};
}

template <typename T> [[nodiscard]] Result<void> deinitialize(T& value)
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

    [[nodiscard]] static Owner& instance()
    {
        return storage;
    }
    [[nodiscard]] static const Owner& instance_const()
    {
        return storage;
    }
    [[nodiscard]] static Result<void> initialize()
    {
        return module::initialize(storage);
    }
    [[nodiscard]] static Result<void> start()
    {
        return module::start(storage);
    }
    [[nodiscard]] static Result<void> stop()
    {
        return module::stop(storage);
    }
    [[nodiscard]] static Result<void> deinitialize()
    {
        return module::deinitialize(storage);
    }
};

} // namespace solar::module

namespace solar
{

/** A static type already expressed in Solar's lifecycle vocabulary. */
template <typename T>
concept Module = requires {
    { T::initialize() } -> std::same_as<Result<void>>;
    { T::start() } -> std::same_as<Result<void>>;
    { T::stop() } -> std::same_as<Result<void>>;
    { T::deinitialize() } -> std::same_as<Result<void>>;
};

namespace module::detail
{
template <typename T, typename = void> struct DependenciesOf
{
    using type = TypeList<>;
};
template <typename T> struct DependenciesOf<T, std::void_t<typename T::Dependencies>>
{
    using type = typename T::Dependencies;
};

template <typename Function> [[nodiscard]] Result<void> adapt(Function&& function) noexcept
{
    if constexpr (std::is_void_v<std::invoke_result_t<Function>>) {
        std::forward<Function>(function)();
        return {};
    } else {
        auto result = std::forward<Function>(function)();
        if constexpr (std::same_as<decltype(result), Result<void>>) {
            return result;
        } else {
            return result ? Result<void>{} : fail<Error>({.status = status_of(result.error())});
        }
    }
}
} // namespace module::detail

/**
 * Adapt a conventional static device (`init/start/stop/deinit`) to a Solar
 * lifecycle module. Error types are reduced through `status_of`.
 */
template <typename Device> struct AsModule
{
    using DeviceType = Device;
    using Dependencies = typename module::detail::DependenciesOf<Device>::type;
    static constexpr std::string_view name = [] {
        if constexpr (requires { Device::name; })
            return std::string_view{Device::name};
        if constexpr (requires { Device::descriptor.name; })
            return std::string_view{Device::descriptor.name};
        return std::string_view{};
    }();

    [[nodiscard]] static Result<void> initialize()
    {
        if constexpr (requires { Device::init(); })
            return module::detail::adapt([] { return Device::init(); });
        return {};
    }
    [[nodiscard]] static Result<void> start()
    {
        if constexpr (requires { Device::start(); })
            return module::detail::adapt([] { return Device::start(); });
        return {};
    }
    [[nodiscard]] static Result<void> stop()
    {
        if constexpr (requires { Device::stop(); })
            return module::detail::adapt([] { return Device::stop(); });
        return {};
    }
    [[nodiscard]] static Result<void> deinitialize()
    {
        if constexpr (requires { Device::deinit(); })
            return module::detail::adapt([] { return Device::deinit(); });
        return {};
    }
};

} // namespace solar
