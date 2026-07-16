#pragma once

#include <functional>
#include <span>

#include "solar/bus/protocol.hpp"

namespace solar::bus
{

namespace detail
{

template <typename Subscriber, typename Message, typename RouteTag, typename Routes>
struct FindRoute;

template <typename Subscriber, typename Message, typename RouteTag>
struct FindRoute<Subscriber, Message, RouteTag, TypeList<>>
{
    static constexpr bool found = false;
    using type = void;
};

template <typename Subscriber, typename Message, typename RouteTag, typename Head, typename... Tail>
struct FindRoute<Subscriber, Message, RouteTag, TypeList<Head, Tail...>>
{
    using Traits = subscription_traits<Head>;
    static constexpr bool matches = std::is_same_v<Subscriber, typename Traits::SubscriberType> &&
                                    std::is_same_v<Message, typename Traits::MessageType> &&
                                    std::is_same_v<RouteTag, typename Traits::RouteTag>;
    using Remaining = FindRoute<Subscriber, Message, RouteTag, TypeList<Tail...>>;
    static constexpr bool found = matches || Remaining::found;
    using type = std::conditional_t<matches, Head, typename Remaining::type>;
};

template <typename Application, typename Message, EmitMode Mode>
[[nodiscard]] Result<void, Error> emit_frontend(const Message& message) noexcept
{
    if constexpr (Mode != EmitMode::Isr) {
        return frontend::Operation<EmitFrontend<Mode>, Message, Application>::call(message);
    } else {
        using System = bound_system_t<Application>;
        using Catalog = typename System::BusMessageCatalog;
        if constexpr (!enabled) {
            return fail(frontend_error(frontend::Error::Disabled, Operation::TryEmitIsr));
        } else if constexpr (Catalog::template contains<Message>) {
            return System::BusFacility::template emit<System, Message, EmitMode::Isr>(message);
        } else {
            static_assert(!frontend::strict,
                          "SOLAR_DIAGNOSTIC_STRICT_UNREGISTERED_BUS_MESSAGE: ISR-emitted message "
                          "is absent from the bound Bus catalog");
            return fail(frontend_error(frontend::Error::NotRegistered, Operation::TryEmitIsr));
        }
    }
}

} // namespace detail

template <Message MessageT> [[nodiscard]] Result<void, Error> emit(const MessageT& message) noexcept
{
    return detail::emit_frontend<DefaultApplication, MessageT, detail::EmitMode::Normal>(message);
}

template <Message MessageT>
    requires std::default_initializable<MessageT>
[[nodiscard]] Result<void, Error> emit() noexcept
{
    return emit(MessageT{});
}

template <Message MessageT>
[[nodiscard]] Result<void, Error> try_emit(const MessageT& message) noexcept
{
    return detail::emit_frontend<DefaultApplication, MessageT, detail::EmitMode::Try>(message);
}

template <Message MessageT>
    requires std::default_initializable<MessageT>
[[nodiscard]] Result<void, Error> try_emit() noexcept
{
    return try_emit(MessageT{});
}

template <Message MessageT>
[[nodiscard]] Result<void, Error> try_emit_isr(const MessageT& message) noexcept
{
    return detail::emit_frontend<DefaultApplication, MessageT, detail::EmitMode::Isr>(message);
}

template <Message MessageT>
    requires std::default_initializable<MessageT>
[[nodiscard]] Result<void, Error> try_emit_isr() noexcept
{
    return try_emit_isr(MessageT{});
}

template <typename Application = DefaultApplication>
[[nodiscard]] constexpr auto messages() noexcept
{
    using System = bound_system_t<Application>;
    return System::BusMessageCatalog::descriptors();
}

template <Message MessageT, typename Application = DefaultApplication>
[[nodiscard]] auto message() noexcept
{
    using System = bound_system_t<Application>;
    using Catalog = typename System::BusMessageCatalog;
    if constexpr (Catalog::template contains<MessageT>) {
        return Catalog::find(Catalog::template Entry<MessageT>::local_id);
    } else {
        static_assert(!frontend::strict,
                      "SOLAR_DIAGNOSTIC_STRICT_UNREGISTERED_BUS_MESSAGE_QUERY: queried message is "
                      "absent from the bound Bus catalog");
        using Return =
            Result<std::reference_wrapper<const MessageDescriptorView>, catalog::LookupError>;
        return Return{fail(catalog::LookupError::Unavailable)};
    }
}

template <typename Application = DefaultApplication>
[[nodiscard]] constexpr std::span<const SubscriptionView> subscriptions() noexcept
{
    if constexpr (!enabled) {
        return {};
    } else {
        using System = bound_system_t<Application>;
        using Storage = typename detail::SubscriptionViews<System>::Storage;
        return Storage::values;
    }
}

template <typename Subscriber, Message MessageT, typename RouteTag = DefaultRouteTag,
          typename Application = DefaultApplication>
[[nodiscard]] Result<RouteRecord, Error> record() noexcept
{
    if constexpr (!enabled) {
        return fail(detail::frontend_error(frontend::Error::Disabled, Operation::Query));
    } else {
        using System = bound_system_t<Application>;
        using Lookup = detail::FindRoute<Subscriber, MessageT, RouteTag,
                                         typename System::BusFacility::RouteTypes>;
        if constexpr (Lookup::found) {
            return detail::route_record<System, typename Lookup::type>();
        } else {
            static_assert(!frontend::strict,
                          "SOLAR_DIAGNOSTIC_STRICT_UNREGISTERED_BUS_ROUTE_QUERY: queried "
                          "subscriber, message, and route tag are absent from the bound Bus "
                          "topology");
            Error error{.status = Status::NotFound,
                        .reason = Reason::NotRegistered,
                        .operation = Operation::Query};
            if constexpr (System::BusMessageCatalog::template contains<MessageT>) {
                error.message = System::BusMessageCatalog::template Entry<MessageT>::local_id;
            }
            return fail(error);
        }
    }
}

template <typename Application> struct Of
{
    template <Message MessageT>
    [[nodiscard]] static Result<void, Error> emit(const MessageT& message) noexcept
    {
        return detail::emit_frontend<Application, MessageT, detail::EmitMode::Normal>(message);
    }

    template <Message MessageT>
    [[nodiscard]] static Result<void, Error> try_emit(const MessageT& message) noexcept
    {
        return detail::emit_frontend<Application, MessageT, detail::EmitMode::Try>(message);
    }

    template <Message MessageT>
    [[nodiscard]] static Result<void, Error> try_emit_isr(const MessageT& message) noexcept
    {
        return detail::emit_frontend<Application, MessageT, detail::EmitMode::Isr>(message);
    }

    [[nodiscard]] static constexpr auto messages() noexcept
    {
        return bus::messages<Application>();
    }

    [[nodiscard]] static constexpr auto subscriptions() noexcept
    {
        return bus::subscriptions<Application>();
    }

    template <typename Subscriber, Message MessageT, typename RouteTag = DefaultRouteTag>
    [[nodiscard]] static Result<RouteRecord, Error> record() noexcept
    {
        return bus::record<Subscriber, MessageT, RouteTag, Application>();
    }
};

} // namespace solar::bus
