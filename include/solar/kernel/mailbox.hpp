#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

#include <zephyr/kernel.h>
#include <zephyr/sys/__assert.h>

#include "solar/core/status.hpp"
#include "solar/kernel/deadline.hpp"
#include "solar/kernel/error.hpp"
#include "solar/kernel/interrupt.hpp"
#include "solar/kernel/semaphore.hpp"

namespace solar::kernel
{

/** Select any sender or receiver when matching a mailbox message. */
inline constexpr k_tid_t any_mailbox_thread = static_cast<k_tid_t>(K_ANY);

struct MailboxSendOptions
{
    std::uint32_t info{};
    k_tid_t target{any_mailbox_thread};
};

struct MailboxReceiveOptions
{
    /** Value returned to the sender after this receive matches. */
    std::uint32_t reply_info{};
    k_tid_t source{any_mailbox_thread};
};

/** Metadata negotiated by a Zephyr mailbox rendezvous. */
struct MailboxReceipt
{
    std::uint32_t info{};
    std::size_t size{};
    k_tid_t peer{any_mailbox_thread};
};

struct MailboxSendReceipt
{
    /** Receiver-provided information value. */
    std::uint32_t reply_info{};
    /** Number of payload bytes accepted by the receiver. */
    std::size_t accepted_size{};
    k_tid_t receiver{any_mailbox_thread};
};

class MailboxRef;

inline constexpr bool mailbox_async_available = CONFIG_NUM_MBOX_ASYNC_MSGS > 0;

/**
 * Stable storage for a mailbox receive whose data copy is intentionally deferred.
 *
 * The matching sender remains blocked until retrieve() or discard() is called. The
 * object must therefore remain at the same address and must not be destroyed while
 * a payload is pending.
 */
class DeferredMailboxReceive
{
  public:
    DeferredMailboxReceive() = default;
    DeferredMailboxReceive(const DeferredMailboxReceive&) = delete;
    DeferredMailboxReceive& operator=(const DeferredMailboxReceive&) = delete;
    DeferredMailboxReceive(DeferredMailboxReceive&&) = delete;
    DeferredMailboxReceive& operator=(DeferredMailboxReceive&&) = delete;

    ~DeferredMailboxReceive()
    {
        __ASSERT_NO_MSG(!pending_);
    }

    [[nodiscard]] bool pending() const noexcept
    {
        return pending_;
    }

    [[nodiscard]] MailboxReceipt receipt() const noexcept
    {
        return receipt_;
    }

    /** Copy the pending bytes and release the sender. A short buffer is rejected. */
    [[nodiscard]] Result<MailboxReceipt> retrieve(std::span<std::byte> destination) noexcept
    {
        if (!pending_) {
            return fail<Error>({.status = Status::NotReady});
        }
        if (destination.size() < receipt_.size) {
            return fail<Error>({.status = Status::NoBuffer});
        }
        k_mbox_data_get(&message_, destination.data());
        pending_ = false;
        return receipt_;
    }

    template <typename Payload>
        requires std::is_trivially_copyable_v<Payload>
    [[nodiscard]] Result<MailboxReceipt> retrieve(Payload& destination) noexcept
    {
        return retrieve(std::as_writable_bytes(std::span{&destination, 1}));
    }

    /** Discard the pending bytes and release the sender. */
    [[nodiscard]] Result<void> discard() noexcept
    {
        if (!pending_) {
            return fail<Error>({.status = Status::NotReady});
        }
        k_mbox_data_get(&message_, nullptr);
        pending_ = false;
        return {};
    }

  private:
    friend class MailboxRef;

    k_mbox_msg message_{};
    MailboxReceipt receipt_{};
    bool pending_{};
};

#if CONFIG_NUM_MBOX_ASYNC_MSGS > 0
/**
 * Owns an asynchronous mailbox payload and its completion signal.
 *
 * The payload cannot be replaced while in flight. wait() proves that the receiver
 * has copied or discarded it before the owner may be reused or destroyed.
 */
template <typename Payload> class AsyncMailboxSend
{
    static_assert(std::is_trivially_copyable_v<Payload>,
                  "SOLAR_DIAGNOSTIC_MAILBOX_NONTRIVIAL_PAYLOAD: mailbox payloads must be "
                  "trivially copyable");

  public:
    explicit AsyncMailboxSend(Payload payload, MailboxSendOptions options = {}) noexcept
        : payload_(payload), options_(options)
    {}

    AsyncMailboxSend(const AsyncMailboxSend&) = delete;
    AsyncMailboxSend& operator=(const AsyncMailboxSend&) = delete;
    AsyncMailboxSend(AsyncMailboxSend&&) = delete;
    AsyncMailboxSend& operator=(AsyncMailboxSend&&) = delete;

    ~AsyncMailboxSend()
    {
        __ASSERT_NO_MSG(!in_flight_);
    }

    [[nodiscard]] bool in_flight() const noexcept
    {
        return in_flight_;
    }

    [[nodiscard]] Result<void> wait(Timeout timeout = Timeout::forever()) noexcept
    {
        if (!in_flight_) {
            return fail<Error>({.status = Status::NotReady});
        }
        const auto result = completion_.take(timeout);
        if (result) {
            in_flight_ = false;
        }
        return result;
    }

    [[nodiscard]] Result<void> wait(const Deadline& deadline) noexcept
    {
        return wait(deadline.remaining());
    }

    [[nodiscard]] Result<void> try_complete() noexcept
    {
        return wait(Timeout::no_wait());
    }

    [[nodiscard]] Result<Payload> payload() const noexcept
    {
        if (in_flight_) {
            return fail<Error>({.status = Status::Busy});
        }
        return payload_;
    }

    [[nodiscard]] Result<void> reset(Payload payload, MailboxSendOptions options = {}) noexcept
    {
        if (in_flight_) {
            return fail<Error>({.status = Status::Busy});
        }
        payload_ = payload;
        options_ = options;
        completion_.reset();
        return {};
    }

  private:
    friend class MailboxRef;

    Payload payload_;
    MailboxSendOptions options_;
    BinarySemaphore completion_;
    bool in_flight_{};
};
#endif

/** Non-owning access to an initialized Zephyr mailbox. */
class MailboxRef
{
  public:
    explicit constexpr MailboxRef(k_mbox& mailbox) noexcept : mailbox_(&mailbox) {}

    [[nodiscard]] Result<MailboxSendReceipt>
    send(std::span<const std::byte> payload, MailboxSendOptions options = {},
         Timeout timeout = Timeout::forever()) const noexcept
    {
        if (in_isr()) {
            return fail<Error>({.status = Status::Invalid});
        }
        k_mbox_msg message{.size = payload.size(),
                           .info = options.info,
                           .tx_data = const_cast<std::byte*>(payload.data()),
                           .rx_source_thread = nullptr,
                           .tx_target_thread = options.target};
        const auto result = detail::map_wait(
            k_mbox_put(mailbox_, &message, timeout.native_handle()), timeout, Status::WouldBlock);
        if (!result) {
            return fail<Error>(result.error());
        }
        return MailboxSendReceipt{.reply_info = message.info,
                                  .accepted_size = message.size,
                                  .receiver = message.tx_target_thread};
    }

    [[nodiscard]] Result<MailboxSendReceipt> send(std::span<const std::byte> payload,
                                                  MailboxSendOptions options,
                                                  const Deadline& deadline) const noexcept
    {
        return send(payload, options, deadline.remaining());
    }

    template <typename Payload>
        requires std::is_trivially_copyable_v<Payload>
    [[nodiscard]] Result<MailboxSendReceipt>
    send(const Payload& payload, MailboxSendOptions options = {},
         Timeout timeout = Timeout::forever()) const noexcept
    {
        return send(std::as_bytes(std::span{&payload, 1}), options, timeout);
    }

    template <typename Payload>
        requires std::is_trivially_copyable_v<Payload>
    [[nodiscard]] Result<MailboxSendReceipt> send(const Payload& payload,
                                                  MailboxSendOptions options,
                                                  const Deadline& deadline) const noexcept
    {
        return send(std::as_bytes(std::span{&payload, 1}), options, deadline.remaining());
    }

    [[nodiscard]] Result<MailboxSendReceipt>
    send(MailboxSendOptions options = {}, Timeout timeout = Timeout::forever()) const noexcept
    {
        return send(std::span<const std::byte>{}, options, timeout);
    }

    [[nodiscard]] Result<MailboxSendReceipt> send(MailboxSendOptions options,
                                                  const Deadline& deadline) const noexcept
    {
        return send(std::span<const std::byte>{}, options, deadline.remaining());
    }

    [[nodiscard]] Result<MailboxReceipt>
    receive(std::span<std::byte> destination, MailboxReceiveOptions options = {},
            Timeout timeout = Timeout::forever()) const noexcept
    {
        if (in_isr()) {
            return fail<Error>({.status = Status::Invalid});
        }
        k_mbox_msg message{.size = destination.size(),
                           .info = options.reply_info,
                           .tx_data = nullptr,
                           .rx_source_thread = options.source,
                           .tx_target_thread = nullptr};
        const auto result = detail::map_wait(
            k_mbox_get(mailbox_, &message, destination.data(), timeout.native_handle()), timeout,
            Status::WouldBlock);
        if (!result) {
            return fail<Error>(result.error());
        }
        return MailboxReceipt{
            .info = message.info, .size = message.size, .peer = message.rx_source_thread};
    }

    [[nodiscard]] Result<MailboxReceipt> receive(std::span<std::byte> destination,
                                                 MailboxReceiveOptions options,
                                                 const Deadline& deadline) const noexcept
    {
        return receive(destination, options, deadline.remaining());
    }

    template <typename Payload>
        requires std::is_trivially_copyable_v<Payload>
    [[nodiscard]] Result<MailboxReceipt>
    receive(Payload& destination, MailboxReceiveOptions options = {},
            Timeout timeout = Timeout::forever()) const noexcept
    {
        return receive(std::as_writable_bytes(std::span{&destination, 1}), options, timeout);
    }

    template <typename Payload>
        requires std::is_trivially_copyable_v<Payload>
    [[nodiscard]] Result<MailboxReceipt> receive(Payload& destination,
                                                 MailboxReceiveOptions options,
                                                 const Deadline& deadline) const noexcept
    {
        return receive(std::as_writable_bytes(std::span{&destination, 1}), options,
                       deadline.remaining());
    }

    [[nodiscard]] Result<MailboxReceipt>
    receive_deferred(DeferredMailboxReceive& deferred, std::size_t maximum_size,
                     MailboxReceiveOptions options = {},
                     Timeout timeout = Timeout::forever()) const noexcept
    {
        if (in_isr()) {
            return fail<Error>({.status = Status::Invalid});
        }
        if (deferred.pending_) {
            return fail<Error>({.status = Status::Busy});
        }
        deferred.message_ = {.size = maximum_size,
                             .info = options.reply_info,
                             .tx_data = nullptr,
                             .rx_source_thread = options.source,
                             .tx_target_thread = nullptr};
        const auto result = detail::map_wait(
            k_mbox_get(mailbox_, &deferred.message_, nullptr, timeout.native_handle()), timeout,
            Status::WouldBlock);
        if (!result) {
            return fail<Error>(result.error());
        }
        deferred.receipt_ = {.info = deferred.message_.info,
                             .size = deferred.message_.size,
                             .peer = deferred.message_.rx_source_thread};
        // Zephyr disposes metadata-only messages during k_mbox_get().
        deferred.pending_ = deferred.message_.size > 0;
        return deferred.receipt_;
    }

    [[nodiscard]] Result<MailboxReceipt> receive_deferred(DeferredMailboxReceive& deferred,
                                                          std::size_t maximum_size,
                                                          MailboxReceiveOptions options,
                                                          const Deadline& deadline) const noexcept
    {
        return receive_deferred(deferred, maximum_size, options, deadline.remaining());
    }

#if CONFIG_NUM_MBOX_ASYNC_MSGS > 0
    template <typename Payload>
    [[nodiscard]] Result<void> send_async(AsyncMailboxSend<Payload>& message) const noexcept
    {
        if (in_isr()) {
            return fail<Error>({.status = Status::Invalid});
        }
        if (message.in_flight_) {
            return fail<Error>({.status = Status::Busy});
        }
        message.completion_.reset();
        k_mbox_msg native{.size = sizeof(Payload),
                          .info = message.options_.info,
                          .tx_data = &message.payload_,
                          .rx_source_thread = nullptr,
                          .tx_target_thread = message.options_.target};
        message.in_flight_ = true;
        k_mbox_async_put(mailbox_, &native, message.completion_.ref().native_handle());
        return {};
    }
#endif

    [[nodiscard]] constexpr k_mbox* native_handle() const noexcept
    {
        return mailbox_;
    }

  private:
    k_mbox* mailbox_;
};

/** Address-stable owner of a Zephyr rendezvous mailbox. */
class Mailbox
{
  public:
    Mailbox() noexcept
    {
        k_mbox_init(&mailbox_);
    }

    Mailbox(const Mailbox&) = delete;
    Mailbox& operator=(const Mailbox&) = delete;
    Mailbox(Mailbox&&) = delete;
    Mailbox& operator=(Mailbox&&) = delete;

    [[nodiscard]] MailboxRef ref() noexcept
    {
        return MailboxRef{mailbox_};
    }

    [[nodiscard]] Result<MailboxSendReceipt> send(std::span<const std::byte> payload,
                                                  MailboxSendOptions options = {},
                                                  Timeout timeout = Timeout::forever()) noexcept
    {
        return ref().send(payload, options, timeout);
    }

    [[nodiscard]] Result<MailboxSendReceipt> send(std::span<const std::byte> payload,
                                                  MailboxSendOptions options,
                                                  const Deadline& deadline) noexcept
    {
        return ref().send(payload, options, deadline);
    }

    template <typename Payload>
        requires std::is_trivially_copyable_v<Payload>
    [[nodiscard]] Result<MailboxSendReceipt> send(const Payload& payload,
                                                  MailboxSendOptions options = {},
                                                  Timeout timeout = Timeout::forever()) noexcept
    {
        return ref().send(payload, options, timeout);
    }

    template <typename Payload>
        requires std::is_trivially_copyable_v<Payload>
    [[nodiscard]] Result<MailboxSendReceipt>
    send(const Payload& payload, MailboxSendOptions options, const Deadline& deadline) noexcept
    {
        return ref().send(payload, options, deadline);
    }

    [[nodiscard]] Result<MailboxSendReceipt> send(MailboxSendOptions options = {},
                                                  Timeout timeout = Timeout::forever()) noexcept
    {
        return ref().send(options, timeout);
    }

    [[nodiscard]] Result<MailboxSendReceipt> send(MailboxSendOptions options,
                                                  const Deadline& deadline) noexcept
    {
        return ref().send(options, deadline);
    }

    [[nodiscard]] Result<MailboxReceipt> receive(std::span<std::byte> destination,
                                                 MailboxReceiveOptions options = {},
                                                 Timeout timeout = Timeout::forever()) noexcept
    {
        return ref().receive(destination, options, timeout);
    }

    [[nodiscard]] Result<MailboxReceipt> receive(std::span<std::byte> destination,
                                                 MailboxReceiveOptions options,
                                                 const Deadline& deadline) noexcept
    {
        return ref().receive(destination, options, deadline);
    }

    template <typename Payload>
        requires std::is_trivially_copyable_v<Payload>
    [[nodiscard]] Result<MailboxReceipt> receive(Payload& destination,
                                                 MailboxReceiveOptions options = {},
                                                 Timeout timeout = Timeout::forever()) noexcept
    {
        return ref().receive(destination, options, timeout);
    }

    template <typename Payload>
        requires std::is_trivially_copyable_v<Payload>
    [[nodiscard]] Result<MailboxReceipt>
    receive(Payload& destination, MailboxReceiveOptions options, const Deadline& deadline) noexcept
    {
        return ref().receive(destination, options, deadline);
    }

    [[nodiscard]] Result<MailboxReceipt>
    receive_deferred(DeferredMailboxReceive& deferred, std::size_t maximum_size,
                     MailboxReceiveOptions options = {},
                     Timeout timeout = Timeout::forever()) noexcept
    {
        return ref().receive_deferred(deferred, maximum_size, options, timeout);
    }

    [[nodiscard]] Result<MailboxReceipt> receive_deferred(DeferredMailboxReceive& deferred,
                                                          std::size_t maximum_size,
                                                          MailboxReceiveOptions options,
                                                          const Deadline& deadline) noexcept
    {
        return ref().receive_deferred(deferred, maximum_size, options, deadline);
    }

#if CONFIG_NUM_MBOX_ASYNC_MSGS > 0
    template <typename Payload>
    [[nodiscard]] Result<void> send_async(AsyncMailboxSend<Payload>& message) noexcept
    {
        return ref().send_async(message);
    }
#endif

  private:
    k_mbox mailbox_{};
};

} // namespace solar::kernel
