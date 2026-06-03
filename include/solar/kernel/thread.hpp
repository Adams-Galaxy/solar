#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include <zephyr/kernel.h>

#include "solar/core/status.hpp"
#include "solar/kernel/config.hpp"
#include "solar/kernel/priority.hpp"
#include "solar/kernel/time.hpp"

namespace solar::kernel
{

using ThreadId = k_tid_t;

template <std::size_t StackBytes>
struct ThreadStorage
{
    static_assert(StackBytes > 0, "Solar thread stacks require a non-zero byte count");

    alignas(ARCH_STACK_PTR_ALIGN) k_thread_stack_t stack[K_THREAD_STACK_LEN(StackBytes)]{};

    constexpr std::size_t size() const
    {
        return K_THREAD_STACK_SIZEOF(stack);
    }
};

class StopToken
{
public:
    StopToken() = default;

    bool requested() const
    {
        return requested_ != nullptr && requested_->load();
    }

    bool stop_requested() const
    {
        return requested();
    }

    k_sem *native_semaphore() const
    {
        return semaphore_;
    }

private:
    StopToken(const std::atomic<bool> *requested, k_sem *semaphore)
        : requested_(requested), semaphore_(semaphore) {}

    const std::atomic<bool> *requested_ = nullptr;
    k_sem *semaphore_ = nullptr;

    friend class StopSource;
};

class StopSource
{
public:
    StopSource()
    {
        k_sem_init(&semaphore_, 0, 1);
    }

    StopToken token() const
    {
        return StopToken{&requested_, const_cast<k_sem *>(&semaphore_)};
    }

    void request_stop()
    {
        requested_.store(true);
        k_sem_give(&semaphore_);
    }

    void reset()
    {
        requested_.store(false);
        k_sem_reset(&semaphore_);
    }

    bool requested() const
    {
        return requested_.load();
    }

    k_sem *native_semaphore()
    {
        return &semaphore_;
    }

private:
    std::atomic<bool> requested_{false};
    k_sem semaphore_{};
};

class Thread
{
public:
    using Entry = void (*)(void *);

    Thread(const char *name, Priority priority, std::uint32_t, Entry entry, void *arg = nullptr)
        : name_(name), priority_(priority), entry_(entry), arg_(arg) {}

    Thread(const Thread &) = delete;
    Thread &operator=(const Thread &) = delete;

    template <std::size_t StackBytes>
    Status start(ThreadStorage<StackBytes> &storage)
    {
        if (entry_ == nullptr || tid_ != nullptr)
        {
            return Status::Invalid;
        }

        stop_source_.reset();
        running_.store(true);

        tid_ = k_thread_create(
            &thread_,
            storage.stack,
            storage.size(),
            &Thread::trampoline,
            this,
            nullptr,
            nullptr,
            to_native_priority(priority_),
            0,
            K_NO_WAIT);

        if (tid_ == nullptr)
        {
            running_.store(false);
            return Status::Error;
        }

        if (name_ != nullptr)
        {
            k_thread_name_set(tid_, name_);
        }
        return Status::Ok;
    }

    void request_stop()
    {
        stop_source_.request_stop();
    }

    bool stop_requested() const
    {
        return stop_source_.requested();
    }

    StopToken stop_token() const
    {
        return stop_source_.token();
    }

    bool is_running() const
    {
        return running_.load();
    }

    bool running() const
    {
        return is_running();
    }

    Status join(Timeout timeout = Timeout::forever())
    {
        if (tid_ == nullptr)
        {
            return Status::Ok;
        }

        const int result = k_thread_join(&thread_, timeout.native());
        const Status status = status_from_native_wait(result);
        if (status == Status::Ok)
        {
            tid_ = nullptr;
            running_.store(false);
        }
        return status;
    }

    Status join(Tick timeout_ticks)
    {
        return join(Timeout::after_ticks(timeout_ticks));
    }

    Status stop(Timeout timeout = Timeout::forever())
    {
        request_stop();
        return join(timeout);
    }

    Status abort()
    {
        if (tid_ == nullptr)
        {
            running_.store(false);
            return Status::Ok;
        }
        k_thread_abort(tid_);
        tid_ = nullptr;
        running_.store(false);
        return Status::Ok;
    }

    ThreadId native_handle() const
    {
        return tid_;
    }

    k_thread *native_thread()
    {
        return &thread_;
    }

    const k_thread *native_thread() const
    {
        return &thread_;
    }

private:
    static void trampoline(void *self_ptr, void *, void *)
    {
        auto *self = static_cast<Thread *>(self_ptr);
        if (self == nullptr || self->entry_ == nullptr)
        {
            return;
        }
        self->entry_(self->arg_);
        self->running_.store(false);
    }

    const char *name_ = nullptr;
    Priority priority_ = Priority::Normal;
    Entry entry_ = nullptr;
    void *arg_ = nullptr;
    k_thread thread_{};
    k_tid_t tid_ = nullptr;
    StopSource stop_source_{};
    std::atomic<bool> running_{false};
};

} // namespace solar::kernel
