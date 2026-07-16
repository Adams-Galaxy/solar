#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

#include <zephyr/drivers/pwm.h>

#include "solar/hardware/endpoint.hpp"

namespace solar::hardware::pwm
{

inline constexpr bool available =
#if defined(CONFIG_SOLAR_HARDWARE_PWM)
    true;
#else
    false;
#endif

struct DutyCycle
{
    std::uint32_t parts_per_million{};

    [[nodiscard]] static constexpr DutyCycle percent(std::uint32_t value) noexcept
    {
        return DutyCycle{value > UINT32_MAX / 10'000U ? UINT32_MAX : value * 10'000U};
    }
};

struct CaptureSample
{
    std::uint32_t period_cycles{};
    std::uint32_t pulse_cycles{};
};

using CaptureHandler = void (*)(Result<CaptureSample, Error>) noexcept;

namespace detail
{

template <const device* Device, std::uint32_t Channel> struct CaptureState
{
    inline static std::atomic<CaptureHandler> handler{};
};

} // namespace detail

template <auto Spec> struct Output : hardware::Endpoint<Spec>
{
    static_assert(available, "SOLAR_DIAGNOSTIC_HARDWARE_PWM_DISABLED: PWM wrappers require "
                             "CONFIG_SOLAR_HARDWARE_PWM");
    static_assert(dt::PwmDescriptorType<decltype(Spec)>,
                  "SOLAR_DIAGNOSTIC_HARDWARE_PWM_DESCRIPTOR_REQUIRED: PWM Output requires a "
                  "PWM devicetree descriptor");

    using Base = hardware::Endpoint<Spec>;

    [[nodiscard]] static Result<void, Error> set_cycles(std::uint32_t period,
                                                        std::uint32_t pulse) noexcept
    {
        if (pulse > period) {
            return fail(Error{.status = Status::Invalid,
                              .reason = Reason::InvalidConfiguration,
                              .operation = Operation::Configure,
                              .native = -EINVAL,
                              .endpoint = Base::path()});
        }
        return hardware::detail::native_result(
            pwm_set_cycles(Base::native_device(), Base::descriptor_value.native.channel, period,
                           pulse, Base::descriptor_value.native.flags),
            Operation::Write, Base::path());
    }

    template <typename PeriodRep, typename PeriodRatio, typename PulseRep, typename PulseRatio>
    [[nodiscard]] static Result<void, Error>
    set(std::chrono::duration<PeriodRep, PeriodRatio> period,
        std::chrono::duration<PulseRep, PulseRatio> pulse) noexcept
    {
        const auto period_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(period).count();
        const auto pulse_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(pulse).count();
        if (period_ns < 0 || pulse_ns < 0 || pulse_ns > period_ns || period_ns > UINT32_MAX) {
            return fail(Error{.status = Status::Invalid,
                              .reason = Reason::InvalidConfiguration,
                              .operation = Operation::Configure,
                              .native = -EINVAL,
                              .endpoint = Base::path()});
        }
        return hardware::detail::native_result(pwm_set_dt(&Base::descriptor_value.native,
                                                          static_cast<std::uint32_t>(period_ns),
                                                          static_cast<std::uint32_t>(pulse_ns)),
                                               Operation::Write, Base::path());
    }

    [[nodiscard]] static Result<void, Error> set(DutyCycle duty) noexcept
    {
        if (duty.parts_per_million > 1'000'000U) {
            return fail(Error{.status = Status::Invalid,
                              .reason = Reason::InvalidConfiguration,
                              .operation = Operation::Configure,
                              .native = -EINVAL,
                              .endpoint = Base::path()});
        }
        const auto pulse = static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(Base::descriptor_value.native.period) *
             duty.parts_per_million) /
            1'000'000U);
        return hardware::detail::native_result(
            pwm_set_pulse_dt(&Base::descriptor_value.native, pulse), Operation::Write,
            Base::path());
    }

    [[nodiscard]] static Result<void, Error> off() noexcept
    {
        return hardware::detail::native_result(pwm_set_pulse_dt(&Base::descriptor_value.native, 0),
                                               Operation::Disable, Base::path());
    }
};

#if defined(CONFIG_SOLAR_HARDWARE_PWM_CAPTURE)
template <auto Spec> struct Capture : hardware::Endpoint<Spec>
{
    static_assert(dt::PwmDescriptorType<decltype(Spec)>,
                  "SOLAR_DIAGNOSTIC_HARDWARE_PWM_DESCRIPTOR_REQUIRED: PWM Capture requires a "
                  "PWM devicetree descriptor");

    using Base = hardware::Endpoint<Spec>;
    using State =
        detail::CaptureState<Base::native_device(), Base::descriptor_value.native.channel>;
    using Sample = CaptureSample;

    [[nodiscard]] static Result<Sample, Error> capture(pwm_flags_t mode = PWM_CAPTURE_TYPE_BOTH,
                                                       k_timeout_t timeout = K_FOREVER) noexcept
    {
        if (State::handler.load(std::memory_order_acquire) != nullptr) {
            return fail(Error{.status = Status::Busy,
                              .reason = Reason::AlreadyOwned,
                              .operation = Operation::Read,
                              .native = -EBUSY,
                              .endpoint = Base::path()});
        }
        Sample value{};
        const auto result =
            pwm_capture_cycles(Base::native_device(), Base::descriptor_value.native.channel,
                               Base::descriptor_value.native.flags | mode, &value.period_cycles,
                               &value.pulse_cycles, timeout);
        if (result != 0) {
            return fail(hardware::detail::native_error(result, Operation::Read, Base::path()));
        }
        return value;
    }

    [[nodiscard]] static Result<void, Error>
    install(CaptureHandler handler,
            pwm_flags_t mode = PWM_CAPTURE_TYPE_BOTH | PWM_CAPTURE_MODE_CONTINUOUS) noexcept
    {
        if (handler == nullptr) {
            return fail(Error{.status = Status::Invalid,
                              .reason = Reason::InvalidConfiguration,
                              .operation = Operation::CallbackInstall,
                              .native = -EINVAL,
                              .endpoint = Base::path()});
        }
        CaptureHandler expected{};
        if (!State::handler.compare_exchange_strong(expected, handler, std::memory_order_acq_rel)) {
            return fail(Error{.status = Status::Already,
                              .reason = Reason::AlreadyOwned,
                              .operation = Operation::CallbackInstall,
                              .native = -EALREADY,
                              .endpoint = Base::path()});
        }
        const auto result =
            pwm_configure_capture(Base::native_device(), Base::descriptor_value.native.channel,
                                  Base::descriptor_value.native.flags | mode, &trampoline, nullptr);
        if (result != 0) {
            State::handler.store(nullptr, std::memory_order_release);
            return fail(
                hardware::detail::native_error(result, Operation::CallbackInstall, Base::path()));
        }
        return {};
    }

    [[nodiscard]] static Result<void, Error> enable() noexcept
    {
        if (State::handler.load(std::memory_order_acquire) == nullptr) {
            return fail(Error{.status = Status::NotReady,
                              .reason = Reason::NotReady,
                              .operation = Operation::Enable,
                              .native = -ENOENT,
                              .endpoint = Base::path()});
        }
        return hardware::detail::native_result(
            pwm_enable_capture(Base::native_device(), Base::descriptor_value.native.channel),
            Operation::Enable, Base::path());
    }

    [[nodiscard]] static Result<void, Error> disable() noexcept
    {
        return hardware::detail::native_result(
            pwm_disable_capture(Base::native_device(), Base::descriptor_value.native.channel),
            Operation::Disable, Base::path());
    }

    [[nodiscard]] static Result<void, Error> uninstall() noexcept
    {
        if (State::handler.load(std::memory_order_acquire) == nullptr) {
            return {};
        }
        auto result = disable();
        if (result || result.error().reason == Reason::Unsupported) {
            State::handler.store(nullptr, std::memory_order_release);
        }
        return result;
    }

    [[nodiscard]] static bool installed() noexcept
    {
        return State::handler.load(std::memory_order_acquire) != nullptr;
    }

  private:
    static void trampoline(const device*, std::uint32_t, std::uint32_t period, std::uint32_t pulse,
                           int status, void*) noexcept
    {
        const auto callback = State::handler.load(std::memory_order_acquire);
        if (callback == nullptr) {
            return;
        }
        if (status != 0) {
            callback(
                fail(hardware::detail::native_error(status, Operation::Complete, Base::path())));
        } else {
            callback(CaptureSample{.period_cycles = period, .pulse_cycles = pulse});
        }
    }
};
#else
template <auto Spec> struct Capture
{
    static_assert(sizeof(decltype(Spec)) == 0,
                  "SOLAR_DIAGNOSTIC_HARDWARE_PWM_CAPTURE_DISABLED: PWM Capture requires "
                  "CONFIG_SOLAR_HARDWARE_PWM_CAPTURE");
};
#endif

} // namespace solar::hardware::pwm
