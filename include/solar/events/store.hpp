#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <optional>
#include <tuple>

#include "solar/core/spin_mutex.hpp"
#include "solar/core/status.hpp"
#include "solar/core/type_list.hpp"

namespace solar::events
{

template <typename... Events> struct Schema
{
    using Entries = TypeList<Events...>;
};
struct NoRetention
{};
struct RetainLatest
{};
template <typename SchemaT, std::size_t MaximumObservers, typename Retention = NoRetention>
class EventBus;

/** Synchronous, allocation-free typed event fan-out. */
template <typename... Events, std::size_t MaximumObservers, typename Retention>
class EventBus<Schema<Events...>, MaximumObservers, Retention>
{
    static_assert(MaximumObservers > 0);
    static_assert(std::same_as<Retention, NoRetention> || std::same_as<Retention, RetainLatest>);
    template <typename Event> struct ObserverSet
    {
        using Function = void (*)(const typename Event::Value&) noexcept;
        std::array<Function, MaximumObservers> values{};
        std::optional<typename Event::Value> retained{};
    };

  public:
    [[nodiscard]] Result<void> initialize() noexcept
    {
        SpinGuard lock{mutex_};
        observers_ = {};
        return {};
    }
    template <typename Event>
    [[nodiscard]] Result<void> observe(typename ObserverSet<Event>::Function function) noexcept
    {
        require_member<Event>();
        std::optional<typename Event::Value> retained;
        {
            SpinGuard lock{mutex_};
            auto& observers = std::get<ObserverSet<Event>>(observers_);
            for (auto& slot : observers.values) {
                if (slot == nullptr) {
                    slot = function;
                    if constexpr (std::same_as<Retention, RetainLatest>) {
                        retained = observers.retained;
                    }
                    if (retained) {
                        break;
                    }
                    return {};
                }
            }
            if (!retained) {
                return fail<solar::Error>({.status = Status::NoSpace});
            }
        }
        function(*retained);
        return {};
    }
    template <typename Event>
    [[nodiscard]] Result<void> unobserve(typename ObserverSet<Event>::Function function) noexcept
    {
        require_member<Event>();
        SpinGuard lock{mutex_};
        for (auto& slot : std::get<ObserverSet<Event>>(observers_).values) {
            if (slot == function) {
                slot = nullptr;
                return {};
            }
        }
        return fail<solar::Error>({.status = Status::NotFound});
    }
    template <typename Event>
    [[nodiscard]] Result<std::size_t> emit(const typename Event::Value& value) noexcept
    {
        require_member<Event>();
        std::array<typename ObserverSet<Event>::Function, MaximumObservers> observers{};
        {
            SpinGuard lock{mutex_};
            auto& state = std::get<ObserverSet<Event>>(observers_);
            observers = state.values;
            if constexpr (std::same_as<Retention, RetainLatest>) {
                state.retained = value;
            }
        }
        std::size_t delivered{};
        for (auto observer : observers)
            if (observer != nullptr) {
                observer(value);
                ++delivered;
            }
        return delivered;
    }

  private:
    template <typename Event> static consteval void require_member()
    {
        static_assert(contains_v<Event, TypeList<Events...>>,
                      "SOLAR_EVENTS_NOT_IN_SCHEMA: event is absent from the closed schema");
    }
    std::tuple<ObserverSet<Events>...> observers_{};
    mutable SpinMutex mutex_{};
};

template <typename Application, typename SchemaT, std::size_t MaximumObservers,
          typename Retention = NoRetention>
struct StaticEventBus
{
    inline static EventBus<SchemaT, MaximumObservers, Retention> storage{};
    [[nodiscard]] static Result<void> initialize() noexcept
    {
        return storage.initialize();
    }
    template <typename Event> [[nodiscard]] static auto observe(auto function) noexcept
    {
        return storage.template observe<Event>(function);
    }
    template <typename Event> [[nodiscard]] static auto unobserve(auto function) noexcept
    {
        return storage.template unobserve<Event>(function);
    }
    template <typename Event>
    [[nodiscard]] static auto emit(const typename Event::Value& value) noexcept
    {
        return storage.template emit<Event>(value);
    }
};

} // namespace solar::events
