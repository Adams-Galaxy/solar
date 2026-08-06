#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

#include "solar/core/spin_mutex.hpp"
#include "solar/core/status.hpp"
#include "solar/core/type_list.hpp"

namespace solar::remote
{

struct ServerConfig
{
    std::size_t maximum_sessions{1};
    std::size_t maximum_subscriptions{16};
    std::size_t maximum_inflight_requests{8};
    std::size_t maximum_payload_bytes{1024};
    std::uint16_t initial_input_credit{1};
};

struct Session
{
    std::uint32_t value{};
    constexpr bool operator==(const Session&) const = default;
};

struct Request
{
    Session session{};
    std::uint32_t correlation{};
    constexpr bool operator==(const Request&) const = default;
};

/**
 * Independently owned, bounded Remote endpoint server.
 *
 * Server owns sessions and subscription routing. A byte transport may decode a
 * request and call this API, but the server itself neither selects a transport
 * nor depends on System. Output streams are pushed to subscribed sessions;
 * they are not represented as polled data values.
 */
template <typename Contract, ServerConfig Config = ServerConfig{}> class Server
{
    static_assert(Config.maximum_sessions > 0);
    static_assert(Config.maximum_subscriptions > 0);
    static_assert(Config.maximum_inflight_requests > 0);
    static_assert(Config.maximum_payload_bytes > 0);

  public:
    using Parameters = typename Contract::Parameters;
    using Endpoints = typename Contract::Endpoints;
    using Declarations = typename Contract::Declarations;

    [[nodiscard]] Result<void> initialize()
    {
        SpinGuard lock{mutex_};
        sessions_.fill({});
        subscriptions_.fill({});
        requests_.fill({});
        next_session_ = 1;
        initialized_ = true;
        active_ = false;
        return {};
    }

    [[nodiscard]] Result<void> start()
    {
        SpinGuard lock{mutex_};
        if (!initialized_) {
            return fail<solar::Error>({.status = Status::NotReady});
        }
        if (active_) {
            return fail<solar::Error>({.status = Status::Already});
        }
        active_ = true;
        return {};
    }

    [[nodiscard]] Result<void> stop()
    {
        SpinGuard lock{mutex_};
        active_ = false;
        sessions_.fill({});
        subscriptions_.fill({});
        requests_.fill({});
        return {};
    }

    [[nodiscard]] Result<void> deinitialize()
    {
        SpinGuard lock{mutex_};
        active_ = false;
        initialized_ = false;
        sessions_.fill({});
        subscriptions_.fill({});
        requests_.fill({});
        return {};
    }

    [[nodiscard]] Result<Session> open_session()
    {
        SpinGuard lock{mutex_};
        if (!active_) {
            return fail<solar::Error>({.status = Status::NotReady});
        }
        for (auto& slot : sessions_) {
            if (!slot.active) {
                slot = {.id = next_session_++, .active = true};
                if (next_session_ == 0) {
                    next_session_ = 1;
                }
                return Session{slot.id};
            }
        }
        return fail<solar::Error>({.status = Status::NoSpace});
    }

    [[nodiscard]] Result<void> close_session(Session session)
    {
        SpinGuard lock{mutex_};
        auto* slot = find_session(session);
        if (slot == nullptr) {
            return fail<solar::Error>({.status = Status::NotFound});
        }
        slot->active = false;
        for (auto& subscription : subscriptions_) {
            if (subscription.active && subscription.session == session.value) {
                subscription.active = false;
            }
        }
        for (auto& request : requests_) {
            if (request.active && request.session == session.value)
                request.active = false;
        }
        return {};
    }

    [[nodiscard]] Result<Request> begin_request(Session session, std::uint32_t correlation,
                                                std::size_t payload_bytes)
    {
        SpinGuard lock{mutex_};
        if (!active_ || find_session(session) == nullptr)
            return fail<solar::Error>({.status = Status::NotFound});
        if (payload_bytes > Config.maximum_payload_bytes)
            return fail<solar::Error>({.status = Status::MessageTooLarge});
        for (const auto& request : requests_)
            if (request.active && request.session == session.value &&
                request.correlation == correlation)
                return fail<solar::Error>({.status = Status::Already});
        for (auto& request : requests_)
            if (!request.active) {
                request = {.session = session.value, .correlation = correlation, .active = true};
                return Request{session, correlation};
            }
        return fail<solar::Error>({.status = Status::NoSpace});
    }

    [[nodiscard]] Result<void> complete_request(Request token)
    {
        SpinGuard lock{mutex_};
        for (auto& request : requests_) {
            if (request.active && request.session == token.session.value &&
                request.correlation == token.correlation) {
                request.active = false;
                return {};
            }
        }
        return fail<solar::Error>({.status = Status::NotFound});
    }

    template <typename Parameter>
    [[nodiscard]] Result<typename Parameter::Value> get(Session session)
    {
        if (!valid_session(session)) {
            return fail<solar::Error>({.status = Status::NotFound});
        }
        return Parameters::template get<Parameter>();
    }

    template <typename Parameter>
    [[nodiscard]] Result<void> set(Session session, typename Parameter::Value value)
    {
        if (!valid_session(session)) {
            return fail<solar::Error>({.status = Status::NotFound});
        }
        auto result = Parameters::template set<Parameter>(std::move(value));
        return result ? Result<void>{} : fail<solar::Error>(result.error());
    }

    template <typename Action>
    [[nodiscard]] Result<typename Action::Response> call(Session session,
                                                         const typename Action::Request& request)
    {
        if (!valid_session(session)) {
            return fail<solar::Error>({.status = Status::NotFound});
        }
        return Endpoints::template call<Action>(request);
    }

    template <typename Stream>
    [[nodiscard]] Result<void> subscribe(Session session, std::uint32_t maximum_rate_hz)
    {
        static_assert(!Stream::input,
                      "SOLAR_REMOTE_SUBSCRIBE_INPUT_STREAM: input streams must be opened");
        return add_subscription<Stream>(session, maximum_rate_hz, false);
    }

    template <typename Stream>
    [[nodiscard]] Result<void> open_input(Session session, std::uint32_t maximum_rate_hz)
    {
        static_assert(Stream::input,
                      "SOLAR_REMOTE_OPEN_OUTPUT_STREAM: output streams must be subscribed");
        return add_subscription<Stream>(session, maximum_rate_hz, true);
    }

    template <typename Stream> [[nodiscard]] Result<void> unsubscribe(Session session)
    {
        SpinGuard lock{mutex_};
        for (auto& slot : subscriptions_) {
            if (slot.active && slot.session == session.value && slot.endpoint == Stream::id) {
                slot.active = false;
                return {};
            }
        }
        return fail<solar::Error>({.status = Status::NotFound});
    }

    template <typename Stream>
    [[nodiscard]] Result<void> consume(Session session, const typename Stream::Value& value)
    {
        static_assert(Stream::input);
        if (!take_credit<Stream>(session)) {
            return fail<solar::Error>({.status = Status::WouldBlock});
        }
        return Endpoints::template consume<Stream>(value);
    }

    template <typename Stream>
    [[nodiscard]] Result<void> grant(Session session, std::uint16_t credits)
    {
        static_assert(Stream::input);
        SpinGuard lock{mutex_};
        for (auto& slot : subscriptions_) {
            if (slot.active && slot.input && slot.session == session.value &&
                slot.endpoint == Stream::id) {
                const auto room = static_cast<std::uint32_t>(
                    std::numeric_limits<std::uint16_t>::max() - slot.credits);
                slot.credits = static_cast<std::uint16_t>(
                    slot.credits + (std::min)(room, static_cast<std::uint32_t>(credits)));
                return {};
            }
        }
        return fail<solar::Error>({.status = Status::NotFound});
    }

    template <typename Stream, typename Sink>
    [[nodiscard]] Result<std::size_t> publish(const typename Stream::Value& value, Sink& sink,
                                              std::uint64_t now_us)
    {
        static_assert(!Stream::input);
        std::array<Session, Config.maximum_sessions> recipients{};
        std::size_t count{};
        {
            SpinGuard lock{mutex_};
            if (!active_) {
                return fail<solar::Error>({.status = Status::NotReady});
            }
            for (auto& subscription : subscriptions_) {
                if (subscription.active && !subscription.input &&
                    subscription.endpoint == Stream::id &&
                    now_us >= subscription.next_delivery_us) {
                    subscription.next_delivery_us = now_us + subscription.minimum_interval_us;
                    bool already_added = false;
                    for (std::size_t index = 0; index < count; ++index) {
                        already_added |= recipients[index].value == subscription.session;
                    }
                    if (!already_added && count < recipients.size()) {
                        recipients[count++] = Session{subscription.session};
                    }
                }
            }
        }

        for (std::size_t index = 0; index < count; ++index) {
            if (auto result = sink.template send<Stream>(recipients[index], value); !result) {
                return fail<solar::Error>(result.error());
            }
        }
        return count;
    }

    template <typename Stream, typename Sink>
    [[nodiscard]] Result<std::size_t> publish(const typename Stream::Value& value, Sink& sink)
    {
        return publish<Stream>(value, sink, std::numeric_limits<std::uint64_t>::max());
    }

    [[nodiscard]] std::size_t session_count() const
    {
        SpinGuard lock{mutex_};
        std::size_t count{};
        for (const auto& session : sessions_) {
            count += session.active ? 1U : 0U;
        }
        return count;
    }

  private:
    struct SessionSlot
    {
        std::uint32_t id{};
        bool active{};
    };

    struct SubscriptionSlot
    {
        std::uint32_t session{};
        std::uint32_t endpoint{};
        std::uint32_t maximum_rate_hz{};
        std::uint32_t minimum_interval_us{};
        std::uint64_t next_delivery_us{};
        bool input{};
        bool active{};
        std::uint16_t credits{};
    };

    struct RequestSlot
    {
        std::uint32_t session{};
        std::uint32_t correlation{};
        bool active{};
    };

    [[nodiscard]] SessionSlot* find_session(Session session)
    {
        for (auto& slot : sessions_) {
            if (slot.active && slot.id == session.value) {
                return &slot;
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool valid_session(Session session)
    {
        SpinGuard lock{mutex_};
        return active_ && find_session(session) != nullptr;
    }

    template <typename Stream>
    [[nodiscard]] Result<void> add_subscription(Session session, std::uint32_t maximum_rate_hz,
                                                bool input)
    {
        if (maximum_rate_hz == 0)
            return fail<solar::Error>({.status = Status::Invalid});
        if constexpr (requires { Stream::maximum_rate_hz; }) {
            if (maximum_rate_hz > Stream::maximum_rate_hz)
                return fail<solar::Error>({.status = Status::Invalid});
        }
        SpinGuard lock{mutex_};
        if (!active_ || find_session(session) == nullptr) {
            return fail<solar::Error>({.status = Status::NotFound});
        }
        for (auto& slot : subscriptions_) {
            if (slot.active && slot.session == session.value && slot.endpoint == Stream::id) {
                slot.maximum_rate_hz = maximum_rate_hz;
                slot.minimum_interval_us = 1'000'000U / maximum_rate_hz;
                slot.next_delivery_us = 0;
                slot.input = input;
                slot.credits = input ? Config.initial_input_credit : 0;
                return {};
            }
        }
        for (auto& slot : subscriptions_) {
            if (!slot.active) {
                slot = {.session = session.value,
                        .endpoint = Stream::id,
                        .maximum_rate_hz = maximum_rate_hz,
                        .minimum_interval_us = 1'000'000U / maximum_rate_hz,
                        .next_delivery_us = 0,
                        .input = input,
                        .active = true,
                        .credits =
                            static_cast<std::uint16_t>(input ? Config.initial_input_credit : 0U)};
                return {};
            }
        }
        return fail<solar::Error>({.status = Status::NoSpace});
    }

    template <typename Stream> [[nodiscard]] bool take_credit(Session session)
    {
        SpinGuard lock{mutex_};
        for (auto& slot : subscriptions_) {
            if (slot.active && slot.session == session.value && slot.endpoint == Stream::id &&
                slot.input) {
                if (slot.credits == 0)
                    return false;
                --slot.credits;
                return true;
            }
        }
        return false;
    }

    mutable SpinMutex mutex_{};
    std::array<SessionSlot, Config.maximum_sessions> sessions_{};
    std::array<SubscriptionSlot, Config.maximum_subscriptions> subscriptions_{};
    std::array<RequestSlot, Config.maximum_inflight_requests> requests_{};
    std::uint32_t next_session_{1};
    bool initialized_{};
    bool active_{};
};

/** Canonical static application facade over one standalone Server. */
template <typename Application, typename Contract, ServerConfig Config = ServerConfig{}>
struct StaticServer
{
    using ApplicationType = Application;
    using ContractType = Contract;
    using Dependencies = TypeList<typename Contract::Parameters>;

    inline static Server<Contract, Config> storage{};

    [[nodiscard]] static Result<void> initialize()
    {
        return storage.initialize();
    }
    [[nodiscard]] static Result<void> start()
    {
        return storage.start();
    }
    [[nodiscard]] static Result<void> stop()
    {
        return storage.stop();
    }
    [[nodiscard]] static Result<void> deinitialize()
    {
        return storage.deinitialize();
    }

    [[nodiscard]] static Result<Session> open_session()
    {
        return storage.open_session();
    }

    [[nodiscard]] static Result<void> close_session(Session session)
    {
        return storage.close_session(session);
    }

    [[nodiscard]] static auto begin_request(Session session, std::uint32_t correlation,
                                            std::size_t payload_bytes)
    {
        return storage.begin_request(session, correlation, payload_bytes);
    }

    [[nodiscard]] static Result<void> complete_request(Request request)
    {
        return storage.complete_request(request);
    }

    template <typename Parameter> [[nodiscard]] static auto get(Session session)
    {
        return storage.template get<Parameter>(session);
    }

    template <typename Parameter>
    [[nodiscard]] static Result<void> set(Session session, typename Parameter::Value value)
    {
        return storage.template set<Parameter>(session, std::move(value));
    }

    template <typename Action>
    [[nodiscard]] static auto call(Session session, const typename Action::Request& request)
    {
        return storage.template call<Action>(session, request);
    }

    template <typename Stream>
    [[nodiscard]] static Result<void> subscribe(Session session,
                                                std::uint32_t maximum_rate_hz)
    {
        return storage.template subscribe<Stream>(session, maximum_rate_hz);
    }

    template <typename Stream>
    [[nodiscard]] static Result<void> open_input(Session session,
                                                 std::uint32_t maximum_rate_hz)
    {
        return storage.template open_input<Stream>(session, maximum_rate_hz);
    }

    template <typename Stream>
    [[nodiscard]] static Result<void> consume(Session session, const typename Stream::Value& value)
    {
        return storage.template consume<Stream>(session, value);
    }

    template <typename Stream>
    [[nodiscard]] static Result<void> grant(Session session, std::uint16_t credits)
    {
        return storage.template grant<Stream>(session, credits);
    }

    template <typename Stream, typename Sink>
    [[nodiscard]] static Result<std::size_t> publish(const typename Stream::Value& value,
                                                     Sink& sink)
    {
        return storage.template publish<Stream>(value, sink);
    }

    template <typename Stream, typename Sink>
    [[nodiscard]] static Result<std::size_t> publish(const typename Stream::Value& value,
                                                     Sink& sink, std::uint64_t now_us)
    {
        return storage.template publish<Stream>(value, sink, now_us);
    }
};

} // namespace solar::remote
