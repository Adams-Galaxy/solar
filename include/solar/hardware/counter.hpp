#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

#include <zephyr/drivers/counter.h>

#include "solar/hardware/endpoint.hpp"

namespace solar::hardware::counter
{

inline constexpr bool available =
#if defined(CONFIG_SOLAR_HARDWARE_COUNTER)
    true;
#else
    false;
#endif

template <auto Spec> struct Counter : hardware::Endpoint<Spec>
{
    static_assert(available, "SOLAR_DIAGNOSTIC_HARDWARE_COUNTER_DISABLED: Counter wrappers require "
                             "CONFIG_SOLAR_HARDWARE_COUNTER");
    static_assert(dt::DeviceDescriptorType<decltype(Spec)> &&
                      Spec.identity.endpoint_kind == EndpointKind::Counter,
                  "SOLAR_DIAGNOSTIC_HARDWARE_COUNTER_DESCRIPTOR_REQUIRED: Counter requires a "
                  "Counter device descriptor");

    using Base = hardware::Endpoint<Spec>;

    [[nodiscard]] static Result<void, Error> start() noexcept
    {
        return hardware::detail::native_result(counter_start(Base::native_device()),
                                               Operation::Start, Base::path());
    }

    [[nodiscard]] static Result<void, Error> stop() noexcept
    {
        return hardware::detail::native_result(counter_stop(Base::native_device()), Operation::Stop,
                                               Base::path());
    }

    [[nodiscard]] static Result<void, Error> reset() noexcept
    {
        return hardware::detail::native_result(counter_reset(Base::native_device()),
                                               Operation::Configure, Base::path());
    }

    [[nodiscard]] static Result<std::uint32_t, Error> value() noexcept
    {
        std::uint32_t ticks{};
        const auto result = counter_get_value(Base::native_device(), &ticks);
        if (result != 0) {
            return fail<Error>(
                hardware::detail::native_error(result, Operation::Read, Base::path()));
        }
        return ticks;
    }

    [[nodiscard]] static Result<std::uint64_t, Error> value64() noexcept
    {
        std::uint64_t ticks{};
        const auto result = counter_get_value_64(Base::native_device(), &ticks);
        if (result != 0) {
            return fail<Error>(
                hardware::detail::native_error(result, Operation::Read, Base::path()));
        }
        return ticks;
    }

    [[nodiscard]] static std::uint32_t frequency() noexcept
    {
        return counter_get_frequency(Base::native_device());
    }

    [[nodiscard]] static std::uint32_t top() noexcept
    {
        return counter_get_top_value(Base::native_device());
    }

    [[nodiscard]] static bool interrupt_pending() noexcept
    {
        return counter_get_pending_int(Base::native_device()) != 0U;
    }

    template <typename Rep, typename Period>
    [[nodiscard]] static std::uint64_t ticks(std::chrono::duration<Rep, Period> duration) noexcept
    {
        const auto nanoseconds =
            std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
        if (nanoseconds <= 0) {
            return 0;
        }
        return counter_us_to_ticks(Base::native_device(),
                                   static_cast<std::uint64_t>(nanoseconds / 1000));
    }
};

using TopHandler = void (*)() noexcept;

template <typename CounterT> struct Top
{
    inline static std::atomic<TopHandler> handler{};

    [[nodiscard]] static Result<void, Error> set(std::uint32_t ticks, TopHandler callback = nullptr,
                                                 std::uint32_t flags = 0U) noexcept
    {
        handler.store(callback, std::memory_order_release);
        counter_top_cfg configuration{
            .ticks = ticks,
            .callback = callback == nullptr ? nullptr : &trampoline,
            .user_data = nullptr,
            .flags = flags,
        };
        auto result = hardware::detail::native_result(
            counter_set_top_value(CounterT::native_device(), &configuration), Operation::Configure,
            CounterT::path());
        if (!result) {
            handler.store(nullptr, std::memory_order_release);
        }
        return result;
    }

    [[nodiscard]] static std::uint32_t value() noexcept
    {
        return counter_get_top_value(CounterT::native_device());
    }

  private:
    static void trampoline(const device*, void*) noexcept
    {
        if (const auto callback = handler.load(std::memory_order_acquire); callback != nullptr) {
            callback();
        }
    }
};

using AlarmHandler = void (*)(std::uint32_t ticks) noexcept;

template <typename CounterT, std::uint8_t Channel> struct Alarm
{
    inline static std::atomic<AlarmHandler> handler{};

    [[nodiscard]] static Result<void, Error> set(std::uint32_t ticks, bool absolute = false,
                                                 AlarmHandler callback = nullptr) noexcept
    {
        if (Channel >= counter_get_num_of_channels(CounterT::native_device())) {
            return fail<Error>({.status = solar::Status::Invalid,
                                .reason = Reason::InvalidConfiguration,
                                .operation = Operation::Configure,
                                .native = -EINVAL,
                                .endpoint = CounterT::path()});
        }
        handler.store(callback, std::memory_order_release);
        counter_alarm_cfg configuration{
            .callback = callback == nullptr ? nullptr : &trampoline,
            .ticks = ticks,
            .user_data = nullptr,
            .flags = static_cast<std::uint32_t>(absolute ? COUNTER_ALARM_CFG_ABSOLUTE : 0U),
        };
        auto result = hardware::detail::native_result(
            counter_set_channel_alarm(CounterT::native_device(), Channel, &configuration),
            Operation::Configure, CounterT::path());
        if (!result) {
            handler.store(nullptr, std::memory_order_release);
        }
        return result;
    }

    [[nodiscard]] static Result<void, Error> cancel() noexcept
    {
        auto result = hardware::detail::native_result(
            counter_cancel_channel_alarm(CounterT::native_device(), Channel), Operation::Cancel,
            CounterT::path());
        if (result) {
            handler.store(nullptr, std::memory_order_release);
        }
        return result;
    }

  private:
    static void trampoline(const device*, std::uint8_t, std::uint32_t ticks, void*) noexcept
    {
        if (const auto callback = handler.load(std::memory_order_acquire); callback != nullptr) {
            callback(ticks);
        }
    }
};

} // namespace solar::hardware::counter
