#pragma once

#include <atomic>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "solar/system/binding.hpp"

namespace solar
{

namespace frontend
{

enum class Error
{
    NotReady,
    Disabled,
    NotRegistered,
};

[[nodiscard]] constexpr Status status_of(Error error) noexcept
{
    switch (error) {
    case Error::NotReady:
        return Status::NotReady;
    case Error::Disabled:
        return Status::NotSupported;
    case Error::NotRegistered:
        return Status::NotFound;
    }
    return Status::Error;
}

#if defined(CONFIG_SOLAR_STRICT_CATALOG_BINDING)
inline constexpr bool strict = true;
#else
inline constexpr bool strict = false;
#endif

namespace detail
{

template <typename Policy, typename Declaration, typename Return>
[[nodiscard]] Return unavailable(Error error)
{
    if constexpr (requires {
                      { Policy::template unavailable<Declaration>(error) } -> std::same_as<Return>;
                  }) {
        return Policy::template unavailable<Declaration>(error);
    } else if constexpr (requires {
                             { Policy::unavailable(error) } -> std::same_as<Return>;
                         }) {
        return Policy::unavailable(error);
    } else {
        return solar::fail<Error>(error);
    }
}

enum class Availability : std::uint8_t
{
    NotReady,
    Ready,
    Disabled,
};

template <typename Application, typename Policy> struct BindingState
{
    inline static std::atomic<Availability> availability{Availability::NotReady};
};

template <typename Policy, typename Declaration, typename Application, typename Signature>
struct OperationSlot;

template <typename Policy, typename Declaration, typename Application, typename Return,
          typename... Arguments>
struct OperationSlot<Policy, Declaration, Application, Return(Arguments...)>
{
    using Function = Return (*)(Arguments...);

    inline static std::atomic<Function> function{nullptr};

    template <typename System> static void bind()
    {
        function.store(
            +[](Arguments... arguments) -> Return {
                return Policy::template invoke<System, Declaration>(
                    std::forward<Arguments>(arguments)...);
            },
            std::memory_order_release);
    }

    static void clear()
    {
        function.store(nullptr, std::memory_order_relaxed);
    }

    static Return call(Arguments... arguments)
    {
        const auto availability =
            BindingState<Application, Policy>::availability.load(std::memory_order_acquire);
        if (availability == Availability::NotReady) {
            return detail::unavailable<Policy, Declaration, Return>(Error::NotReady);
        }
        if (availability == Availability::Disabled) {
            return detail::unavailable<Policy, Declaration, Return>(Error::Disabled);
        }

        const auto target = function.load(std::memory_order_acquire);
        if (target != nullptr) {
            return target(std::forward<Arguments>(arguments)...);
        }
        return detail::unavailable<Policy, Declaration, Return>(Error::NotRegistered);
    }
};

template <typename System, typename Policy, typename Entries> struct BindEntries;

template <typename System, typename Policy, typename... Entries>
struct BindEntries<System, Policy, TypeList<Entries...>>
{
    template <typename Application> static void bind()
    {
        (OperationSlot<Policy, typename Entries::Declaration, Application,
                       typename Policy::template Signature<typename Entries::Declaration>>::
             template bind<System>(),
         ...);
    }

    template <typename Application> static void clear()
    {
        (OperationSlot<Policy, typename Entries::Declaration, Application,
                       typename Policy::template Signature<typename Entries::Declaration>>::clear(),
         ...);
    }
};

} // namespace detail

template <typename Policy, typename Declaration, typename Application = DefaultApplication>
struct Operation
{
    using Signature = typename Policy::template Signature<Declaration>;

    template <typename... Arguments> [[nodiscard]] static auto call(Arguments&&... arguments)
    {
        if constexpr (strict) {
            using System = bound_system_t<Application>;
            using Catalog = typename System::Catalogs::template Of<typename Policy::CatalogTag>;
            static_assert(Catalog::template contains<Declaration>,
                          "SOLAR_DIAGNOSTIC_STRICT_UNREGISTERED_OPERATION: declaration is absent "
                          "from the bound catalog");
            if constexpr (requires { Policy::template validate<System, Declaration>(); }) {
                Policy::template validate<System, Declaration>();
            }
            return Policy::template invoke<System, Declaration>(
                std::forward<Arguments>(arguments)...);
        } else {
            return detail::OperationSlot<Policy, Declaration, Application, Signature>::call(
                std::forward<Arguments>(arguments)...);
        }
    }
};

template <typename System, typename Policy, typename Application = DefaultApplication>
void bind_catalog()
{
    using Catalog = typename System::Catalogs::template Of<typename Policy::CatalogTag>;
    detail::BindEntries<System, Policy, typename Catalog::EntryTypes>::template bind<Application>();
    detail::BindingState<Application, Policy>::availability.store(detail::Availability::Ready,
                                                                  std::memory_order_release);
}

template <typename Policy, typename Application = DefaultApplication> void bind_disabled()
{
    detail::BindingState<Application, Policy>::availability.store(detail::Availability::Disabled,
                                                                  std::memory_order_release);
}

template <typename Policy, typename Application = DefaultApplication> void reset_for_test()
{
    detail::BindingState<Application, Policy>::availability.store(detail::Availability::NotReady,
                                                                  std::memory_order_relaxed);
}

template <typename System, typename Policy, typename Application = DefaultApplication>
void reset_catalog_for_test()
{
    using Catalog = typename System::Catalogs::template Of<typename Policy::CatalogTag>;
    detail::BindEntries<System, Policy,
                        typename Catalog::EntryTypes>::template clear<Application>();
    reset_for_test<Policy, Application>();
}

template <typename Application> struct Of
{
    using ApplicationTag = Application;

    template <typename Policy, typename Declaration>
    using Operation = frontend::Operation<Policy, Declaration, Application>;
};

} // namespace frontend

} // namespace solar
