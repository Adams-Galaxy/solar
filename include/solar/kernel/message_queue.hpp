#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

#include <zephyr/kernel.h>
#include <zephyr/sys/__assert.h>

#include "solar/core/status.hpp"
#include "solar/kernel/deadline.hpp"
#include "solar/kernel/error.hpp"

namespace solar::kernel
{

/** Non-owning typed access to an initialized Zephyr message queue. */
template <typename Message> class MessageQueueRef
{
    static_assert(std::is_trivially_copyable_v<Message>,
                  "SOLAR_DIAGNOSTIC_MESSAGE_QUEUE_NONTRIVIAL_PAYLOAD: message types must be "
                  "trivially copyable");

  public:
    explicit MessageQueueRef(k_msgq& queue) noexcept : queue_(&queue)
    {
        k_msgq_attrs attributes{};
        k_msgq_get_attrs(queue_, &attributes);
        __ASSERT_NO_MSG(attributes.msg_size == sizeof(Message));
    }

    [[nodiscard]] Result<void> send(const Message& message,
                                    Timeout timeout = Timeout::forever()) const noexcept
    {
        return detail::map_wait(k_msgq_put(queue_, &message, timeout.native_handle()), timeout,
                                Status::Full);
    }

    [[nodiscard]] Result<void> send(const Message& message, const Deadline& deadline) const noexcept
    {
        return send(message, deadline.remaining());
    }

    [[nodiscard]] Result<void> try_send(const Message& message) const noexcept
    {
        return send(message, Timeout::no_wait());
    }

    [[nodiscard]] Result<void> try_send_isr(const Message& message) const noexcept
    {
        return try_send(message);
    }

    [[nodiscard]] Result<void> try_send_front(const Message& message) const noexcept
    {
        return k_msgq_put_front(queue_, &message) == 0
                   ? Result<void>{}
                   : Result<void>{fail<Error>({.status = Status::Full})};
    }

    [[nodiscard]] Result<void> try_send_front_isr(const Message& message) const noexcept
    {
        return try_send_front(message);
    }

    [[nodiscard]] Result<Message> receive(Timeout timeout = Timeout::forever()) const noexcept
    {
        std::array<std::byte, sizeof(Message)> bytes{};
        const auto status = detail::map_wait(
            k_msgq_get(queue_, bytes.data(), timeout.native_handle()), timeout, Status::Empty);
        if (!status) {
            return fail<Error>(status.error());
        }
        return std::bit_cast<Message>(bytes);
    }

    [[nodiscard]] Result<Message> receive(const Deadline& deadline) const noexcept
    {
        return receive(deadline.remaining());
    }

    [[nodiscard]] Result<Message> try_receive() const noexcept
    {
        return receive(Timeout::no_wait());
    }

    [[nodiscard]] Result<Message> try_receive_isr() const noexcept
    {
        return try_receive();
    }

    [[nodiscard]] Result<Message> peek() const noexcept
    {
        std::array<std::byte, sizeof(Message)> bytes{};
        if (k_msgq_peek(queue_, bytes.data()) != 0) {
            return fail<solar::Error>({.status = solar::Status::Empty});
        }
        return std::bit_cast<Message>(bytes);
    }

    [[nodiscard]] Result<Message> peek_at(std::size_t index) const noexcept
    {
        if (index > std::numeric_limits<std::uint32_t>::max()) {
            return fail<solar::Error>({.status = solar::Status::Invalid});
        }
        std::array<std::byte, sizeof(Message)> bytes{};
        if (k_msgq_peek_at(queue_, bytes.data(), static_cast<std::uint32_t>(index)) != 0) {
            return fail<solar::Error>({.status = solar::Status::NotFound});
        }
        return std::bit_cast<Message>(bytes);
    }

    void purge() const noexcept
    {
        k_msgq_purge(queue_);
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return k_msgq_num_used_get(queue_);
    }

    [[nodiscard]] std::size_t available() const noexcept
    {
        return k_msgq_num_free_get(queue_);
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return size() == 0;
    }
    [[nodiscard]] bool full() const noexcept
    {
        return available() == 0;
    }

    [[nodiscard]] constexpr k_msgq* native_handle() const noexcept
    {
        return queue_;
    }

  private:
    struct Unchecked
    {};

    constexpr MessageQueueRef(k_msgq& queue, Unchecked) noexcept : queue_(&queue) {}

    template <typename, std::size_t> friend class MessageQueue;

    k_msgq* queue_;
};

template <typename Message, std::size_t Capacity> class MessageQueue
{
    static_assert(Capacity > 0,
                  "SOLAR_DIAGNOSTIC_MESSAGE_QUEUE_ZERO_CAPACITY: capacity must be non-zero");
    static_assert(
        Capacity <= std::numeric_limits<std::uint32_t>::max(),
        "SOLAR_DIAGNOSTIC_MESSAGE_QUEUE_CAPACITY_OVERFLOW: capacity exceeds Zephyr's limit");
    static_assert(std::is_trivially_copyable_v<Message>,
                  "SOLAR_DIAGNOSTIC_MESSAGE_QUEUE_NONTRIVIAL_PAYLOAD: message types must be "
                  "trivially copyable");

  public:
    using Value = Message;
    static constexpr std::size_t capacity = Capacity;

    MessageQueue() noexcept
    {
        k_msgq_init(&queue_, reinterpret_cast<char*>(storage_.data()), sizeof(Message), Capacity);
    }

    MessageQueue(const MessageQueue&) = delete;
    MessageQueue& operator=(const MessageQueue&) = delete;
    MessageQueue(MessageQueue&&) = delete;
    MessageQueue& operator=(MessageQueue&&) = delete;

    [[nodiscard]] Result<void> send(const Message& message,
                                    Timeout timeout = Timeout::forever()) noexcept
    {
        return ref().send(message, timeout);
    }

    [[nodiscard]] Result<void> send(const Message& message, const Deadline& deadline) noexcept
    {
        return send(message, deadline.remaining());
    }

    [[nodiscard]] Result<void> try_send(const Message& message) noexcept
    {
        return send(message, Timeout::no_wait());
    }

    [[nodiscard]] Result<void> try_send_isr(const Message& message) noexcept
    {
        return send(message, Timeout::no_wait());
    }

    [[nodiscard]] Result<void> try_send_front(const Message& message) noexcept
    {
        return ref().try_send_front(message);
    }

    [[nodiscard]] Result<void> try_send_front_isr(const Message& message) noexcept
    {
        return try_send_front(message);
    }

    [[nodiscard]] Result<Message> receive(Timeout timeout = Timeout::forever()) noexcept
    {
        return ref().receive(timeout);
    }

    [[nodiscard]] Result<Message> receive(const Deadline& deadline) noexcept
    {
        return receive(deadline.remaining());
    }

    [[nodiscard]] Result<Message> try_receive() noexcept
    {
        return receive(Timeout::no_wait());
    }

    [[nodiscard]] Result<Message> try_receive_isr() noexcept
    {
        return receive(Timeout::no_wait());
    }

    [[nodiscard]] Result<Message> peek() noexcept
    {
        return ref().peek();
    }

    [[nodiscard]] Result<Message> peek_at(std::size_t index) noexcept
    {
        return ref().peek_at(index);
    }

    void purge() noexcept
    {
        ref().purge();
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return k_msgq_num_used_get(const_cast<k_msgq*>(&queue_));
    }

    [[nodiscard]] std::size_t available() const noexcept
    {
        return k_msgq_num_free_get(const_cast<k_msgq*>(&queue_));
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return size() == 0;
    }

    [[nodiscard]] bool full() const noexcept
    {
        return size() == Capacity;
    }

    [[nodiscard]] MessageQueueRef<Message> ref() noexcept
    {
        return MessageQueueRef<Message>{queue_, typename MessageQueueRef<Message>::Unchecked{}};
    }

  private:
    alignas(Message) std::array<std::byte, sizeof(Message) * Capacity> storage_{};
    k_msgq queue_{};
};

} // namespace solar::kernel
