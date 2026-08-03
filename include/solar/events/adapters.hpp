#pragma once

#include <array>
#include <cstddef>
#include <optional>

#include "solar/core/spin_mutex.hpp"
#include "solar/core/status.hpp"

namespace solar::events
{

/** Named composition adapter forwarding one typed event to one observer. */
struct Forward
{
    template <typename Bus, typename Event, typename Observer>
    [[nodiscard]] static Result<void> connect() noexcept
    {
        return Bus::template observe<Event>(&Observer::observe);
    }

    template <typename Bus, typename Event, typename Observer>
    [[nodiscard]] static Result<void> disconnect() noexcept
    {
        return Bus::template unobserve<Event>(&Observer::observe);
    }
};

/** Bounded scheduled delivery adapter using an explicitly supplied TaskQueue. */
template <typename Event, typename Observer, std::size_t Capacity> class ScheduledForward
{
    static_assert(Capacity > 0);

  public:
    ScheduledForward() noexcept
    {
        for (std::size_t index{}; index < Capacity; ++index) {
            contexts_[index] = {.owner = this, .index = index};
        }
    }

    template <typename Queue>
    [[nodiscard]] Result<void> emit(const typename Event::Value& value, Queue& queue) noexcept
    {
        std::size_t selected = Capacity;
        {
            SpinGuard lock{mutex_};
            for (std::size_t index{}; index < Capacity; ++index) {
                if (!values_[index]) {
                    values_[index] = value;
                    selected = index;
                    break;
                }
            }
        }
        if (selected == Capacity) {
            return fail<solar::Error>({.status = Status::NoSpace});
        }
        auto queued = queue.enqueue(&deliver, &contexts_[selected]);
        if (!queued) {
            SpinGuard lock{mutex_};
            values_[selected].reset();
            return fail<solar::Error>(queued.error());
        }
        return {};
    }

  private:
    struct Context
    {
        ScheduledForward* owner{};
        std::size_t index{};
    };

    static void deliver(void* opaque) noexcept
    {
        auto& context = *static_cast<Context*>(opaque);
        std::optional<typename Event::Value> value;
        {
            SpinGuard lock{context.owner->mutex_};
            value = std::move(context.owner->values_[context.index]);
            context.owner->values_[context.index].reset();
        }
        if (value) {
            Observer::observe(*value);
        }
    }

    std::array<std::optional<typename Event::Value>, Capacity> values_{};
    std::array<Context, Capacity> contexts_{};
    SpinMutex mutex_{};
};

} // namespace solar::events
