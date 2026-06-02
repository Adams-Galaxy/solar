#pragma once

#include <cstddef>
#include <type_traits>

#include <zephyr/kernel.h>

#include "solar/core/status.hpp"
#include "solar/rtos/deadline.hpp"

namespace solar::rtos
{

template <typename T, std::size_t Depth>
class Queue
{
public:
    static_assert(Depth > 0, "Solar queues require non-zero depth");
    static_assert(std::is_trivially_copyable_v<T>, "Zephyr message queue payloads must be trivially copyable");

    Queue()
    {
        k_msgq_init(&queue_, buffer_, sizeof(T), Depth);
    }

    Status send(const T &value, Tick timeout_ticks = WaitForever)
    {
        return k_msgq_put(&queue_, &value, to_timeout(timeout_ticks)) == 0 ? Status::Ok : Status::Timeout;
    }

    Status try_send(const T &value)
    {
        return send(value, 0);
    }

    Status receive(T &out, Tick timeout_ticks = WaitForever)
    {
        return k_msgq_get(&queue_, &out, to_timeout(timeout_ticks)) == 0 ? Status::Ok : Status::Timeout;
    }

    Status try_receive(T &out)
    {
        return receive(out, 0);
    }

    std::size_t size() const
    {
        return k_msgq_num_used_get(const_cast<k_msgq *>(&queue_));
    }

    static constexpr std::size_t capacity()
    {
        return Depth;
    }

private:
    alignas(alignof(T)) char buffer_[sizeof(T) * Depth]{};
    k_msgq queue_{};
};

} // namespace solar::rtos
