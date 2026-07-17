#pragma once

#include <atomic>
#include <cstdint>

#include <zephyr/drivers/gpio.h>

#include "solar/hardware/endpoint.hpp"

namespace solar::hardware::gpio
{

inline constexpr bool available =
#if defined(CONFIG_SOLAR_HARDWARE_GPIO)
    true;
#else
    false;
#endif

enum class Initial : std::uint8_t
{
    Preserve,
    Inactive,
    Active,
    Low,
    High,
};

enum class Trigger : std::uint8_t
{
    Rising,
    Falling,
    BothEdges,
    Low,
    High,
    ToInactive,
    ToActive,
    Inactive,
    Active,
};

struct Event
{
    gpio_port_pins_t pins{};
};

using Handler = void (*)(Event) noexcept;

namespace detail
{

struct Key
{
    const device* port{};
    gpio_pin_t pin{};
    constexpr bool operator==(const Key&) const = default;
};

template <Key EndpointKey> struct CallbackState
{
    inline static gpio_callback callback{};
    inline static std::atomic<Handler> handler{};
    inline static std::atomic<bool> registered{};
    inline static std::atomic<gpio_flags_t> trigger{};
};

[[nodiscard]] constexpr gpio_flags_t trigger_flags(Trigger trigger) noexcept
{
    switch (trigger) {
    case Trigger::Rising:
        return GPIO_INT_EDGE_RISING;
    case Trigger::Falling:
        return GPIO_INT_EDGE_FALLING;
    case Trigger::BothEdges:
        return GPIO_INT_EDGE_BOTH;
    case Trigger::Low:
        return GPIO_INT_LEVEL_LOW;
    case Trigger::High:
        return GPIO_INT_LEVEL_HIGH;
    case Trigger::ToInactive:
        return GPIO_INT_EDGE_TO_INACTIVE;
    case Trigger::ToActive:
        return GPIO_INT_EDGE_TO_ACTIVE;
    case Trigger::Inactive:
        return GPIO_INT_LEVEL_INACTIVE;
    case Trigger::Active:
        return GPIO_INT_LEVEL_ACTIVE;
    }
    return GPIO_INT_DISABLE;
}

[[nodiscard]] constexpr gpio_flags_t initial_flags(Initial initial) noexcept
{
    switch (initial) {
    case Initial::Preserve:
        return GPIO_OUTPUT;
    case Initial::Inactive:
        return GPIO_OUTPUT_INACTIVE;
    case Initial::Active:
        return GPIO_OUTPUT_ACTIVE;
    case Initial::Low:
        return GPIO_OUTPUT_LOW;
    case Initial::High:
        return GPIO_OUTPUT_HIGH;
    }
    return GPIO_OUTPUT;
}

} // namespace detail

template <auto Spec> struct Pin : Endpoint<Spec>
{
    static_assert(available, "SOLAR_DIAGNOSTIC_HARDWARE_GPIO_DISABLED: GPIO wrappers require "
                             "CONFIG_SOLAR_HARDWARE_GPIO");
    static_assert(dt::GpioDescriptorType<decltype(Spec)>,
                  "SOLAR_DIAGNOSTIC_HARDWARE_GPIO_DESCRIPTOR_REQUIRED: GPIO wrapper requires a "
                  "GPIO devicetree descriptor");

    using Base = Endpoint<Spec>;
    inline static constexpr auto key =
        detail::Key{Base::descriptor_value.native.port, Base::descriptor_value.native.pin};

    [[nodiscard]] static Result<void, Error> configure(gpio_flags_t flags) noexcept
    {
        if (auto ready = Base::require_ready(); !ready) {
            return ready;
        }
        return hardware::detail::native_result(
            gpio_pin_configure_dt(&Base::descriptor_value.native, flags), Operation::Configure,
            Base::path());
    }

    [[nodiscard]] static Result<bool, Error> read() noexcept
    {
        const auto value = gpio_pin_get_dt(&Base::descriptor_value.native);
        if (value < 0) {
            return fail<Error>(
                hardware::detail::native_error(value, Operation::Read, Base::path()));
        }
        return value != 0;
    }

    [[nodiscard]] static Result<bool, Error> read_raw() noexcept
    {
        const auto value =
            gpio_pin_get_raw(Base::descriptor_value.native.port, Base::descriptor_value.native.pin);
        if (value < 0) {
            return fail<Error>(
                hardware::detail::native_error(value, Operation::Read, Base::path()));
        }
        return value != 0;
    }

    [[nodiscard]] static Result<void, Error> write(bool active) noexcept
    {
        return hardware::detail::native_result(
            gpio_pin_set_dt(&Base::descriptor_value.native, active), Operation::Write,
            Base::path());
    }

    [[nodiscard]] static Result<void, Error> write_raw(bool high) noexcept
    {
        return hardware::detail::native_result(gpio_pin_set_raw(Base::descriptor_value.native.port,
                                                                Base::descriptor_value.native.pin,
                                                                high),
                                               Operation::Write, Base::path());
    }

    [[nodiscard]] static Result<void, Error> toggle() noexcept
    {
        return hardware::detail::native_result(gpio_pin_toggle_dt(&Base::descriptor_value.native),
                                               Operation::Toggle, Base::path());
    }

    [[nodiscard]] static constexpr gpio_pin_t pin() noexcept
    {
        return Base::descriptor_value.native.pin;
    }

    [[nodiscard]] static constexpr gpio_dt_flags_t devicetree_flags() noexcept
    {
        return Base::descriptor_value.native.dt_flags;
    }
};

template <auto Spec, gpio_flags_t Options = 0> struct Input : Pin<Spec>
{
    [[nodiscard]] static Result<void, Error> configure() noexcept
    {
        return Pin<Spec>::configure(GPIO_INPUT | Options);
    }
};

template <auto Spec, Initial InitialValue = Initial::Inactive, gpio_flags_t Options = 0>
struct Output : Pin<Spec>
{
    [[nodiscard]] static Result<void, Error> configure() noexcept
    {
        return Pin<Spec>::configure(detail::initial_flags(InitialValue) | Options);
    }

    [[nodiscard]] static Result<void, Error> activate() noexcept
    {
        return Pin<Spec>::write(true);
    }

    [[nodiscard]] static Result<void, Error> deactivate() noexcept
    {
        return Pin<Spec>::write(false);
    }
};

template <auto Spec, Trigger DefaultTrigger = Trigger::BothEdges, gpio_flags_t Options = 0>
struct Interrupt : Input<Spec, Options>
{
    static_assert(
#if defined(CONFIG_SOLAR_HARDWARE_GPIO_INTERRUPTS)
        true,
#else
        false,
#endif
        "SOLAR_DIAGNOSTIC_HARDWARE_GPIO_INTERRUPTS_DISABLED: GPIO Interrupt requires "
        "CONFIG_SOLAR_HARDWARE_GPIO_INTERRUPTS");

    using Base = Pin<Spec>;
    using State = detail::CallbackState<Base::key>;

    [[nodiscard]] static Result<void, Error> install(Handler handler) noexcept
    {
        if (handler == nullptr) {
            return fail<Error>({.status = solar::Status::Invalid,
                                .reason = Reason::InvalidConfiguration,
                                .operation = Operation::CallbackInstall,
                                .native = -EINVAL,
                                .endpoint = Base::path()});
        }
        bool expected{};
        if (!State::registered.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            return fail<Error>({.status = solar::Status::Already,
                                .reason = Reason::AlreadyOwned,
                                .operation = Operation::CallbackInstall,
                                .native = -EALREADY,
                                .endpoint = Base::path()});
        }
        State::handler.store(handler, std::memory_order_release);
        gpio_init_callback(&State::callback, &trampoline,
                           static_cast<gpio_port_pins_t>(BIT(Base::pin())));
        const auto result = gpio_add_callback_dt(&Base::descriptor_value.native, &State::callback);
        if (result != 0) {
            State::handler.store(nullptr, std::memory_order_release);
            State::registered.store(false, std::memory_order_release);
            return fail<Error>(
                hardware::detail::native_error(result, Operation::CallbackInstall, Base::path()));
        }
        return {};
    }

    [[nodiscard]] static Result<void, Error> uninstall() noexcept
    {
        if (!State::registered.load(std::memory_order_acquire)) {
            return fail<Error>({.status = solar::Status::NotReady,
                                .reason = Reason::NotReady,
                                .operation = Operation::CallbackRemove,
                                .native = -ENOENT,
                                .endpoint = Base::path()});
        }
        const auto result =
            gpio_remove_callback_dt(&Base::descriptor_value.native, &State::callback);
        if (result != 0) {
            return fail<Error>(
                hardware::detail::native_error(result, Operation::CallbackRemove, Base::path()));
        }
        State::handler.store(nullptr, std::memory_order_release);
        State::registered.store(false, std::memory_order_release);
        return {};
    }

    [[nodiscard]] static Result<void, Error> configure(Trigger trigger = DefaultTrigger) noexcept
    {
        const auto flags = detail::trigger_flags(trigger);
        State::trigger.store(flags, std::memory_order_release);
        return hardware::detail::native_result(
            gpio_pin_interrupt_configure_dt(&Base::descriptor_value.native, flags),
            Operation::InterruptConfigure, Base::path());
    }

    [[nodiscard]] static Result<void, Error> enable() noexcept
    {
        auto flags = State::trigger.load(std::memory_order_acquire);
        if (flags == 0) {
            flags = detail::trigger_flags(DefaultTrigger);
        }
        return hardware::detail::native_result(
            gpio_pin_interrupt_configure_dt(&Base::descriptor_value.native, flags),
            Operation::InterruptConfigure, Base::path());
    }

    [[nodiscard]] static Result<void, Error> disable() noexcept
    {
        return hardware::detail::native_result(
            gpio_pin_interrupt_configure_dt(&Base::descriptor_value.native, GPIO_INT_DISABLE),
            Operation::InterruptConfigure, Base::path());
    }

    [[nodiscard]] static Result<void, Error> start(Handler handler,
                                                   Trigger trigger = DefaultTrigger) noexcept
    {
        if (auto configured = Input<Spec, Options>::configure(); !configured) {
            return configured;
        }
        if (auto installed = install(handler); !installed) {
            return installed;
        }
        if (auto interrupt = configure(trigger); !interrupt) {
            (void)uninstall();
            return interrupt;
        }
        return {};
    }

    [[nodiscard]] static Result<void, Error> stop() noexcept
    {
        auto disabled = disable();
        auto removed = uninstall();
        return disabled ? removed : disabled;
    }

    [[nodiscard]] static Result<bool, Error> pending() noexcept
    {
        const auto result = gpio_get_pending_int(Base::descriptor_value.native.port);
        if (result < 0) {
            return fail<Error>(
                hardware::detail::native_error(result, Operation::Pending, Base::path()));
        }
        return result != 0;
    }

    [[nodiscard]] static bool installed() noexcept
    {
        return State::registered.load(std::memory_order_acquire);
    }

  private:
    static void trampoline(const device*, gpio_callback*, gpio_port_pins_t pins) noexcept
    {
        if (const auto handler = State::handler.load(std::memory_order_acquire)) {
            handler(Event{.pins = pins});
        }
    }
};

template <const device* Device> struct Port
{
    [[nodiscard]] static bool ready() noexcept
    {
        return device_is_ready(Device);
    }

    [[nodiscard]] static constexpr const device* native_handle() noexcept
    {
        return Device;
    }

    [[nodiscard]] static Result<gpio_port_value_t, Error> read_raw() noexcept
    {
        gpio_port_value_t value{};
        const auto result = gpio_port_get_raw(Device, &value);
        if (result != 0) {
            return fail<Error>(hardware::detail::native_error(result, Operation::Read));
        }
        return value;
    }

    [[nodiscard]] static Result<void, Error> write_masked_raw(gpio_port_pins_t mask,
                                                              gpio_port_value_t value) noexcept
    {
        return hardware::detail::native_result(gpio_port_set_masked_raw(Device, mask, value),
                                               Operation::Write);
    }
};

} // namespace solar::hardware::gpio
