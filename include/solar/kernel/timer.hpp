#pragma once

#include <chrono>
#include <cstdint>

#include <zephyr/kernel.h>

#include "solar/core/status.hpp"
#include "solar/kernel/interrupt.hpp"
#include "solar/kernel/time.hpp"

namespace solar::kernel
{

/** Non-owning operations on an initialized native Zephyr timer. */
class TimerRef
{
  public:
    explicit constexpr TimerRef(k_timer& timer) noexcept : timer_(&timer) {}

    [[nodiscard]] Result<void> start(Timeout initial,
                                     Timeout period = Timeout::no_wait()) const noexcept
    {
        if (in_isr()) {
            return fail<Error>({.status = Status::Invalid});
        }
        k_timer_start(timer_, initial.native_handle(), period.native_handle());
        return {};
    }

    void stop() const noexcept
    {
        k_timer_stop(timer_);
    }
    [[nodiscard]] std::uint32_t expirations() const noexcept
    {
        return k_timer_status_get(timer_);
    }

    [[nodiscard]] Result<std::uint32_t> sync() const noexcept
    {
        if (in_isr()) {
            return fail<solar::Error>({.status = solar::Status::Invalid});
        }
        return k_timer_status_sync(timer_);
    }

    [[nodiscard]] TickDuration remaining() const noexcept
    {
        return from_ticks(static_cast<Tick>(k_timer_remaining_ticks(timer_)));
    }

    [[nodiscard]] TimePoint expires_at() const noexcept
    {
        return TimePoint{TickDuration{static_cast<Tick>(k_timer_expires_ticks(timer_))}};
    }

    [[nodiscard]] bool running() const noexcept
    {
        return k_timer_remaining_ticks(timer_) != 0;
    }

  private:
    k_timer* timer_;
};

class Timer
{
  public:
    using Callback = void (*)(Timer&) noexcept;

    explicit Timer(Callback expiry_callback = nullptr, Callback stop_callback = nullptr) noexcept
        : expiry_callback_(expiry_callback), stop_callback_(stop_callback)
    {
        k_timer_init(&timer_, &Timer::on_expiry, &Timer::on_stop);
        k_timer_user_data_set(&timer_, this);
    }

    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
    Timer(Timer&&) = delete;
    Timer& operator=(Timer&&) = delete;

    [[nodiscard]] Result<void> start(Timeout initial, Timeout period = Timeout::no_wait()) noexcept
    {
        return ref().start(initial, period);
    }

    template <typename InitialRep, typename InitialPeriod>
    [[nodiscard]] Result<void>
    start_after(std::chrono::duration<InitialRep, InitialPeriod> initial) noexcept
    {
        return start(Timeout::after(initial));
    }

    template <typename InitialRep, typename InitialPeriod, typename RepeatRep,
              typename RepeatPeriod>
    [[nodiscard]] Result<void>
    start_periodic(std::chrono::duration<InitialRep, InitialPeriod> initial,
                   std::chrono::duration<RepeatRep, RepeatPeriod> period) noexcept
    {
        return start(Timeout::after(initial), Timeout::after(period));
    }

    void stop() noexcept
    {
        ref().stop();
    }

    [[nodiscard]] std::uint32_t expirations() noexcept
    {
        return ref().expirations();
    }

    [[nodiscard]] Result<std::uint32_t> sync() noexcept
    {
        return ref().sync();
    }

    [[nodiscard]] TickDuration remaining() const noexcept
    {
        return TimerRef{const_cast<k_timer&>(timer_)}.remaining();
    }

    [[nodiscard]] TimePoint expires_at() const noexcept
    {
        return TimerRef{const_cast<k_timer&>(timer_)}.expires_at();
    }

    [[nodiscard]] bool running() const noexcept
    {
        return TimerRef{const_cast<k_timer&>(timer_)}.running();
    }

    [[nodiscard]] TimerRef ref() noexcept
    {
        return TimerRef{timer_};
    }

  private:
    static void on_expiry(k_timer* timer) noexcept
    {
        auto* self = static_cast<Timer*>(k_timer_user_data_get(timer));
        if (self != nullptr && self->expiry_callback_ != nullptr) {
            self->expiry_callback_(*self);
        }
    }

    static void on_stop(k_timer* timer) noexcept
    {
        auto* self = static_cast<Timer*>(k_timer_user_data_get(timer));
        if (self != nullptr && self->stop_callback_ != nullptr) {
            self->stop_callback_(*self);
        }
    }

    k_timer timer_{};
    Callback expiry_callback_{};
    Callback stop_callback_{};
};

} // namespace solar::kernel
