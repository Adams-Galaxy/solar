#pragma once

#include "solar/lifecycle/types.hpp"

namespace solar::lifecycle
{

template <typename System, typename Component> struct ExecutionProtocol
{
    static constexpr bool participates = false;

    [[nodiscard]] static Result<void> prepare() noexcept
    {
        return {};
    }

    [[nodiscard]] static Result<void> validate_activation() noexcept
    {
        return {};
    }

    static void activate() noexcept {}

    [[nodiscard]] static Result<void> request_stop() noexcept
    {
        return {};
    }

    [[nodiscard]] static Containment contain() noexcept
    {
        return {};
    }

    template <typename Visitor> static void visit_uncontained_dependencies(Visitor&&) noexcept {}
};

template <typename System> struct SystemExecutionProtocol
{
    static constexpr bool participates = false;

    [[nodiscard]] static Result<void> prepare() noexcept
    {
        return {};
    }

    [[nodiscard]] static Result<void> validate_activation() noexcept
    {
        return {};
    }

    static void activate() noexcept {}

    [[nodiscard]] static Result<void> request_stop() noexcept
    {
        return {};
    }

    [[nodiscard]] static Containment contain() noexcept
    {
        return {};
    }

    [[nodiscard]] static Failure failure(Operation operation, Status status) noexcept
    {
        return {
            .component = component::LocalId{},
            .category = ComponentCategory::Facility,
            .operation = operation,
            .status = status,
            .primary = true,
        };
    }

    [[nodiscard]] static std::size_t uncontained_count() noexcept
    {
        return 0;
    }

    template <typename Visitor> static void visit_uncontained_dependencies(Visitor&&) noexcept {}
};

template <typename CatalogTag> struct ApplicationBindingProtocol
{
    template <typename System, typename Application> static void bind() noexcept {}
};

template <typename CatalogTag> struct CatalogActivationProtocol
{
    template <typename System> static constexpr bool participates = false;

    template <typename System> [[nodiscard]] static Result<void> commit() noexcept
    {
        return {};
    }

    template <typename System>
    [[nodiscard]] static Failure failure(Operation operation, Status status) noexcept
    {
        return {
            .component = component::LocalId{},
            .category = ComponentCategory::Facility,
            .operation = operation,
            .status = status,
            .primary = true,
        };
    }

    template <typename System> static void activate() noexcept {}
};

template <typename System, typename Application> struct ApplicationProtocol
{
    static void bind() noexcept
    {
        for_each_type<typename System::Catalogs::CatalogTypes>([]<typename Catalog> {
            ApplicationBindingProtocol<typename Catalog::Tag>::template bind<System, Application>();
        });
    }
};

} // namespace solar::lifecycle
