#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>

#include <zephyr/kernel.h>

#include "solar/core/status.hpp"
#include "solar/kernel/priority.hpp"

namespace solar::kernel
{

using ThreadId = k_tid_t;

template <std::size_t StackWords>
struct ThreadStorage
{
    static constexpr std::size_t Bytes = StackWords * sizeof(void *);
    alignas(ARCH_STACK_PTR_ALIGN) k_thread_stack_t stack[K_THREAD_STACK_LEN(Bytes)]{};

    constexpr std::size_t size() const
    {
        return K_THREAD_STACK_SIZEOF(stack);
    }
};

class Thread
{
public:
    using Entry = void (*)(void *);

    Thread(const char *name, Priority priority, std::uint32_t, Entry entry, void *arg = nullptr)
        : name_(name), priority_(priority), entry_(entry), arg_(arg) {}

    Thread(const Thread &) = delete;
    Thread &operator=(const Thread &) = delete;

    template <std::size_t StackWords>
    Status start(ThreadStorage<StackWords> &storage)
    {
        if (entry_ == nullptr || tid_ != nullptr)
        {
            return Status::Invalid;
        }

        stop_requested_.store(false);
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
            return Status::Error;
        }

        if (name_ != nullptr)
        {
            k_thread_name_set(tid_, name_);
        }
        running_.store(true);
        return Status::Ok;
    }

    void request_stop()
    {
        stop_requested_.store(true);
    }

    bool stop_requested() const
    {
        return stop_requested_.load();
    }

    bool is_running() const
    {
        return running_.load();
    }

    Status terminate()
    {
        if (tid_ == nullptr)
        {
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
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> running_{false};
};

} // namespace solar::kernel
