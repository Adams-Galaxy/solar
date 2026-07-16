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

namespace solar::kernel
{

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

    [[nodiscard]] Status send(const Message& message, Timeout timeout = Timeout::forever()) noexcept
    {
        return detail::map_wait(k_msgq_put(&queue_, &message, timeout.native_handle()), timeout,
                                Status::Full);
    }

    [[nodiscard]] Status send(const Message& message, const Deadline& deadline) noexcept
    {
        return send(message, deadline.remaining());
    }

    [[nodiscard]] Status try_send(const Message& message) noexcept
    {
        return send(message, Timeout::no_wait());
    }

    [[nodiscard]] Status try_send_isr(const Message& message) noexcept
    {
        return send(message, Timeout::no_wait());
    }

    [[nodiscard]] Status try_send_front(const Message& message) noexcept
    {
        return k_msgq_put_front(&queue_, &message) == 0 ? Status::Ok : Status::Full;
    }

    [[nodiscard]] Status try_send_front_isr(const Message& message) noexcept
    {
        return try_send_front(message);
    }

    [[nodiscard]] Result<Message> receive(Timeout timeout = Timeout::forever()) noexcept
    {
        std::array<std::byte, sizeof(Message)> bytes{};
        const auto status = detail::map_wait(
            k_msgq_get(&queue_, bytes.data(), timeout.native_handle()), timeout, Status::Empty);
        if (status != Status::Ok) {
            return fail(status);
        }
        return std::bit_cast<Message>(bytes);
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
        std::array<std::byte, sizeof(Message)> bytes{};
        if (k_msgq_peek(&queue_, bytes.data()) != 0) {
            return fail(Status::Empty);
        }
        return std::bit_cast<Message>(bytes);
    }

    [[nodiscard]] Result<Message> peek_at(std::size_t index) noexcept
    {
        if (index > std::numeric_limits<std::uint32_t>::max()) {
            return fail(Status::Invalid);
        }
        std::array<std::byte, sizeof(Message)> bytes{};
        if (k_msgq_peek_at(&queue_, bytes.data(), static_cast<std::uint32_t>(index)) != 0) {
            return fail(Status::NotFound);
        }
        return std::bit_cast<Message>(bytes);
    }

    void purge() noexcept
    {
        k_msgq_purge(&queue_);
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return k_msgq_num_used_get(const_cast<k_msgq*>(&queue_));
    }

    [[nodiscard]] std::size_t available() const noexcept
    {
        return Capacity - size();
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return size() == 0;
    }

    [[nodiscard]] bool full() const noexcept
    {
        return size() == Capacity;
    }

    [[nodiscard]] k_msgq* native_handle() noexcept
    {
        return &queue_;
    }

    [[nodiscard]] const k_msgq* native_handle() const noexcept
    {
        return &queue_;
    }

  private:
    alignas(Message) std::array<std::byte, sizeof(Message) * Capacity> storage_{};
    k_msgq queue_{};
};

} // namespace solar::kernel
