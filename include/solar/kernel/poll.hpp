#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <zephyr/kernel.h>

#include "solar/core/status.hpp"
#include "solar/kernel/config.hpp"
#include "solar/kernel/queue.hpp"
#include "solar/kernel/semaphore.hpp"
#include "solar/kernel/thread.hpp"
#include "solar/kernel/time.hpp"

namespace solar::kernel
{

#if defined(CONFIG_POLL)

template <std::size_t Capacity>
class PollSet
{
public:
    static_assert(Capacity > 0, "Solar poll sets require a non-zero capacity");

    Status add_signal(k_poll_signal *signal, std::uint8_t tag = 0)
    {
        return add(K_POLL_TYPE_SIGNAL, signal, tag);
    }

    Status add_stop(StopToken token, std::uint8_t tag = 0)
    {
        return add(K_POLL_TYPE_SEM_AVAILABLE, token.native_semaphore(), tag);
    }

    Status add_semaphore(Semaphore &semaphore, std::uint8_t tag = 0)
    {
        return add(K_POLL_TYPE_SEM_AVAILABLE, semaphore.native_handle(), tag);
    }

    template <typename T, std::size_t Depth>
    Status add_queue(Queue<T, Depth> &queue, std::uint8_t tag = 0)
    {
        return add(K_POLL_TYPE_MSGQ_DATA_AVAILABLE, queue.native_handle(), tag);
    }

    Status wait(Timeout timeout = Timeout::forever())
    {
        reset_states();
        return status_from_native_wait(k_poll(events_.data(), static_cast<int>(count_), timeout.native()));
    }

    std::size_t count() const
    {
        return count_;
    }

    const k_poll_event &event(std::size_t index) const
    {
        return events_[index];
    }

    bool ready(std::size_t index) const
    {
        return index < count_ && events_[index].state != K_POLL_STATE_NOT_READY;
    }

    std::uint8_t tag(std::size_t index) const
    {
        return index < count_ ? static_cast<std::uint8_t>(events_[index].tag) : 0;
    }

private:
    Status add(std::uint32_t type, void *object, std::uint8_t tag)
    {
        if (object == nullptr)
        {
            return Status::Invalid;
        }
        if (count_ >= Capacity)
        {
            return Status::Full;
        }

        k_poll_event_init(&events_[count_], type, K_POLL_MODE_NOTIFY_ONLY, object);
        events_[count_].tag = tag;
        ++count_;
        return Status::Ok;
    }

    void reset_states()
    {
        for (std::size_t i = 0; i < count_; ++i)
        {
            events_[i].state = K_POLL_STATE_NOT_READY;
        }
    }

    std::array<k_poll_event, Capacity> events_{};
    std::size_t count_ = 0;
};

#else

template <std::size_t Capacity>
class PollSet
{
public:
    static_assert(Capacity > 0, "Solar poll sets require a non-zero capacity");

    Status add_signal(k_poll_signal *, std::uint8_t = 0)
    {
        return Status::NotReady;
    }

    Status add_stop(StopToken, std::uint8_t = 0)
    {
        return Status::NotReady;
    }

    Status add_semaphore(Semaphore &, std::uint8_t = 0)
    {
        return Status::NotReady;
    }

    template <typename T, std::size_t Depth>
    Status add_queue(Queue<T, Depth> &, std::uint8_t = 0)
    {
        return Status::NotReady;
    }

    Status wait(Timeout = Timeout::forever())
    {
        return Status::NotReady;
    }

    std::size_t count() const
    {
        return 0;
    }

    bool ready(std::size_t) const
    {
        return false;
    }

    std::uint8_t tag(std::size_t) const
    {
        return 0;
    }
};

#endif

} // namespace solar::kernel
