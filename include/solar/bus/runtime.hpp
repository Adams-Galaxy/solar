#pragma once

#include <array>
#include <type_traits>

#include "solar/bus/facility.hpp"
#include "solar/execution/runtime.hpp"
#include "solar/lifecycle/engine.hpp"

namespace solar::bus::detail
{

struct RouteAttempt
{
    bool failed{};
    bool accepted{};
    bool dropped{};
    Error error{};
};

template <typename System, typename Message>
[[nodiscard]] constexpr Error make_error(Operation operation, Status status, Reason reason) noexcept
{
    using MessageCatalog = typename System::BusMessageCatalog;
    return {
        .status = status,
        .reason = reason,
        .operation = operation,
        .message = MessageCatalog::template Entry<Message>::local_id,
    };
}

template <typename System, typename Subscription>
using route_execution_t =
    RouteExecution<typename System::BusFacility, Subscription,
                   type_index_v<Subscription, typename System::BusFacility::RouteTypes>>;

template <typename System, typename Subscription>
[[nodiscard]] RouteAttempt
emit_route(const typename subscription_traits<Subscription>::MessageType& message,
           EmitMode mode) noexcept
{
    using Facility = typename System::BusFacility;
    using Traits = subscription_traits<Subscription>;
    using Delivery = typename Facility::template Delivery<Subscription>;
    constexpr auto subscription_id =
        System::BusSubscriptionCatalog::template Entry<Subscription>::local_id;
    auto& state = Facility::template route_state<Subscription>;
    state.considered();
    if (!state.accepting()) {
        auto error = make_error<System, typename Traits::MessageType>(
            mode == EmitMode::Isr   ? Operation::TryEmitIsr
            : mode == EmitMode::Try ? Operation::TryEmit
                                    : Operation::Emit,
            Status::NotReady, Reason::NotReady);
        error.subscription = subscription_id;
        return {.failed = true, .error = error};
    }

    if constexpr (!Delivery::asynchronous) {
        state.accepted(0);
        auto handled = state.template deliver<typename Traits::HandlerType>(message);
        if (!handled) {
            auto error = make_error<System, typename Traits::MessageType>(
                mode == EmitMode::Isr   ? Operation::TryEmitIsr
                : mode == EmitMode::Try ? Operation::TryEmit
                                        : Operation::Emit,
                handled.error(), Reason::InlineHandlerFailed);
            error.subscription = subscription_id;
            return {.failed = true, .accepted = true, .error = error};
        }
        return {.accepted = true};
    } else {
        auto accepted = state.accept(message, mode);
        if (accepted.status != Status::Ok) {
            auto error = make_error<System, typename Traits::MessageType>(
                mode == EmitMode::Isr   ? Operation::TryEmitIsr
                : mode == EmitMode::Try ? Operation::TryEmit
                                        : Operation::Emit,
                accepted.status,
                accepted.timed_out ? Reason::RouteTimedOut : Reason::RouteRejected);
            error.subscription = subscription_id;
            return {.failed = true, .error = error};
        }
        if (accepted.needs_submit) {
            auto submitted = execution::detail::submit_registration<
                System, route_execution_t<System, Subscription>>(mode == EmitMode::Isr);
            if (!submitted) {
                (void)state.clear_pending();
                state.submission_failed(submitted.error().status);
                auto error = make_error<System, typename Traits::MessageType>(
                    mode == EmitMode::Isr   ? Operation::TryEmitIsr
                    : mode == EmitMode::Try ? Operation::TryEmit
                                            : Operation::Emit,
                    submitted.error().status, Reason::ExecutorUnavailable);
                error.subscription = subscription_id;
                error.native_error = submitted.error().native_error;
                return {.failed = true, .error = error};
            }
        }
        return {.accepted = accepted.accepted, .dropped = accepted.dropped};
    }
}

template <typename System, typename Message, EmitMode Mode>
[[nodiscard]] Result<void, Error> emit_message(const Message& message) noexcept
{
    using Facility = typename System::BusFacility;
    using Subscriptions = typename Facility::RouteTypes;

    if constexpr (Mode == EmitMode::Isr) {
        static_assert(
            std::is_trivially_copyable_v<Message> && std::is_trivially_destructible_v<Message>,
            "SOLAR_DIAGNOSTIC_BUS_ISR_PAYLOAD: ISR-emitted Bus messages must be trivially "
            "copyable and destructible");
        static_assert(
            []<typename... Routes>(TypeList<Routes...>) {
                return (
                    (!std::is_same_v<Message, typename subscription_traits<Routes>::MessageType> ||
                     Facility::template Delivery<Routes>::isr_compatible) &&
                    ...);
            }(Subscriptions{}),
            "SOLAR_DIAGNOSTIC_BUS_ISR_TOPOLOGY: every route for an ISR-emitted message "
            "must use InlineIsr or ISR-safe asynchronous delivery without bounded wait");
    }

    if constexpr (Mode != EmitMode::Isr) {
        if (kernel::in_isr()) {
            return fail(make_error<System, Message>(Mode == EmitMode::Try ? Operation::TryEmit
                                                                          : Operation::Emit,
                                                    Status::Invalid, Reason::InvalidContext));
        }
    }

    if (lifecycle::Engine<System>::state() != lifecycle::SystemState::Running) {
        return fail(make_error<System, Message>(Mode == EmitMode::Isr   ? Operation::TryEmitIsr
                                                : Mode == EmitMode::Try ? Operation::TryEmit
                                                                        : Operation::Emit,
                                                Status::NotReady, Reason::NotReady));
    }

    Error aggregate = make_error<System, Message>(Mode == EmitMode::Isr   ? Operation::TryEmitIsr
                                                  : Mode == EmitMode::Try ? Operation::TryEmit
                                                                          : Operation::Emit,
                                                  Status::Ok, Reason::InternalInvariant);
    bool failed{};
    for_each_type<Subscriptions>([&]<typename Subscription> {
        using RouteMessage = typename subscription_traits<Subscription>::MessageType;
        if constexpr (std::is_same_v<Message, RouteMessage>) {
            ++aggregate.attempted_routes;
            const auto attempt = emit_route<System, Subscription>(message, Mode);
            if (attempt.accepted) {
                ++aggregate.accepted_routes;
            }
            if (attempt.dropped) {
                ++aggregate.dropped_routes;
            }
            if (attempt.failed) {
                ++aggregate.rejected_routes;
                if (!failed) {
                    const auto attempted = aggregate.attempted_routes;
                    const auto accepted = aggregate.accepted_routes;
                    const auto rejected = aggregate.rejected_routes;
                    const auto dropped = aggregate.dropped_routes;
                    aggregate = attempt.error;
                    aggregate.attempted_routes = attempted;
                    aggregate.accepted_routes = accepted;
                    aggregate.rejected_routes = rejected;
                    aggregate.dropped_routes = dropped;
                    failed = true;
                }
            }
        }
    });

    if (failed) {
        return fail(aggregate);
    }
    return {};
}

template <typename System, typename Subscription> [[nodiscard]] RouteRecord route_record() noexcept
{
    using Traits = subscription_traits<Subscription>;
    using Components = typename System::Catalogs::template Of<component::Tag>;
    auto record = System::BusFacility::template route_state<Subscription>.copy();
    record.subscriber = Components::template Entry<typename Traits::SubscriberType>::local_id;
    return record;
}

template <typename System, typename Subscription>
[[nodiscard]] consteval SubscriptionView subscription_view()
{
    using Traits = subscription_traits<Subscription>;
    using Delivery = typename System::BusFacility::template Delivery<Subscription>;
    using SubscriptionEntry = typename System::BusSubscriptionCatalog::template Entry<Subscription>;
    using MessageEntry =
        typename System::BusMessageCatalog::template Entry<typename Traits::MessageType>;
    using Components = typename System::Catalogs::template Of<component::Tag>;
    constexpr auto executor = [] {
        if constexpr (Delivery::asynchronous &&
                      !std::is_same_v<typename Delivery::Target, execution::SystemWorkQueue>) {
            return Components::template Entry<typename Delivery::Target>::local_id;
        } else {
            return component::LocalId{};
        }
    }();
    constexpr auto overflow = [] {
        if constexpr (!Delivery::asynchronous) {
            return OverflowKind::None;
        } else if constexpr (std::is_same_v<typename Delivery::Overflow, overflow::DropNewest>) {
            return OverflowKind::DropNewest;
        } else if constexpr (std::is_same_v<typename Delivery::Overflow, overflow::DropOldest>) {
            return OverflowKind::DropOldest;
        } else if constexpr (requires { Delivery::Overflow::timeout; }) {
            return OverflowKind::Wait;
        } else {
            return OverflowKind::Reject;
        }
    }();
    return {
        .local_id = SubscriptionEntry::local_id,
        .message = MessageEntry::local_id,
        .subscriber = Components::template Entry<typename Traits::SubscriberType>::local_id,
        .origin_owner = SubscriptionEntry::owner_view(),
        .origin = SubscriptionEntry::origin_kind,
        .message_name =
            descriptor_traits<MessageTag, typename Traits::MessageType>::descriptor.name,
        .delivery = Delivery::kind,
        .overflow = overflow,
        .stop = std::is_same_v<typename Delivery::Stop, stop::CancelPending>
                    ? StopKind::CancelPending
                    : StopKind::Drain,
        .executor = executor,
        .capacity = Delivery::capacity,
        .payload_bytes = Delivery::asynchronous ? sizeof(typename Traits::MessageType) : 0,
        .isr_compatible = Delivery::isr_compatible,
    };
}

template <typename System> struct SubscriptionViews
{
    template <typename List> struct Build;

    template <typename... Entries> struct Build<TypeList<Entries...>>
    {
        inline static constexpr std::array<SubscriptionView, sizeof...(Entries)> values{
            subscription_view<System, typename Entries::Declaration>()...};
    };

    using Storage = Build<typename System::BusSubscriptionCatalog::EntryTypes>;
};

} // namespace solar::bus::detail

namespace solar::bus
{

template <typename Architecture>
template <typename System, typename Message, detail::EmitMode Mode>
Result<void, Error> Facility<Architecture>::emit(const Message& message) noexcept
{
    return detail::emit_message<System, Message, Mode>(message);
}

} // namespace solar::bus
