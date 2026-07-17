#pragma once

#if defined(CONFIG_SOLAR_BUS)
#include "solar/bus/runtime.hpp"
#else
#include "solar/bus/facility.hpp"
#endif
#include "solar/lifecycle/protocol.hpp"
#include "solar/system/frontend.hpp"

namespace solar::bus::detail
{

[[nodiscard]] constexpr Error frontend_error(frontend::Error error, Operation operation) noexcept
{
    switch (error) {
    case frontend::Error::NotReady:
        return {
            .status = solar::Status::NotReady, .reason = Reason::NotReady, .operation = operation};
    case frontend::Error::Disabled:
        return {.status = solar::Status::NotSupported,
                .reason = Reason::Disabled,
                .operation = operation};
    case frontend::Error::NotRegistered:
        return {.status = solar::Status::NotFound,
                .reason = Reason::NotRegistered,
                .operation = operation};
    }
    return {.status = solar::Status::Error,
            .reason = Reason::InternalInvariant,
            .operation = operation};
}

template <EmitMode Mode> struct EmitFrontend
{
    using CatalogTag = MessageTag;
    template <typename Message> using Signature = Result<void, Error>(const Message&);

    template <typename System, typename Message>
    [[nodiscard]] static Result<void, Error> invoke(const Message& message) noexcept
    {
        return System::BusFacility::template emit<System, Message, Mode>(message);
    }

    [[nodiscard]] static Result<void, Error> unavailable(frontend::Error error) noexcept
    {
        constexpr Operation operation = Mode == EmitMode::Isr   ? Operation::TryEmitIsr
                                        : Mode == EmitMode::Try ? Operation::TryEmit
                                                                : Operation::Emit;
        return fail<Error>(frontend_error(error, operation));
    }
};

template <typename System, typename Application> void bind_bus_frontends() noexcept
{
    if constexpr (enabled) {
        frontend::bind_catalog<System, EmitFrontend<EmitMode::Normal>, Application>();
        frontend::bind_catalog<System, EmitFrontend<EmitMode::Try>, Application>();
    } else {
        frontend::bind_disabled<EmitFrontend<EmitMode::Normal>, Application>();
        frontend::bind_disabled<EmitFrontend<EmitMode::Try>, Application>();
    }
}

} // namespace solar::bus::detail

template <> struct solar::lifecycle::ApplicationBindingProtocol<solar::bus::MessageTag>
{
    template <typename System, typename Application> static void bind() noexcept
    {
        solar::bus::detail::bind_bus_frontends<System, Application>();
    }
};
