#pragma once

#include <cstddef>
#include <type_traits>

#include <zephyr/kernel.h>

#include "solar/core/status.hpp"
#include "solar/kernel/config.hpp"
#include "solar/kernel/deadline.hpp"

namespace solar::kernel
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

    Status send(const T &value, Timeout timeout = Timeout::forever())
    {
        const int result = k_msgq_put(&queue_, &value, timeout.native());
#ifdef ENOMSG
        if (result == -ENOMSG && timeout.ticks() == 0)
        {
            return Status::Full;
        }
#endif
        if (result == -EAGAIN)
        {
            return Status::Timeout;
        }
        return status_from_native(result);
    }

    Status send(const T &value, Tick timeout_ticks)
    {
        return send(value, Timeout::after_ticks(timeout_ticks));
    }

    Status try_send(const T &value)
    {
        return send(value, Timeout::no_wait());
    }

    Status try_send_isr(const T &value)
    {
        return try_send(value);
    }

    Status receive(T &out, Timeout timeout = Timeout::forever())
    {
        const int result = k_msgq_get(&queue_, &out, timeout.native());
#ifdef ENOMSG
        if (result == -ENOMSG && timeout.ticks() == 0)
        {
            return Status::Empty;
        }
#endif
        if (result == -EAGAIN)
        {
            return Status::Timeout;
        }
        return status_from_native(result);
    }

    Status receive(T &out, Tick timeout_ticks)
    {
        return receive(out, Timeout::after_ticks(timeout_ticks));
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

    k_msgq *native_handle()
    {
        return &queue_;
    }

    const k_msgq *native_handle() const
    {
        return &queue_;
    }

private:
    alignas(alignof(T)) char buffer_[sizeof(T) * Depth]{};
    k_msgq queue_{};
};

} // namespace solar::kernel
