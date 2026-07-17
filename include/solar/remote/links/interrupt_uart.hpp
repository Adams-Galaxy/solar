#pragma once

#if !defined(__ZEPHYR__)
#error "solar::remote::links::InterruptUart requires Zephyr"
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>

#include "solar/kernel/spinlock.hpp"
#include "solar/remote/link.hpp"

namespace solar::remote::links
{

template <typename Derived, const device* Device, std::size_t RxCapacity> class InterruptUart
{
    static_assert(RxCapacity > 0);

  public:
    [[nodiscard]] static Result<void, LinkError> open(LinkEventSink sink) noexcept
    {
        if (!device_is_ready(Device)) {
            return fail<LinkError>({.status = solar::Status::NotReady});
        }
        {
            auto guard = lock_.acquire();
            if (opened_) {
                return fail<LinkError>({.status = solar::Status::Already});
            }
            sink_ = sink;
            opened_ = true;
            rx_occupied_ = false;
            rx_size_ = 0;
            tx_active_ = false;
            tx_offset_ = 0;
        }
        const auto status = uart_irq_callback_user_data_set(Device, &interrupt, nullptr);
        if (status != 0) {
            auto guard = lock_.acquire();
            opened_ = false;
            sink_ = {};
            return fail<LinkError>({.status = status_from_errno(status), .native_error = status});
        }
        uart_irq_rx_enable(Device);
        sink.notify(LinkEvent{.kind = LinkEventKind::Connected});
        return {};
    }

    static void close() noexcept
    {
        uart_irq_rx_disable(Device);
        uart_irq_tx_disable(Device);
        (void)uart_irq_callback_user_data_set(Device, nullptr, nullptr);
        auto guard = lock_.acquire();
        opened_ = false;
        rx_occupied_ = false;
        rx_size_ = 0;
        tx_active_ = false;
        tx_bytes_ = {};
        sink_ = {};
    }

    [[nodiscard]] static Result<std::span<const std::byte>, LinkError>
    rx_bytes(LeaseHandle lease) noexcept
    {
        auto guard = lock_.acquire();
        if (!opened_ || !rx_occupied_ || lease != rx_handle_) {
            return fail<LinkError>({.status = solar::Status::NotFound});
        }
        return std::span<const std::byte>{rx_storage_}.first(rx_size_);
    }

    static void release_rx(LeaseHandle lease) noexcept
    {
        bool enable{};
        {
            auto guard = lock_.acquire();
            if (opened_ && rx_occupied_ && lease == rx_handle_) {
                rx_occupied_ = false;
                rx_size_ = 0;
                enable = true;
            }
        }
        if (enable) {
            uart_irq_rx_enable(Device);
        }
    }

    [[nodiscard]] static Result<TxDisposition, LinkError> try_transmit(TxLease lease) noexcept
    {
        {
            auto guard = lock_.acquire();
            if (!opened_) {
                return fail<LinkError>({.status = solar::Status::NotReady});
            }
            if (tx_active_) {
                return TxDisposition::Busy;
            }
            tx_bytes_ = lease.bytes();
            tx_handle_ = lease.handle();
            tx_offset_ = 0;
            tx_active_ = true;
        }
        uart_irq_tx_enable(Device);
        return TxDisposition::Accepted;
    }

  private:
    static void interrupt(const device* device_value, void*) noexcept
    {
        if (uart_irq_update(device_value) < 0) {
            return;
        }

        LinkEventSink rx_sink;
        LinkEvent rx_event;
        bool rx_notify{};
        if (uart_irq_rx_ready(device_value) > 0) {
            while (true) {
                std::uint8_t byte{};
                const auto received = uart_fifo_read(device_value, &byte, 1);
                if (received <= 0) {
                    break;
                }
                auto guard = lock_.acquire();
                if (!opened_ || rx_occupied_) {
                    continue;
                }
                if (rx_size_ == rx_storage_.size()) {
                    rx_size_ = 0;
                }
                rx_storage_[rx_size_++] = static_cast<std::byte>(byte);
                if (byte == 0U) {
                    rx_occupied_ = true;
                    rx_handle_ = {.slot = 0, .generation = ++rx_generation_};
                    rx_sink = sink_;
                    rx_event = {.kind = LinkEventKind::RxReady,
                                .lease = rx_handle_,
                                .size = static_cast<std::uint32_t>(rx_size_)};
                    rx_notify = true;
                    uart_irq_rx_disable(device_value);
                    break;
                }
            }
        }

        LinkEventSink tx_sink;
        LinkEvent tx_event;
        bool tx_notify{};
        if (uart_irq_tx_ready(device_value) > 0) {
            auto guard = lock_.acquire();
            if (tx_active_) {
                const auto remaining = tx_bytes_.subspan(tx_offset_);
                const auto sent = uart_fifo_fill(
                    device_value, reinterpret_cast<const std::uint8_t*>(remaining.data()),
                    static_cast<int>(remaining.size()));
                if (sent > 0) {
                    tx_offset_ += static_cast<std::size_t>(sent);
                }
                if (tx_offset_ == tx_bytes_.size()) {
                    uart_irq_tx_disable(device_value);
                    tx_active_ = false;
                    tx_sink = sink_;
                    tx_event = {.kind = LinkEventKind::TxComplete,
                                .lease = tx_handle_,
                                .size = static_cast<std::uint32_t>(tx_offset_)};
                    tx_bytes_ = {};
                    tx_notify = true;
                }
            }
        }

        if (rx_notify) {
            rx_sink.notify(rx_event);
        }
        if (tx_notify) {
            tx_sink.notify(tx_event);
        }
    }

    inline static kernel::SpinLock lock_{};
    inline static LinkEventSink sink_{};
    inline static std::array<std::byte, RxCapacity> rx_storage_{};
    inline static std::span<const std::byte> tx_bytes_{};
    inline static LeaseHandle rx_handle_{};
    inline static LeaseHandle tx_handle_{};
    inline static std::size_t rx_size_{};
    inline static std::size_t tx_offset_{};
    inline static std::uint16_t rx_generation_{};
    inline static bool opened_{};
    inline static bool rx_occupied_{};
    inline static bool tx_active_{};
};

} // namespace solar::remote::links
