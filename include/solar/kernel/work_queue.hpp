#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <zephyr/kernel.h>

#include "solar/core/status.hpp"
#include "solar/kernel/deadline.hpp"
#include "solar/kernel/error.hpp"
#include "solar/kernel/interrupt.hpp"
#include "solar/kernel/priority.hpp"
#include "solar/kernel/time.hpp"

namespace solar::kernel
{

struct WorkQueueConfiguration
{
    Priority priority{};
    const char* name{};
    bool no_yield{};
    bool essential{};
    Milliseconds work_timeout{};
};

class SystemWorkQueue
{
  public:
    [[nodiscard]] k_work_q* native_handle() const noexcept
    {
        return &k_sys_work_q;
    }

    [[nodiscard]] k_tid_t thread_id() const noexcept
    {
        return k_work_queue_thread_get(&k_sys_work_q);
    }
};

inline constexpr SystemWorkQueue system_work_queue{};

template <std::size_t StackBytes> class WorkQueue
{
    static_assert(StackBytes > 0,
                  "SOLAR_DIAGNOSTIC_WORK_QUEUE_ZERO_STACK: workqueue stack must be non-zero");

  public:
    static constexpr std::size_t requested_stack_size = StackBytes;

    WorkQueue() noexcept
    {
        k_work_queue_init(&queue_);
    }

    ~WorkQueue()
    {
        __ASSERT_NO_MSG(!started());
    }

    WorkQueue(const WorkQueue&) = delete;
    WorkQueue& operator=(const WorkQueue&) = delete;
    WorkQueue(WorkQueue&&) = delete;
    WorkQueue& operator=(WorkQueue&&) = delete;

    [[nodiscard]] Status start(WorkQueueConfiguration configuration = {}) noexcept
    {
        if (in_isr()) {
            return Status::Invalid;
        }
        if (started()) {
            return Status::Already;
        }
        if (configuration.name != nullptr && !IS_ENABLED(CONFIG_THREAD_NAME)) {
            return Status::NotSupported;
        }
        if (configuration.work_timeout.count() < 0 ||
            static_cast<std::uint64_t>(configuration.work_timeout.count()) >
                std::numeric_limits<std::uint32_t>::max()) {
            return Status::Invalid;
        }
        if (configuration.work_timeout.count() != 0 && !IS_ENABLED(CONFIG_WORKQUEUE_WORK_TIMEOUT)) {
            return Status::NotSupported;
        }

        const k_work_queue_config native{
            .name = configuration.name,
            .no_yield = configuration.no_yield,
            .essential = configuration.essential,
            .work_timeout_ms = static_cast<std::uint32_t>(configuration.work_timeout.count()),
        };
        k_work_queue_start(&queue_, stack_, K_KERNEL_STACK_SIZEOF(stack_),
                           configuration.priority.native_handle(), &native);
        started_.store(true, std::memory_order_release);
        return Status::Ok;
    }

    [[nodiscard]] Result<bool> drain(bool plug = false) noexcept
    {
        if (in_isr()) {
            return fail(Status::Invalid);
        }
        if (!started()) {
            return fail(Status::NotReady);
        }
        if (thread_id() == k_current_get()) {
            return fail(Status::Deadlock);
        }
        const int result = k_work_queue_drain(&queue_, plug);
        if (result < 0) {
            return fail(status_from_errno(result));
        }
        return result != 0;
    }

    [[nodiscard]] Status unplug() noexcept
    {
        if (!started()) {
            return Status::NotReady;
        }
        return detail::map_native(k_work_queue_unplug(&queue_));
    }

    [[nodiscard]] Status stop(Timeout timeout = Timeout::forever()) noexcept
    {
        if (in_isr()) {
            return Status::Invalid;
        }
        if (!started()) {
            return Status::Already;
        }
        if (thread_id() == k_current_get()) {
            return Status::Deadlock;
        }
        const int result = k_work_queue_stop(&queue_, timeout.native_handle());
        if (result == 0) {
            started_.store(false, std::memory_order_release);
            return Status::Ok;
        }
        if (result == -ETIMEDOUT) {
            return Status::Timeout;
        }
        return status_from_errno(result);
    }

    [[nodiscard]] Status stop(const Deadline& deadline) noexcept
    {
        return stop(deadline.remaining());
    }

    [[nodiscard]] Status abort() noexcept
    {
        if (in_isr()) {
            return Status::Invalid;
        }
        if (!started()) {
            return Status::Already;
        }
        const auto id = thread_id();
        if (id == nullptr || id == k_current_get()) {
            return Status::Invalid;
        }
        k_thread_abort(id);
        started_.store(false, std::memory_order_release);
        return Status::Ok;
    }

    [[nodiscard]] bool started() const noexcept
    {
        return started_.load(std::memory_order_acquire);
    }

    [[nodiscard]] k_work_q* native_handle() const noexcept
    {
        return const_cast<k_work_q*>(&queue_);
    }

    [[nodiscard]] k_tid_t thread_id() const noexcept
    {
        return k_work_queue_thread_get(const_cast<k_work_q*>(&queue_));
    }

    [[nodiscard]] k_thread_stack_t* native_stack() noexcept
    {
        return stack_;
    }

    [[nodiscard]] static constexpr std::size_t stack_size() noexcept
    {
        return K_KERNEL_STACK_SIZEOF(stack_);
    }

  private:
    k_work_q queue_{};
    K_KERNEL_STACK_MEMBER(stack_, StackBytes);
    std::atomic_bool started_{false};
};

} // namespace solar::kernel
