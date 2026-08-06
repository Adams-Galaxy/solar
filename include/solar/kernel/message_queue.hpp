#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

#include <zephyr/kernel.h>

#include "solar/core/status.hpp"
#include "solar/kernel/deadline.hpp"
#include "solar/kernel/error.hpp"
#include "solar/kernel/interrupt.hpp"

namespace solar::kernel
{

struct MessageQueueAttributes
{
    std::size_t message_size{};
    std::size_t capacity{};
    std::size_t used{};
    std::size_t available{};
};

template <std::size_t Capacity> class PollSet;

/** Non-owning typed access to an initialized Zephyr message queue. */
template <typename Message> class MessageQueueRef
{
    static_assert(std::is_trivially_copyable_v<Message>,
                  "SOLAR_DIAGNOSTIC_MESSAGE_QUEUE_NONTRIVIAL_PAYLOAD: message types must be "
                  "trivially copyable");

  public:
    [[nodiscard]] static Result<MessageQueueRef> borrow(k_msgq& queue)
    {
        k_msgq_attrs attributes{};
        k_msgq_get_attrs(&queue, &attributes);
        if (attributes.msg_size != sizeof(Message)) {
            return fail<Error>({.status = Status::Invalid});
        }
        return MessageQueueRef{queue, Unchecked{}};
    }

    [[nodiscard]] Result<void> send(const Message& message,
                                    Timeout timeout = Timeout::forever()) const
    {
        if (in_isr()) {
            return fail<Error>({.status = Status::Invalid});
        }
        return send_native(message, timeout);
    }

    [[nodiscard]] Result<void> send(const Message& message, const Deadline& deadline) const
    {
        return send(message, deadline.remaining());
    }

    [[nodiscard]] Result<void> try_send(const Message& message) const
    {
        return send(message, Timeout::no_wait());
    }

    [[nodiscard]] Result<void> try_send_isr(const Message& message) const
    {
        return send_native(message, Timeout::no_wait());
    }

    [[nodiscard]] Result<void> try_send_front(const Message& message) const
    {
        const int result = k_msgq_put_front(queue_, &message);
        return result == 0 ? Result<void>{}
                           : Result<void>{fail<Error>({.status = Status::Full, .native = result})};
    }

    [[nodiscard]] Result<Message> receive(Timeout timeout = Timeout::forever()) const
    {
        if (in_isr()) {
            return fail<Error>({.status = Status::Invalid});
        }
        return receive_native(timeout);
    }

    [[nodiscard]] Result<Message> receive(const Deadline& deadline) const
    {
        return receive(deadline.remaining());
    }

    [[nodiscard]] Result<Message> try_receive() const
    {
        return receive(Timeout::no_wait());
    }

    [[nodiscard]] Result<Message> try_receive_isr() const
    {
        return receive_native(Timeout::no_wait());
    }

    [[nodiscard]] Result<Message> peek() const
    {
        std::array<std::byte, sizeof(Message)> bytes{};
        const int result = k_msgq_peek(queue_, bytes.data());
        if (result != 0) {
            return fail<solar::Error>({.status = solar::Status::Empty, .native = result});
        }
        return std::bit_cast<Message>(bytes);
    }

    [[nodiscard]] Result<Message> peek_at(std::size_t index) const
    {
        if (index > std::numeric_limits<std::uint32_t>::max()) {
            return fail<solar::Error>({.status = solar::Status::Invalid});
        }
        std::array<std::byte, sizeof(Message)> bytes{};
        const int result = k_msgq_peek_at(queue_, bytes.data(), static_cast<std::uint32_t>(index));
        if (result != 0) {
            return fail<solar::Error>({.status = solar::Status::NotFound, .native = result});
        }
        return std::bit_cast<Message>(bytes);
    }

    void purge() const
    {
        k_msgq_purge(queue_);
    }

    [[nodiscard]] std::size_t size() const
    {
        return k_msgq_num_used_get(queue_);
    }

    [[nodiscard]] std::size_t available() const
    {
        return k_msgq_num_free_get(queue_);
    }

    [[nodiscard]] bool empty() const
    {
        return size() == 0;
    }
    [[nodiscard]] bool full() const
    {
        return available() == 0;
    }

    [[nodiscard]] MessageQueueAttributes attributes() const
    {
        k_msgq_attrs native{};
        k_msgq_get_attrs(queue_, &native);
        return {.message_size = native.msg_size,
                .capacity = native.max_msgs,
                .used = native.used_msgs,
                .available = native.max_msgs - native.used_msgs};
    }

  private:
    [[nodiscard]] Result<void> send_native(const Message& message, Timeout timeout) const
    {
        return detail::map_wait(k_msgq_put(queue_, &message, timeout.native_handle()), timeout,
                                Status::Full);
    }

    [[nodiscard]] Result<Message> receive_native(Timeout timeout) const
    {
        std::array<std::byte, sizeof(Message)> bytes{};
        const auto status = detail::map_wait(
            k_msgq_get(queue_, bytes.data(), timeout.native_handle()), timeout, Status::Empty);
        if (!status) {
            return fail<Error>(status.error());
        }
        return std::bit_cast<Message>(bytes);
    }

    [[nodiscard]] constexpr k_msgq* native_queue() const
    {
        return queue_;
    }

    struct Unchecked
    {};

    constexpr MessageQueueRef(k_msgq& queue, Unchecked) : queue_(&queue) {}

    template <typename, std::size_t> friend class MessageQueue;
    template <std::size_t> friend class PollSet;

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

    MessageQueue()
    {
        k_msgq_init(&queue_, reinterpret_cast<char*>(storage_.data()), sizeof(Message), Capacity);
    }

    MessageQueue(const MessageQueue&) = delete;
    MessageQueue& operator=(const MessageQueue&) = delete;
    MessageQueue(MessageQueue&&) = delete;
    MessageQueue& operator=(MessageQueue&&) = delete;

    [[nodiscard]] Result<void> send(const Message& message,
                                    Timeout timeout = Timeout::forever())
    {
        return ref().send(message, timeout);
    }

    [[nodiscard]] Result<void> send(const Message& message, const Deadline& deadline)
    {
        return send(message, deadline.remaining());
    }

    [[nodiscard]] Result<void> try_send(const Message& message)
    {
        return send(message, Timeout::no_wait());
    }

    [[nodiscard]] Result<void> try_send_isr(const Message& message)
    {
        return ref().try_send_isr(message);
    }

    [[nodiscard]] Result<void> try_send_front(const Message& message)
    {
        return ref().try_send_front(message);
    }

    [[nodiscard]] Result<Message> receive(Timeout timeout = Timeout::forever())
    {
        return ref().receive(timeout);
    }

    [[nodiscard]] Result<Message> receive(const Deadline& deadline)
    {
        return receive(deadline.remaining());
    }

    [[nodiscard]] Result<Message> try_receive()
    {
        return receive(Timeout::no_wait());
    }

    [[nodiscard]] Result<Message> try_receive_isr()
    {
        return ref().try_receive_isr();
    }

    [[nodiscard]] Result<Message> peek()
    {
        return ref().peek();
    }

    [[nodiscard]] Result<Message> peek_at(std::size_t index)
    {
        return ref().peek_at(index);
    }

    void purge()
    {
        ref().purge();
    }

    [[nodiscard]] std::size_t size() const
    {
        return k_msgq_num_used_get(const_cast<k_msgq*>(&queue_));
    }

    [[nodiscard]] std::size_t available() const
    {
        return k_msgq_num_free_get(const_cast<k_msgq*>(&queue_));
    }

    [[nodiscard]] bool empty() const
    {
        return size() == 0;
    }

    [[nodiscard]] bool full() const
    {
        return size() == Capacity;
    }

    [[nodiscard]] MessageQueueAttributes attributes() const
    {
        return MessageQueueRef<Message>{const_cast<k_msgq&>(queue_),
                                        typename MessageQueueRef<Message>::Unchecked{}}
            .attributes();
    }

    [[nodiscard]] MessageQueueRef<Message> ref()
    {
        return MessageQueueRef<Message>{queue_, typename MessageQueueRef<Message>::Unchecked{}};
    }

  private:
    alignas(Message) std::array<std::byte, sizeof(Message) * Capacity> storage_{};
    k_msgq queue_{};
};

} // namespace solar::kernel
