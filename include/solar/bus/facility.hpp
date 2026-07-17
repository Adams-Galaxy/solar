#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "solar/bus/catalog.hpp"
#include "solar/bus/policy.hpp"
#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_BUS)
#include "solar/execution/registration.hpp"
#include "solar/execution/work_queue.hpp"
#include "solar/kernel/semaphore.hpp"
#include "solar/kernel/spinlock.hpp"
#include "solar/kernel/this_thread.hpp"
#endif

namespace solar::bus
{

#if defined(CONFIG_SOLAR) && !defined(CONFIG_SOLAR_BUS)
inline constexpr bool enabled = false;
#else
inline constexpr bool enabled = true;
#endif

template <typename Architecture> struct Facility;

namespace detail
{

enum class EmitMode : std::uint8_t
{
    Normal,
    Try,
    Isr,
};

template <typename System, typename Subscription> [[nodiscard]] RouteRecord route_record() noexcept;

template <typename System> struct SubscriptionViews;

template <typename List> struct DeclarationsOf;

template <typename... Entries> struct DeclarationsOf<TypeList<Entries...>>
{
    using type = TypeList<typename Entries::Declaration...>;
};

template <typename List> using declarations_of_t = typename DeclarationsOf<List>::type;

} // namespace detail

#if !defined(__ZEPHYR__) || !defined(CONFIG_SOLAR_BUS)

template <typename MessageDeclarations, typename SubscriptionDeclarations, typename Components,
          typename Configuration>
struct Architecture
{
    using Messages = MessageDeclarations;
    using Subscriptions = SubscriptionDeclarations;
    using ComponentTypes = Components;
    using ConfigurationPolicies = Configuration;

    static constexpr bool demanded = list_size_v<Messages> != 0 || list_size_v<Subscriptions> != 0;
};

#else

namespace detail
{

struct RouteAcceptance
{
    Status status{Status::Ok};
    bool accepted{true};
    bool dropped{};
    bool needs_submit{};
    bool timed_out{};
};

template <typename Needle, typename List> struct TypeIndex;

template <typename Needle, typename... Tail>
struct TypeIndex<Needle, TypeList<Needle, Tail...>> : std::integral_constant<std::size_t, 0>
{};

template <typename Needle, typename Head, typename... Tail>
struct TypeIndex<Needle, TypeList<Head, Tail...>>
    : std::integral_constant<std::size_t, 1 + TypeIndex<Needle, TypeList<Tail...>>::value>
{};

template <typename Needle, typename List>
inline constexpr std::size_t type_index_v = TypeIndex<Needle, List>::value;

template <typename Handler, typename Message>
concept HandlerVoid = requires(const Message& message) {
    { Handler::handle(message) } -> std::same_as<void>;
};

template <typename Handler, typename Message>
concept HandlerResult = requires(const Message& message) { Handler::handle(message); } &&
                        VoidResult<decltype(Handler::handle(std::declval<const Message&>()))>;

template <typename Handler, typename Message>
concept ValidHandler = HandlerVoid<Handler, Message> || HandlerResult<Handler, Message>;

template <typename Handler, typename Message>
[[nodiscard]] Result<void> invoke_handler(const Message& message) noexcept
{
    static_assert(ValidHandler<Handler, Message>,
                  "SOLAR_DIAGNOSTIC_INVALID_BUS_HANDLER: handler must expose static void or "
                  "Result<void, ErrorType> handle(const Message&)");
    if constexpr (HandlerVoid<Handler, Message>) {
        Handler::handle(message);
        return {};
    } else if constexpr (HandlerResult<Handler, Message>) {
        auto result = Handler::handle(message);
        return result ? Result<void>{}
                      : Result<void>{fail<solar::Error>({.status = status_of(result.error())})};
    } else {
        return fail<solar::Error>({.status = solar::Status::Invalid});
    }
}

template <typename Message> using PayloadBytes = std::array<std::byte, sizeof(Message)>;

template <typename Message> [[nodiscard]] PayloadBytes<Message> store_payload(const Message& value)
{
    return std::bit_cast<PayloadBytes<Message>>(value);
}

template <typename Message> [[nodiscard]] Message load_payload(const PayloadBytes<Message>& value)
{
    return std::bit_cast<Message>(value);
}

class RouteStateCommon
{
  public:
    void initialize(SubscriptionLocalId subscription, MessageLocalId message,
                    DeliveryKind delivery) noexcept
    {
        accepting_.store(false, std::memory_order_release);
        in_flight_.store(0, std::memory_order_release);
        auto guard = record_lock_.acquire();
        record_ = {
            .subscription = subscription,
            .message = message,
            .delivery = delivery,
            .last_status = Status::NotReady,
            .quiescent = true,
        };
    }

    void set_accepting(bool accepting) noexcept
    {
        accepting_.store(accepting, std::memory_order_release);
        mutate([&](RouteRecord& record) {
            record.accepting = accepting;
            record.draining = !accepting;
            if (accepting) {
                record.last_status = Status::Ok;
            }
        });
    }

    [[nodiscard]] bool accepting() const noexcept
    {
        return accepting_.load(std::memory_order_acquire);
    }

    void considered() noexcept
    {
        mutate([](RouteRecord& record) { ++record.considered; });
    }

    void accepted(std::uint32_t pending) noexcept
    {
        mutate([&](RouteRecord& record) {
            ++record.accepted;
            record.pending = pending;
            record.last_accept_tick = kernel::now_ticks();
            record.last_status = Status::Ok;
            record.quiescent = false;
            if (pending > record.pending_high_water) {
                record.pending_high_water = pending;
            }
        });
    }

    void rejected(Status status, bool timed_out) noexcept
    {
        mutate([&](RouteRecord& record) {
            ++record.rejected;
            if (timed_out) {
                ++record.timed_out;
            }
            record.last_status = status;
        });
    }

    void dropped_newest() noexcept
    {
        mutate([](RouteRecord& record) { ++record.dropped_newest; });
    }

    void dropped_oldest() noexcept
    {
        mutate([](RouteRecord& record) { ++record.dropped_oldest; });
    }

    void replaced() noexcept
    {
        mutate([](RouteRecord& record) { ++record.replacements; });
    }

    void coalesced() noexcept
    {
        mutate([](RouteRecord& record) { ++record.coalesced; });
    }

    void cancelled(std::uint32_t count) noexcept
    {
        mutate([&](RouteRecord& record) {
            record.cancelled += count;
            record.pending = 0;
            record.quiescent = in_flight_.load(std::memory_order_acquire) == 0;
        });
    }

    void submission_failed(Status status) noexcept
    {
        mutate([&](RouteRecord& record) {
            ++record.executor_unavailable;
            record.last_status = status;
        });
    }

    void finish_stop() noexcept
    {
        mutate([](RouteRecord& record) {
            record.draining = false;
            record.quiescent = record.pending == 0 && record.in_flight == 0;
        });
    }

    template <typename Handler, typename Message>
    [[nodiscard]] Result<void> deliver(const Message& message) noexcept
    {
        in_flight_.fetch_add(1, std::memory_order_acq_rel);
        mutate([](RouteRecord& record) {
            ++record.in_flight;
            record.quiescent = false;
        });
        auto result = invoke_handler<Handler>(message);
        in_flight_.fetch_sub(1, std::memory_order_acq_rel);
        mutate([&](RouteRecord& record) {
            ++record.delivered;
            if (!result) {
                ++record.handler_failed;
                if (!record.has_handler_failure) {
                    record.first_handler_failure = status_of(result.error());
                    record.has_handler_failure = true;
                }
                record.last_handler_failure = status_of(result.error());
            }
            record.last_status = result ? Status::Ok : status_of(result.error());
            record.last_delivery_tick = kernel::now_ticks();
            --record.in_flight;
            record.quiescent = record.pending == 0 && record.in_flight == 0;
        });
        return result;
    }

    [[nodiscard]] std::uint32_t in_flight() const noexcept
    {
        return in_flight_.load(std::memory_order_acquire);
    }

    [[nodiscard]] RouteRecord copy() noexcept
    {
        auto guard = record_lock_.acquire();
        record_.accepting = accepting_.load(std::memory_order_acquire);
        record_.in_flight = in_flight_.load(std::memory_order_acquire);
        record_.quiescent = record_.pending == 0 && record_.in_flight == 0;
        return record_;
    }

  protected:
    template <typename Mutator> void mutate(Mutator&& mutator) noexcept
    {
        auto guard = record_lock_.acquire();
        mutator(record_);
    }

  private:
    kernel::SpinLock record_lock_{};
    RouteRecord record_{};
    std::atomic_bool accepting_{};
    std::atomic_uint32_t in_flight_{};
};

template <typename FacilityT, typename Subscription,
          DeliveryKind Kind = FacilityT::template Delivery<Subscription>::kind>
class RouteState;

template <typename FacilityT, typename Subscription>
class RouteState<FacilityT, Subscription, DeliveryKind::Inline> : public RouteStateCommon
{};

template <typename FacilityT, typename Subscription>
class RouteState<FacilityT, Subscription, DeliveryKind::InlineIsr> : public RouteStateCommon
{};

template <typename FacilityT, typename Subscription>
class RouteState<FacilityT, Subscription, DeliveryKind::Queued> : public RouteStateCommon
{
    using Traits = subscription_traits<Subscription>;
    using Message = typename Traits::MessageType;
    using Policy = typename FacilityT::template Delivery<Subscription>;
    static constexpr std::size_t capacity = Policy::capacity;

  public:
    RouteState() noexcept : slots_(capacity, capacity) {}

    void reset() noexcept
    {
        auto guard = storage_lock_.acquire();
        head_ = 0;
        tail_ = 0;
        count_ = 0;
        slots_.reset();
        for (std::size_t index = 0; index < capacity; ++index) {
            slots_.give();
        }
    }

    [[nodiscard]] RouteAcceptance accept(const Message& message, EmitMode mode) noexcept
    {
        using Overflow = typename Policy::Overflow;
        Result<void> slot_result = fail<solar::Error>({.status = solar::Status::WouldBlock});
        if constexpr (requires { Overflow::timeout; }) {
            if (mode == EmitMode::Normal) {
                slot_result = slots_.take(kernel::Timeout::after(Overflow::timeout.duration()));
            } else {
                slot_result = mode == EmitMode::Isr ? slots_.try_take_isr() : slots_.try_take();
            }
        } else {
            slot_result = mode == EmitMode::Isr ? slots_.try_take_isr() : slots_.try_take();
        }

        bool dropped_oldest_value = false;
        if (!slot_result) {
            if constexpr (std::is_same_v<Overflow, overflow::DropNewest>) {
                this->dropped_newest();
                return {.accepted = false, .dropped = true};
            } else if constexpr (std::is_same_v<Overflow, overflow::DropOldest>) {
                dropped_oldest_value = true;
            } else {
                const bool timed_out = requires { Overflow::timeout; } && mode == EmitMode::Normal;
                const auto status = timed_out ? Status::Timeout : Status::Full;
                this->rejected(status, timed_out);
                return {.status = status,
                        .accepted = false,
                        .needs_submit = false,
                        .timed_out = timed_out};
            }
        }

        std::uint32_t pending{};
        {
            auto guard = storage_lock_.acquire();
            if (dropped_oldest_value) {
                head_ = (head_ + 1) % capacity;
                --count_;
                this->dropped_oldest();
            }
            storage_[tail_] = store_payload(message);
            tail_ = (tail_ + 1) % capacity;
            ++count_;
            pending = static_cast<std::uint32_t>(count_);
        }
        this->accepted(pending);
        return {.accepted = true, .dropped = dropped_oldest_value, .needs_submit = true};
    }

    [[nodiscard]] bool drain_one() noexcept
    {
        PayloadBytes<Message> payload{};
        std::uint32_t pending{};
        {
            auto guard = storage_lock_.acquire();
            if (count_ == 0) {
                return false;
            }
            payload = storage_[head_];
            head_ = (head_ + 1) % capacity;
            --count_;
            pending = static_cast<std::uint32_t>(count_);
        }
        slots_.give();
        this->mutate([&](RouteRecord& record) { record.pending = pending; });
        const auto message = load_payload<Message>(payload);
        (void)this->template deliver<typename Traits::HandlerType>(message);
        return true;
    }

    [[nodiscard]] std::uint32_t clear_pending() noexcept
    {
        std::uint32_t removed{};
        {
            auto guard = storage_lock_.acquire();
            removed = static_cast<std::uint32_t>(count_);
            head_ = 0;
            tail_ = 0;
            count_ = 0;
            slots_.reset();
            for (std::size_t index = 0; index < capacity; ++index) {
                slots_.give();
            }
        }
        this->cancelled(removed);
        return removed;
    }

  private:
    std::array<PayloadBytes<Message>, capacity> storage_{};
    kernel::SpinLock storage_lock_{};
    kernel::Semaphore slots_;
    std::size_t head_{};
    std::size_t tail_{};
    std::size_t count_{};
};

template <typename FacilityT, typename Subscription>
class RouteState<FacilityT, Subscription, DeliveryKind::Latest> : public RouteStateCommon
{
    using Traits = subscription_traits<Subscription>;
    using Message = typename Traits::MessageType;

  public:
    void reset() noexcept
    {
        auto guard = storage_lock_.acquire();
        pending_ = false;
    }

    [[nodiscard]] RouteAcceptance accept(const Message& message, EmitMode) noexcept
    {
        bool replaced_value{};
        {
            auto guard = storage_lock_.acquire();
            replaced_value = pending_ || this->in_flight() != 0;
            payload_ = store_payload(message);
            pending_ = true;
        }
        if (replaced_value) {
            this->replaced();
        }
        this->accepted(1);
        return {.accepted = true, .dropped = false, .needs_submit = true};
    }

    [[nodiscard]] bool drain_one() noexcept
    {
        PayloadBytes<Message> payload{};
        {
            auto guard = storage_lock_.acquire();
            if (!pending_) {
                return false;
            }
            payload = payload_;
            pending_ = false;
        }
        this->mutate([](RouteRecord& record) { record.pending = 0; });
        const auto message = load_payload<Message>(payload);
        (void)this->template deliver<typename Traits::HandlerType>(message);
        return true;
    }

    [[nodiscard]] std::uint32_t clear_pending() noexcept
    {
        bool removed{};
        {
            auto guard = storage_lock_.acquire();
            removed = pending_;
            pending_ = false;
        }
        this->cancelled(removed ? 1U : 0U);
        return removed ? 1U : 0U;
    }

  private:
    kernel::SpinLock storage_lock_{};
    PayloadBytes<Message> payload_{};
    bool pending_{};
};

template <typename FacilityT, typename Subscription>
class RouteState<FacilityT, Subscription, DeliveryKind::Coalesced> : public RouteStateCommon
{
    using Traits = subscription_traits<Subscription>;
    using Message = typename Traits::MessageType;

  public:
    void reset() noexcept
    {
        pending_.store(false, std::memory_order_release);
    }

    [[nodiscard]] RouteAcceptance accept(const Message&, EmitMode) noexcept
    {
        const bool already_pending = pending_.exchange(true, std::memory_order_acq_rel);
        if (already_pending) {
            this->coalesced();
        }
        this->accepted(1);
        return {.accepted = true, .dropped = false, .needs_submit = !already_pending};
    }

    [[nodiscard]] bool drain_one() noexcept
    {
        if (!pending_.exchange(false, std::memory_order_acq_rel)) {
            return false;
        }
        this->mutate([](RouteRecord& record) { record.pending = 0; });
        const Message message{};
        (void)this->template deliver<typename Traits::HandlerType>(message);
        return true;
    }

    [[nodiscard]] std::uint32_t clear_pending() noexcept
    {
        const bool removed = pending_.exchange(false, std::memory_order_acq_rel);
        this->cancelled(removed ? 1U : 0U);
        return removed ? 1U : 0U;
    }

  private:
    std::atomic_bool pending_{};
};

template <typename Left, typename Right> [[nodiscard]] consteval bool same_logical_route()
{
    if constexpr (!Subscription<Left> || !Subscription<Right>) {
        return false;
    } else {
        using L = subscription_traits<Left>;
        using R = subscription_traits<Right>;
        return std::is_same_v<typename L::MessageType, typename R::MessageType> &&
               std::is_same_v<typename L::SubscriberType, typename R::SubscriberType> &&
               std::is_same_v<typename L::RouteTag, typename R::RouteTag>;
    }
}

template <typename... Routes> struct ValidateRoutePairs;

template <> struct ValidateRoutePairs<>
{
    static constexpr bool valid = true;
};

template <typename Route> struct ValidateRoutePairs<Route>
{
    static constexpr bool valid = true;
};

template <typename Head, typename... Tail> struct ValidateRoutePairs<Head, Tail...>
{
    static_assert((!same_logical_route<Head, Tail>() && ...),
                  "SOLAR_DIAGNOSTIC_DUPLICATE_BUS_SUBSCRIPTION: message, subscriber, and route tag "
                  "must form a unique subscription key");
    static constexpr bool valid = ValidateRoutePairs<Tail...>::valid;
};

template <typename Route, typename Messages, typename Components, typename Config,
          bool Valid = Subscription<Route>>
struct ValidateRoute;

template <typename Route, typename Messages, typename Components, typename Config>
struct ValidateRoute<Route, Messages, Components, Config, false>
{
    static_assert(solar::detail::dependent_false_v<Route>,
                  "SOLAR_DIAGNOSTIC_INVALID_BUS_SUBSCRIPTION: Bus subscription declaration is "
                  "malformed or unsupported");
    static constexpr bool valid = false;
};

template <typename Route, typename Messages, typename Components, typename Config>
struct ValidateRoute<Route, Messages, Components, Config, true>
{
    using Traits = subscription_traits<Route>;
    using Message = typename Traits::MessageType;
    using Subscriber = typename Traits::SubscriberType;
    using Handler = typename Traits::HandlerType;
    using AuthoredDelivery = typename Traits::DeliveryType;

    static_assert(contains_v<Message, Messages>,
                  "SOLAR_DIAGNOSTIC_BUS_MESSAGE_NOT_REGISTERED: subscription references a message "
                  "absent from the effective Bus message catalog");
    static_assert(contains_v<Subscriber, Components>,
                  "SOLAR_DIAGNOSTIC_BUS_SUBSCRIBER_NOT_REGISTERED: subscription targets a "
                  "component absent from the effective graph");
    static_assert(delivery_traits<AuthoredDelivery>::valid,
                  "SOLAR_DIAGNOSTIC_INVALID_BUS_DELIVERY: subscription delivery policy is invalid");
    static_assert(ValidHandler<Handler, Message>,
                  "SOLAR_DIAGNOSTIC_INVALID_BUS_HANDLER: subscriber or explicit handler must "
                  "implement handle(const Message&) returning void, Status, or Result<void>");

    using Delivery = EffectiveDelivery<AuthoredDelivery, Config>;
    static constexpr bool asynchronous = Delivery::asynchronous;
    static_assert(!asynchronous || (std::is_trivially_copyable_v<Message> &&
                                    std::is_trivially_destructible_v<Message>),
                  "SOLAR_DIAGNOSTIC_BUS_ASYNC_PAYLOAD: asynchronous Bus messages must be trivially "
                  "copyable and destructible");
#if defined(CONFIG_SOLAR_BUS)
    static_assert(!asynchronous || sizeof(Message) <= CONFIG_SOLAR_BUS_MAX_ASYNC_PAYLOAD_BYTES,
                  "SOLAR_DIAGNOSTIC_BUS_PAYLOAD_SIZE: asynchronous Bus payload exceeds the Kconfig "
                  "byte ceiling");
    static_assert(!asynchronous || alignof(Message) <= CONFIG_SOLAR_BUS_MAX_ASYNC_PAYLOAD_ALIGNMENT,
                  "SOLAR_DIAGNOSTIC_BUS_PAYLOAD_ALIGNMENT: asynchronous Bus payload exceeds the "
                  "Kconfig alignment ceiling");
    static_assert(Delivery::kind != DeliveryKind::Queued ||
                      (Delivery::capacity != 0 &&
                       Delivery::capacity <= CONFIG_SOLAR_BUS_MAX_ROUTE_CAPACITY),
                  "SOLAR_DIAGNOSTIC_BUS_ROUTE_CAPACITY: queued route capacity is zero or exceeds "
                  "the Kconfig ceiling");
#endif
    static_assert(Delivery::kind != DeliveryKind::Coalesced || std::is_empty_v<Message>,
                  "SOLAR_DIAGNOSTIC_BUS_COALESCED_PAYLOAD: Coalesced delivery accepts empty signal "
                  "messages only");
    static_assert(Delivery::kind != DeliveryKind::Coalesced || std::default_initializable<Message>,
                  "SOLAR_DIAGNOSTIC_BUS_COALESCED_CONSTRUCTION: Coalesced signal must be default "
                  "constructible");
    static_assert(!asynchronous ||
                      std::is_same_v<typename Delivery::Target, execution::SystemWorkQueue> ||
                      contains_v<typename Delivery::Target, Components>,
                  "SOLAR_DIAGNOSTIC_BUS_EXECUTOR_NOT_REGISTERED: deferred route target is absent "
                  "from the effective component graph");
    static_assert(!asynchronous ||
                      std::is_same_v<typename Delivery::Target, execution::SystemWorkQueue> ||
                      execution::WorkQueueExecutor<typename Delivery::Target>,
                  "SOLAR_DIAGNOSTIC_BUS_INVALID_EXECUTOR: deferred route target is not an "
                  "execution::WorkQueue");
    static_assert(std::is_same_v<typename Delivery::Stop, stop::Drain> ||
                      std::is_same_v<typename Delivery::Stop, stop::CancelPending>,
                  "SOLAR_DIAGNOSTIC_BUS_INVALID_STOP_POLICY: route stop policy is unsupported");
    static_assert(
        std::is_same_v<typename Delivery::Overflow, overflow::Reject> ||
            std::is_same_v<typename Delivery::Overflow, overflow::DropNewest> ||
            std::is_same_v<typename Delivery::Overflow, overflow::DropOldest> ||
            requires { Delivery::Overflow::timeout; },
        "SOLAR_DIAGNOSTIC_BUS_INVALID_OVERFLOW_POLICY: queued overflow policy is "
        "unsupported");

    static constexpr bool valid = true;
};

template <typename Policy, typename Routes> struct ValidateBusPolicy
{
    static constexpr bool valid = true;
};

template <typename Message, typename Routes>
struct ValidateBusPolicy<RequireSubscriber<Message>, Routes>
{
    template <typename Route>
    struct Matches : std::bool_constant<
                         std::is_same_v<Message, typename subscription_traits<Route>::MessageType>>
    {};

    static_assert(list_size_v<filter_t<Routes, Matches>> != 0,
                  "SOLAR_DIAGNOSTIC_BUS_REQUIRED_SUBSCRIBER: required message has no effective "
                  "subscriber");
    static constexpr bool valid = true;
};

template <typename Config, typename Routes> struct ValidateBusPolicies;

template <typename... Policies, typename Routes>
struct ValidateBusPolicies<TypeList<Policies...>, Routes>
{
    static constexpr bool valid = (ValidateBusPolicy<Policies, Routes>::valid && ...);
};

template <typename Route, typename Config> struct RouteDependencies
{
    using Traits = subscription_traits<Route>;
    using Delivery = EffectiveDelivery<typename Traits::DeliveryType, Config>;
    using TargetDependencies =
        std::conditional_t<Delivery::asynchronous && !std::is_same_v<typename Delivery::Target,
                                                                     execution::SystemWorkQueue>,
                           TypeList<typename Delivery::Target>, TypeList<>>;
    using type = concat_t<TypeList<typename Traits::SubscriberType>, TargetDependencies>;
};

template <typename Routes, typename Config> struct AllRouteDependencies;

template <typename... Routes, typename Config>
struct AllRouteDependencies<TypeList<Routes...>, Config>
{
    using type = unique_t<concat_t<typename RouteDependencies<Routes, Config>::type...>>;
};

template <typename FacilityT, typename Subscription, std::size_t Index> struct RouteExecution;

template <typename FacilityT, typename Subscription> struct RouteBehavior
{
    [[nodiscard]] static Result<void> execute() noexcept
    {
        FacilityT::template drain<Subscription>();
        return {};
    }
};

struct RouteName
{
    std::array<char, 24> bytes{};
    std::size_t length{};

    [[nodiscard]] constexpr std::string_view view() const noexcept
    {
        return {bytes.data(), length};
    }
};

template <std::size_t Index> [[nodiscard]] consteval RouteName make_route_name()
{
    RouteName name{};
    constexpr std::string_view prefix{"bus.route."};
    for (const char character : prefix) {
        name.bytes[name.length++] = character;
    }
    std::array<char, 20> digits{};
    std::size_t count{};
    auto value = Index;
    do {
        digits[count++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    } while (value != 0);
    while (count != 0) {
        name.bytes[name.length++] = digits[--count];
    }
    return name;
}

template <typename FacilityT, typename Subscription, std::size_t Index> struct RouteExecution
{
    using FacilityType = FacilityT;
    using SubscriptionType = Subscription;
    static constexpr auto name = make_route_name<Index>();
    static constexpr execution::Descriptor descriptor{
        .name = name.view(),
        .kind = execution::RegistrationKind::OnDemand,
    };
};

template <typename FacilityT, typename Routes, std::size_t Index = 0> struct BuildRouteExecutions;

template <typename FacilityT, std::size_t Index>
struct BuildRouteExecutions<FacilityT, TypeList<>, Index>
{
    using type = TypeList<>;
};

template <typename FacilityT, typename Head, typename... Tail, std::size_t Index>
struct BuildRouteExecutions<FacilityT, TypeList<Head, Tail...>, Index>
{
    using Delivery = typename FacilityT::template Delivery<Head>;
    using Current =
        std::conditional_t<Delivery::asynchronous, TypeList<RouteExecution<FacilityT, Head, Index>>,
                           TypeList<>>;
    using type =
        concat_t<Current,
                 typename BuildRouteExecutions<FacilityT, TypeList<Tail...>, Index + 1>::type>;
};

template <typename List> struct AsTasks;

template <typename... Registrations> struct AsTasks<TypeList<Registrations...>>
{
    using type = execution::Tasks<Registrations...>;
};

template <typename List> struct AsDependencies;

template <typename... Components> struct AsDependencies<TypeList<Components...>>
{
    using type = Dependencies<Components...>;
};

template <typename FacilityT, typename Subscription> consteval std::size_t payload_storage_bytes()
{
    using Delivery = typename FacilityT::template Delivery<Subscription>;
    using Message = typename subscription_traits<Subscription>::MessageType;
    if constexpr (Delivery::kind == DeliveryKind::Queued) {
        return sizeof(Message) * Delivery::capacity;
    } else if constexpr (Delivery::kind == DeliveryKind::Latest) {
        return sizeof(Message);
    } else {
        return 0;
    }
}

} // namespace detail

template <typename MessageDeclarations, typename SubscriptionDeclarations, typename Components,
          typename Configuration>
struct Architecture
{
    using Messages = MessageDeclarations;
    using Subscriptions = SubscriptionDeclarations;
    using ComponentTypes = Components;
    using ConfigurationPolicies = Configuration;

    static_assert(detail::ValidateRoutePairs<>::valid);

    template <typename Route>
    struct ValidateOne
        : std::bool_constant<
              detail::ValidateRoute<Route, Messages, Components, Configuration>::valid>
    {};

    static_assert([]<typename... Routes>(TypeList<Routes...>) {
        return detail::ValidateRoutePairs<Routes...>::valid && (ValidateOne<Routes>::value && ...);
    }(Subscriptions{}));
    static_assert(detail::ValidateBusPolicies<Configuration, Subscriptions>::valid);

    using Dependencies = typename detail::AllRouteDependencies<Subscriptions, Configuration>::type;
    static constexpr bool demanded = list_size_v<Messages> != 0 || list_size_v<Subscriptions> != 0;
};

template <typename ArchitectureT> struct Facility
{
    using Architecture = ArchitectureT;
    using MessageTypes = typename Architecture::Messages;
    using RouteTypes = typename Architecture::Subscriptions;
    using Configuration = typename Architecture::ConfigurationPolicies;

    static constexpr component::Descriptor descriptor{
        .name = "solar.bus",
        .description = "Typed application bus",
    };

    template <typename Subscription>
    using Delivery =
        detail::EffectiveDelivery<typename subscription_traits<Subscription>::DeliveryType,
                                  Configuration>;

    using Dependencies = typename detail::AsDependencies<typename Architecture::Dependencies>::type;
    using RouteExecutions = typename detail::BuildRouteExecutions<Facility, RouteTypes>::type;
    using Tasks = typename detail::AsTasks<RouteExecutions>::type;

    static constexpr std::size_t asynchronous_route_count = list_size_v<RouteExecutions>;
    static constexpr std::size_t payload_storage_bytes =
        []<typename... Routes>(TypeList<Routes...>) {
            return (detail::payload_storage_bytes<Facility, Routes>() + ... + 0U);
        }(RouteTypes{});
    static constexpr std::size_t route_state_bytes = []<typename... Routes>(TypeList<Routes...>) {
        return (sizeof(detail::RouteState<Facility, Routes>) + ... + 0U);
    }(RouteTypes{});

    template <typename Subscription>
    inline static detail::RouteState<Facility, Subscription> route_state{};

    [[nodiscard]] static Result<void> init() noexcept
    {
        for_each_type<RouteTypes>([]<typename Subscription> {
            using Message = typename subscription_traits<Subscription>::MessageType;
            auto& state = route_state<Subscription>;
            state.initialize(SubscriptionLocalId{static_cast<SubscriptionLocalId::Representation>(
                                 detail::type_index_v<Subscription, RouteTypes>)},
                             MessageLocalId{static_cast<MessageLocalId::Representation>(
                                 detail::type_index_v<Message, MessageTypes>)},
                             Delivery<Subscription>::kind);
            if constexpr (Delivery<Subscription>::asynchronous) {
                state.reset();
            }
        });
        return {};
    }

    [[nodiscard]] static Result<void> start() noexcept
    {
        for_each_type<RouteTypes>(
            []<typename Subscription> { route_state<Subscription>.set_accepting(true); });
        return {};
    }

    [[nodiscard]] static Result<void> stop() noexcept
    {
        Status status = Status::Ok;
        for_each_type<RouteTypes>([&]<typename Subscription> {
            auto& state = route_state<Subscription>;
            state.set_accepting(false);
            if constexpr (Delivery<Subscription>::asynchronous) {
                if constexpr (std::is_same_v<typename Delivery<Subscription>::Stop,
                                             stop::CancelPending>) {
                    (void)state.clear_pending();
                } else if (state.copy().pending != 0) {
                    status = Status::Timeout;
                }
            }
        });

        const auto deadline = kernel::Deadline::after(
            kernel::Timeout::after(Milliseconds{CONFIG_SOLAR_BUS_STOP_TIMEOUT_MS}));
        bool waiting = true;
        while (waiting && !deadline.expired()) {
            waiting = false;
            for_each_type<RouteTypes>([&]<typename Subscription> {
                waiting = waiting || route_state<Subscription>.in_flight() != 0;
            });
            if (waiting) {
                (void)kernel::this_thread::sleep_for(Milliseconds{1});
            }
        }
        for_each_type<RouteTypes>(
            []<typename Subscription> { route_state<Subscription>.finish_stop(); });
        return status == Status::Ok && !waiting
                   ? Result<void>{}
                   : Result<void>{fail<solar::Error>({.status = solar::Status::Timeout})};
    }

    template <typename Subscription> static void drain() noexcept
    {
        auto& state = route_state<Subscription>;
        if constexpr (Delivery<Subscription>::kind == DeliveryKind::Queued) {
            while (state.drain_one()) {
            }
        } else if constexpr (Delivery<Subscription>::asynchronous) {
            (void)state.drain_one();
        }
    }

    template <typename System, typename Message, detail::EmitMode Mode>
    [[nodiscard]] static Result<void, Error> emit(const Message& message) noexcept;
};

#endif

} // namespace solar::bus

template <typename Architecture> struct solar::builtin_traits<solar::bus::Facility<Architecture>>
{
    static constexpr bool enabled = solar::bus::enabled;
    static constexpr bool always_present = false;
    using Requirements = solar::TypeList<>;

    template <typename> static constexpr bool demanded = Architecture::demanded;
};

#if defined(__ZEPHYR__) && defined(CONFIG_SOLAR_BUS)
template <typename Facility, typename Subscription, std::size_t Index>
struct solar::execution::registration_traits<
    solar::bus::detail::RouteExecution<Facility, Subscription, Index>>
{
    using RegistrationType = solar::bus::detail::RouteExecution<Facility, Subscription, Index>;
    using RouteDelivery = typename Facility::template Delivery<Subscription>;

    static constexpr bool valid = true;
    static constexpr RegistrationKind kind = RegistrationKind::OnDemand;
    using BehaviorType = solar::bus::detail::RouteBehavior<Facility, Subscription>;
    using OptionsList = solar::TypeList<>;
    using Target = typename RouteDelivery::Target;
    using Dependencies = solar::TypeList<>;
    using Admission = NativeCoalescing;
    using StopPolicy = std::conditional_t<
        std::is_same_v<typename RouteDelivery::Stop, solar::bus::stop::CancelPending>,
        stop::CancelPending, stop::Drain>;
    using FailurePolicy = failure::RecordAndContinue;
};
#endif
