#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

#include <zephyr/drivers/uart.h>

#include "solar/hardware/endpoint.hpp"

namespace solar::hardware::uart
{

inline constexpr bool available =
#if defined(CONFIG_SOLAR_HARDWARE_UART)
    true;
#else
    false;
#endif

template <auto Spec> struct Port : hardware::Endpoint<Spec>
{
    static_assert(available, "SOLAR_DIAGNOSTIC_HARDWARE_UART_DISABLED: UART wrappers require "
                             "CONFIG_SOLAR_HARDWARE_UART");
    static_assert(dt::DeviceDescriptorType<decltype(Spec)> &&
                      Spec.identity.endpoint_kind == EndpointKind::Uart,
                  "SOLAR_DIAGNOSTIC_HARDWARE_UART_DESCRIPTOR_REQUIRED: UART Port requires a "
                  "UART device descriptor");

    using Base = hardware::Endpoint<Spec>;

    [[nodiscard]] static Result<uart_config, Error> configuration() noexcept
    {
        uart_config value{};
        const auto result = uart_config_get(Base::native_device(), &value);
        if (result != 0) {
            return fail(hardware::detail::native_error(result, Operation::Read, Base::path()));
        }
        return value;
    }

    [[nodiscard]] static Result<void, Error> configure(const uart_config& value) noexcept
    {
        return hardware::detail::native_result(uart_configure(Base::native_device(), &value),
                                               Operation::Configure, Base::path());
    }

    [[nodiscard]] static Result<std::uint32_t, Error> errors() noexcept
    {
        const auto result = uart_err_check(Base::native_device());
        if (result < 0) {
            return fail(hardware::detail::native_error(result, Operation::Read, Base::path()));
        }
        return static_cast<std::uint32_t>(result);
    }

    [[nodiscard]] static Result<void, Error> set_line(std::uint32_t control,
                                                      std::uint32_t value) noexcept
    {
        return hardware::detail::native_result(
            uart_line_ctrl_set(Base::native_device(), control, value), Operation::Configure,
            Base::path());
    }

    [[nodiscard]] static Result<std::uint32_t, Error> line(std::uint32_t control) noexcept
    {
        std::uint32_t value{};
        const auto result = uart_line_ctrl_get(Base::native_device(), control, &value);
        if (result != 0) {
            return fail(hardware::detail::native_error(result, Operation::Read, Base::path()));
        }
        return value;
    }
};

template <auto Spec> struct Polling : Port<Spec>
{
    using Base = Port<Spec>;

    [[nodiscard]] static Result<std::optional<std::uint8_t>, Error> read() noexcept
    {
        unsigned char value{};
        const auto result = uart_poll_in(Base::native_device(), &value);
        if (result == -1) {
            return std::optional<std::uint8_t>{};
        }
        if (result != 0) {
            return fail(hardware::detail::native_error(result, Operation::Read, Base::path()));
        }
        return std::optional<std::uint8_t>{value};
    }

    static void write(std::uint8_t value) noexcept
    {
        uart_poll_out(Base::native_device(), value);
    }

    static void write(std::span<const std::byte> bytes) noexcept
    {
        for (const auto value : bytes) {
            write(std::to_integer<std::uint8_t>(value));
        }
    }
};

using InterruptHandler = void (*)() noexcept;
using AsyncHandler = void (*)(const uart_event&) noexcept;

namespace detail
{

enum class CallbackRole : std::uint8_t
{
    None,
    Interrupt,
    Async,
};

template <const device* Device> struct CallbackState
{
    inline static std::atomic<CallbackRole> owner{CallbackRole::None};
    inline static std::atomic<InterruptHandler> interrupt_handler{};
    inline static std::atomic<AsyncHandler> async_handler{};
};

} // namespace detail

#if defined(CONFIG_SOLAR_HARDWARE_UART_INTERRUPT)
template <auto Spec> struct InterruptDriven : Port<Spec>
{
    using Base = Port<Spec>;
    using State = detail::CallbackState<Base::native_device()>;

    [[nodiscard]] static Result<void, Error> install(InterruptHandler handler) noexcept
    {
        auto expected = detail::CallbackRole::None;
        if (handler == nullptr ||
            !State::owner.compare_exchange_strong(expected, detail::CallbackRole::Interrupt,
                                                  std::memory_order_acq_rel)) {
            return fail(Error{.status = handler == nullptr ? Status::Invalid : Status::Already,
                              .reason = handler == nullptr ? Reason::InvalidConfiguration
                                                           : Reason::AlreadyOwned,
                              .operation = Operation::CallbackInstall,
                              .native = handler == nullptr ? -EINVAL : -EALREADY,
                              .endpoint = Base::path()});
        }
        State::interrupt_handler.store(handler, std::memory_order_release);
        const auto result =
            uart_irq_callback_user_data_set(Base::native_device(), &trampoline, nullptr);
        if (result != 0) {
            State::interrupt_handler.store(nullptr, std::memory_order_release);
            State::owner.store(detail::CallbackRole::None, std::memory_order_release);
            return fail(
                hardware::detail::native_error(result, Operation::CallbackInstall, Base::path()));
        }
        return {};
    }

    [[nodiscard]] static Result<void, Error> uninstall() noexcept
    {
        if (State::owner.load(std::memory_order_acquire) != detail::CallbackRole::Interrupt) {
            return fail(Error{.status = Status::NotReady,
                              .reason = Reason::NotReady,
                              .operation = Operation::CallbackRemove,
                              .native = -ENOENT,
                              .endpoint = Base::path()});
        }
        uart_irq_tx_disable(Base::native_device());
        uart_irq_rx_disable(Base::native_device());
        const auto result =
            uart_irq_callback_user_data_set(Base::native_device(), nullptr, nullptr);
        if (result != 0) {
            return fail(
                hardware::detail::native_error(result, Operation::CallbackRemove, Base::path()));
        }
        State::interrupt_handler.store(nullptr, std::memory_order_release);
        auto expected = detail::CallbackRole::Interrupt;
        (void)State::owner.compare_exchange_strong(expected, detail::CallbackRole::None,
                                                   std::memory_order_acq_rel);
        return {};
    }

    [[nodiscard]] static bool installed() noexcept
    {
        return State::owner.load(std::memory_order_acquire) == detail::CallbackRole::Interrupt;
    }

    [[nodiscard]] static Result<std::size_t, Error>
    write_fifo(std::span<const std::byte> bytes) noexcept
    {
        const auto result = uart_fifo_fill(Base::native_device(),
                                           reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                           static_cast<int>(bytes.size()));
        if (result < 0) {
            return fail(hardware::detail::native_error(result, Operation::Write, Base::path()));
        }
        return static_cast<std::size_t>(result);
    }

    [[nodiscard]] static Result<std::size_t, Error> read_fifo(std::span<std::byte> bytes) noexcept
    {
        const auto result =
            uart_fifo_read(Base::native_device(), reinterpret_cast<std::uint8_t*>(bytes.data()),
                           static_cast<int>(bytes.size()));
        if (result < 0) {
            return fail(hardware::detail::native_error(result, Operation::Read, Base::path()));
        }
        return static_cast<std::size_t>(result);
    }

    static void enable_receive() noexcept
    {
        uart_irq_rx_enable(Base::native_device());
    }
    static void disable_receive() noexcept
    {
        uart_irq_rx_disable(Base::native_device());
    }
    static void enable_transmit() noexcept
    {
        uart_irq_tx_enable(Base::native_device());
    }
    static void disable_transmit() noexcept
    {
        uart_irq_tx_disable(Base::native_device());
    }

    [[nodiscard]] static bool receive_ready() noexcept
    {
        return uart_irq_rx_ready(Base::native_device()) > 0;
    }

    [[nodiscard]] static bool transmit_ready() noexcept
    {
        return uart_irq_tx_ready(Base::native_device()) > 0;
    }

    [[nodiscard]] static Result<bool, Error> update() noexcept
    {
        const auto result = uart_irq_update(Base::native_device());
        if (result < 0) {
            return fail(hardware::detail::native_error(result, Operation::Read, Base::path()));
        }
        return result != 0;
    }

  private:
    static void trampoline(const device*, void*) noexcept
    {
        if (const auto handler = State::interrupt_handler.load(std::memory_order_acquire);
            handler != nullptr) {
            handler();
        }
    }
};
#else
template <auto Spec> struct InterruptDriven
{
    static_assert(sizeof(decltype(Spec)) == 0,
                  "SOLAR_DIAGNOSTIC_HARDWARE_UART_INTERRUPT_DISABLED: InterruptDriven UART "
                  "requires CONFIG_SOLAR_HARDWARE_UART_INTERRUPT");
};
#endif

namespace detail
{

[[nodiscard]] constexpr int32_t timeout_us(std::chrono::microseconds timeout) noexcept
{
    if (timeout == std::chrono::microseconds::max()) {
        return SYS_FOREVER_US;
    }
    if (timeout.count() > std::numeric_limits<int32_t>::max()) {
        return std::numeric_limits<int32_t>::max();
    }
    return static_cast<int32_t>(timeout.count());
}

} // namespace detail

#if defined(CONFIG_SOLAR_HARDWARE_UART_ASYNC)
template <auto Spec> struct Async : Port<Spec>
{
    using Base = Port<Spec>;
    using State = detail::CallbackState<Base::native_device()>;

    [[nodiscard]] static Result<void, Error> install(AsyncHandler handler) noexcept
    {
        auto expected = detail::CallbackRole::None;
        if (handler == nullptr ||
            !State::owner.compare_exchange_strong(expected, detail::CallbackRole::Async,
                                                  std::memory_order_acq_rel)) {
            return fail(Error{.status = handler == nullptr ? Status::Invalid : Status::Already,
                              .reason = handler == nullptr ? Reason::InvalidConfiguration
                                                           : Reason::AlreadyOwned,
                              .operation = Operation::CallbackInstall,
                              .native = handler == nullptr ? -EINVAL : -EALREADY,
                              .endpoint = Base::path()});
        }
        State::async_handler.store(handler, std::memory_order_release);
        const auto result = uart_callback_set(Base::native_device(), &trampoline, nullptr);
        if (result != 0) {
            State::async_handler.store(nullptr, std::memory_order_release);
            State::owner.store(detail::CallbackRole::None, std::memory_order_release);
            return fail(
                hardware::detail::native_error(result, Operation::CallbackInstall, Base::path()));
        }
        return {};
    }

    [[nodiscard]] static Result<void, Error> uninstall() noexcept
    {
        if (State::owner.load(std::memory_order_acquire) != detail::CallbackRole::Async) {
            return fail(Error{.status = Status::NotReady,
                              .reason = Reason::NotReady,
                              .operation = Operation::CallbackRemove,
                              .native = -ENOENT,
                              .endpoint = Base::path()});
        }
        const auto result = uart_callback_set(Base::native_device(), nullptr, nullptr);
        if (result != 0) {
            return fail(
                hardware::detail::native_error(result, Operation::CallbackRemove, Base::path()));
        }
        State::async_handler.store(nullptr, std::memory_order_release);
        auto expected = detail::CallbackRole::Async;
        (void)State::owner.compare_exchange_strong(expected, detail::CallbackRole::None,
                                                   std::memory_order_acq_rel);
        return {};
    }

    [[nodiscard]] static bool installed() noexcept
    {
        return State::owner.load(std::memory_order_acquire) == detail::CallbackRole::Async;
    }

    [[nodiscard]] static Result<void, Error>
    transmit(std::span<const std::byte> bytes,
             std::chrono::microseconds timeout = std::chrono::microseconds::max()) noexcept
    {
        if (timeout != std::chrono::microseconds::max() && timeout.count() < 0) {
            return fail(Error{.status = Status::Invalid,
                              .reason = Reason::InvalidConfiguration,
                              .operation = Operation::Submit,
                              .native = -EINVAL,
                              .endpoint = Base::path()});
        }
        return hardware::detail::native_result(
            uart_tx(Base::native_device(), reinterpret_cast<const std::uint8_t*>(bytes.data()),
                    bytes.size(), detail::timeout_us(timeout)),
            Operation::Submit, Base::path());
    }

    [[nodiscard]] static Result<void, Error> abort_transmit() noexcept
    {
        return hardware::detail::native_result(uart_tx_abort(Base::native_device()),
                                               Operation::Abort, Base::path());
    }

    [[nodiscard]] static Result<void, Error>
    receive(std::span<std::byte> buffer,
            std::chrono::microseconds timeout = std::chrono::microseconds::max()) noexcept
    {
        if (timeout != std::chrono::microseconds::max() && timeout.count() < 0) {
            return fail(Error{.status = Status::Invalid,
                              .reason = Reason::InvalidConfiguration,
                              .operation = Operation::Submit,
                              .native = -EINVAL,
                              .endpoint = Base::path()});
        }
        return hardware::detail::native_result(
            uart_rx_enable(Base::native_device(), reinterpret_cast<std::uint8_t*>(buffer.data()),
                           buffer.size(), detail::timeout_us(timeout)),
            Operation::Submit, Base::path());
    }

    [[nodiscard]] static Result<void, Error> provide(std::span<std::byte> buffer) noexcept
    {
        return hardware::detail::native_result(
            uart_rx_buf_rsp(Base::native_device(), reinterpret_cast<std::uint8_t*>(buffer.data()),
                            buffer.size()),
            Operation::Submit, Base::path());
    }

    [[nodiscard]] static Result<void, Error> disable_receive() noexcept
    {
        return hardware::detail::native_result(uart_rx_disable(Base::native_device()),
                                               Operation::Disable, Base::path());
    }

  private:
    static void trampoline(const device*, uart_event* event, void*) noexcept
    {
        if (event == nullptr) {
            return;
        }
        if (const auto handler = State::async_handler.load(std::memory_order_acquire);
            handler != nullptr) {
            handler(*event);
        }
    }
};
#else
template <auto Spec> struct Async
{
    static_assert(sizeof(decltype(Spec)) == 0,
                  "SOLAR_DIAGNOSTIC_HARDWARE_UART_ASYNC_DISABLED: Async UART requires "
                  "CONFIG_SOLAR_HARDWARE_UART_ASYNC");
};
#endif

} // namespace solar::hardware::uart
