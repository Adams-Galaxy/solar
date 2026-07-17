#pragma once

#if !defined(__ZEPHYR__)
#error "solar::remote::links::AsyncUart requires Zephyr"
#endif

#include <array>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <span>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>

#include "solar/kernel/spinlock.hpp"
#include "solar/remote/link.hpp"

namespace solar::remote::links
{

template <typename Derived, const device* Device, std::size_t RxCapacity> class AsyncUart
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
            rx_paused_ = false;
            rx_size_ = 0;
            tx_active_ = false;
            rx_pending_.store(false, std::memory_order_relaxed);
            tx_pending_.store(false, std::memory_order_relaxed);
            fault_pending_.store(false, std::memory_order_relaxed);
        }
        auto status = uart_callback_set(Device, &callback, nullptr);
        if (status == 0) {
            status = uart_rx_enable(Device, rx_driver_storage_.data(), rx_driver_storage_.size(),
                                    SYS_FOREVER_US);
        }
        if (status != 0) {
            auto guard = lock_.acquire();
            opened_ = false;
            sink_ = {};
            return fail<LinkError>({.status = status_from_errno(status), .native_error = status});
        }
        sink.notify(LinkEvent{.kind = LinkEventKind::Connected});
        return {};
    }

    static void close() noexcept
    {
        (void)uart_rx_disable(Device);
        (void)uart_tx_abort(Device);
        auto guard = lock_.acquire();
        opened_ = false;
        rx_occupied_ = false;
        rx_paused_ = false;
        rx_size_ = 0;
        tx_active_ = false;
        tx_bytes_ = {};
        rx_pending_.store(false, std::memory_order_relaxed);
        tx_pending_.store(false, std::memory_order_relaxed);
        fault_pending_.store(false, std::memory_order_relaxed);
        sink_ = {};
    }

    static void poll() noexcept
    {
        if (rx_pending_.exchange(false, std::memory_order_acq_rel)) {
            LinkEventSink sink;
            LinkEvent event;
            {
                auto guard = lock_.acquire();
                sink = sink_;
                event = {.kind = LinkEventKind::RxReady,
                         .lease = rx_handle_,
                         .size = static_cast<std::uint32_t>(rx_size_)};
            }
            sink.notify(event);
        }
        if (tx_pending_.exchange(false, std::memory_order_acq_rel)) {
            LinkEventSink sink;
            LinkEvent event;
            {
                auto guard = lock_.acquire();
                sink = sink_;
                event = {.kind = LinkEventKind::TxComplete,
                         .lease = tx_completed_handle_,
                         .size = static_cast<std::uint32_t>(tx_completed_size_)};
            }
            sink.notify(event);
        }
        if (fault_pending_.exchange(false, std::memory_order_acq_rel)) {
            LinkEventSink sink;
            {
                auto guard = lock_.acquire();
                sink = sink_;
            }
            sink.notify(LinkEvent{.kind = LinkEventKind::Fault,
                                  .status = fault_status_.load(std::memory_order_acquire)});
        }
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
        bool restart{};
        {
            auto guard = lock_.acquire();
            if (opened_ && rx_occupied_ && lease == rx_handle_) {
                rx_occupied_ = false;
                rx_paused_ = false;
                rx_size_ = 0;
                restart = true;
            }
        }
        if (restart) {
            const auto status = uart_rx_enable(Device, rx_driver_storage_.data(),
                                               rx_driver_storage_.size(), SYS_FOREVER_US);
            if (status != 0) {
                signal_fault(status_from_errno(status));
            }
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
            tx_active_ = true;
        }
        const auto status = uart_tx(Device, reinterpret_cast<const std::uint8_t*>(tx_bytes_.data()),
                                    tx_bytes_.size(), SYS_FOREVER_US);
        if (status != 0) {
            auto guard = lock_.acquire();
            tx_active_ = false;
            tx_bytes_ = {};
            return status == -EBUSY ? Result<TxDisposition, LinkError>{TxDisposition::Busy}
                                    : Result<TxDisposition, LinkError>{fail<LinkError>({
                                          .status = status_from_errno(status),
                                          .native_error = status,
                                      })};
        }
        return TxDisposition::Accepted;
    }

  private:
    static void callback(const device*, uart_event* event, void*) noexcept
    {
        switch (event->type) {
        case UART_RX_RDY:
            receive(std::span<const std::uint8_t>{event->data.rx.buf + event->data.rx.offset,
                                                  event->data.rx.len});
            break;
        case UART_RX_STOPPED:
            signal_fault(Status::Error);
            break;
        case UART_RX_DISABLED:
            recover_disabled_rx();
            break;
        case UART_TX_DONE:
            complete_transmit(event->data.tx.len);
            break;
        case UART_TX_ABORTED:
            abort_transmit();
            break;
        default:
            break;
        }
    }

    static void receive(std::span<const std::uint8_t> bytes) noexcept
    {
        bool notify{};
        {
            auto guard = lock_.acquire();
            if (!opened_ || rx_occupied_) {
                return;
            }
            for (const auto byte : bytes) {
                if (rx_size_ == rx_storage_.size()) {
                    rx_size_ = 0;
                }
                rx_storage_[rx_size_++] = static_cast<std::byte>(byte);
                if (byte == 0U) {
                    rx_occupied_ = true;
                    rx_paused_ = true;
                    rx_handle_ = {.slot = 0, .generation = ++rx_generation_};
                    notify = true;
                    break;
                }
            }
        }
        if (notify) {
            (void)uart_rx_disable(Device);
            rx_pending_.store(true, std::memory_order_release);
        }
    }

    static void recover_disabled_rx() noexcept
    {
        bool restart{};
        {
            auto guard = lock_.acquire();
            restart = opened_ && !rx_paused_ && !rx_occupied_;
        }
        if (restart) {
            const auto status = uart_rx_enable(Device, rx_driver_storage_.data(),
                                               rx_driver_storage_.size(), SYS_FOREVER_US);
            if (status != 0) {
                signal_fault(status_from_errno(status));
            }
        }
    }

    static void complete_transmit(std::size_t size) noexcept
    {
        {
            auto guard = lock_.acquire();
            if (!opened_ || !tx_active_) {
                return;
            }
            tx_active_ = false;
            tx_completed_handle_ = tx_handle_;
            tx_completed_size_ = size;
            tx_bytes_ = {};
        }
        tx_pending_.store(true, std::memory_order_release);
    }

    static void abort_transmit() noexcept
    {
        {
            auto guard = lock_.acquire();
            if (!opened_ || !tx_active_) {
                return;
            }
            tx_active_ = false;
            tx_bytes_ = {};
        }
        signal_fault(Status::Error);
    }

    static void signal_fault(Status status) noexcept
    {
        fault_status_.store(status, std::memory_order_release);
        fault_pending_.store(true, std::memory_order_release);
    }

    inline static kernel::SpinLock lock_{};
    inline static LinkEventSink sink_{};
    inline static std::array<std::uint8_t, RxCapacity> rx_driver_storage_{};
    inline static std::array<std::byte, RxCapacity> rx_storage_{};
    inline static std::span<const std::byte> tx_bytes_{};
    inline static LeaseHandle rx_handle_{};
    inline static LeaseHandle tx_handle_{};
    inline static LeaseHandle tx_completed_handle_{};
    inline static std::size_t rx_size_{};
    inline static std::size_t tx_completed_size_{};
    inline static std::uint16_t rx_generation_{};
    inline static bool opened_{};
    inline static bool rx_occupied_{};
    inline static bool rx_paused_{};
    inline static bool tx_active_{};
    inline static std::atomic_bool rx_pending_{};
    inline static std::atomic_bool tx_pending_{};
    inline static std::atomic_bool fault_pending_{};
    inline static std::atomic<Status> fault_status_{Status::Error};
};

} // namespace solar::remote::links
