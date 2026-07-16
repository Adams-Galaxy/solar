#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <zephyr/kernel.h>

#include "solar/core/status.hpp"
#include "solar/kernel/deadline.hpp"
#include "solar/kernel/error.hpp"
#include "solar/kernel/interrupt.hpp"
#include "solar/kernel/priority.hpp"

namespace solar::kernel
{

using ThreadId = k_tid_t;

enum class ThreadExecutionState : std::uint8_t
{
    Unknown,
    Empty,
    Prepared,
    Scheduled,
    Running,
    Suspended,
    Exited,
    Aborted,
};

struct ThreadConfiguration
{
    Priority priority{};
    const char* name{};
    std::uint32_t options{};
};

template <std::size_t StackBytes> class Thread
{
    static_assert(StackBytes > 0,
                  "SOLAR_DIAGNOSTIC_THREAD_ZERO_STACK: thread stack must be non-zero");

  public:
    using Entry = void (*)(void*) noexcept;

    static constexpr std::size_t requested_stack_size = StackBytes;

    Thread() = default;

    ~Thread()
    {
        const auto id = id_.load(std::memory_order_acquire);
        __ASSERT_NO_MSG(id == nullptr || k_thread_join(&thread_, K_NO_WAIT) == 0);
    }

    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;
    Thread(Thread&&) = delete;
    Thread& operator=(Thread&&) = delete;

    [[nodiscard]] Status prepare(Entry entry, void* argument = nullptr,
                                 ThreadConfiguration configuration = {}) noexcept
    {
        return create(entry, argument, configuration, Timeout::forever(), true);
    }

    [[nodiscard]] Status launch(Entry entry, void* argument = nullptr,
                                ThreadConfiguration configuration = {},
                                Timeout delay = Timeout::no_wait()) noexcept
    {
        return create(entry, argument, configuration, delay, false);
    }

    [[nodiscard]] Status start() noexcept
    {
        ThreadExecutionState expected = ThreadExecutionState::Prepared;
        if (!state_.compare_exchange_strong(expected, ThreadExecutionState::Scheduled,
                                            std::memory_order_acq_rel)) {
            return expected == ThreadExecutionState::Scheduled ||
                           expected == ThreadExecutionState::Running
                       ? Status::Already
                       : Status::NotReady;
        }
        k_thread_start(&thread_);
        return Status::Ok;
    }

    [[nodiscard]] Status suspend() noexcept
    {
        const auto id = id_.load(std::memory_order_acquire);
        if (id == nullptr) {
            return Status::NotReady;
        }
        if (id == k_current_get()) {
            return Status::Invalid;
        }
        if (!active()) {
            return Status::NotReady;
        }
        k_thread_suspend(id);
        state_.store(ThreadExecutionState::Suspended, std::memory_order_release);
        return Status::Ok;
    }

    [[nodiscard]] Status resume() noexcept
    {
        const auto id = id_.load(std::memory_order_acquire);
        if (id == nullptr || state() != ThreadExecutionState::Suspended) {
            return Status::NotReady;
        }
        state_.store(ThreadExecutionState::Scheduled, std::memory_order_release);
        k_thread_resume(id);
        return Status::Ok;
    }

    [[nodiscard]] Status join(Timeout timeout = Timeout::forever()) noexcept
    {
        if (id_.load(std::memory_order_acquire) == nullptr) {
            return Status::NotReady;
        }
        if (state() == ThreadExecutionState::Prepared) {
            return Status::NotReady;
        }
        if (in_isr() && !timeout.is_no_wait()) {
            return Status::Invalid;
        }

        const int result = k_thread_join(&thread_, timeout.native_handle());
        const auto status = detail::map_wait(result, timeout, Status::WouldBlock);
        if (status == Status::Ok && state() != ThreadExecutionState::Aborted) {
            state_.store(ThreadExecutionState::Exited, std::memory_order_release);
        }
        return status;
    }

    [[nodiscard]] Status join(const Deadline& deadline) noexcept
    {
        return join(deadline.remaining());
    }

    [[nodiscard]] Result<bool> exited() const noexcept
    {
        if (id_.load(std::memory_order_acquire) == nullptr) {
            return fail(Status::NotReady);
        }
        if (state() == ThreadExecutionState::Prepared) {
            return false;
        }

        const int result = k_thread_join(const_cast<k_thread*>(&thread_), K_NO_WAIT);
        if (result == 0) {
            return true;
        }
        if (result == -EBUSY) {
            return false;
        }
        return fail(status_from_errno(result));
    }

    [[nodiscard]] Status abort() noexcept
    {
        const auto id = id_.load(std::memory_order_acquire);
        if (id == nullptr) {
            return Status::NotReady;
        }
        if (id == k_current_get()) {
            return Status::Invalid;
        }
        const auto already_exited = exited();
        if (already_exited && *already_exited) {
            return Status::Already;
        }
        if (!already_exited && already_exited.error() != Status::NotReady) {
            return already_exited.error();
        }

        k_thread_abort(id);
        state_.store(ThreadExecutionState::Aborted, std::memory_order_release);
        return Status::Ok;
    }

    [[nodiscard]] ThreadExecutionState state() const noexcept
    {
        return state_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool active() const noexcept
    {
        const auto current = state();
        return current == ThreadExecutionState::Prepared ||
               current == ThreadExecutionState::Scheduled ||
               current == ThreadExecutionState::Running ||
               current == ThreadExecutionState::Suspended;
    }

    [[nodiscard]] ThreadId native_handle() const noexcept
    {
        return id_.load(std::memory_order_acquire);
    }

    [[nodiscard]] k_thread* native_thread() noexcept
    {
        return &thread_;
    }

    [[nodiscard]] const k_thread* native_thread() const noexcept
    {
        return &thread_;
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
    [[nodiscard]] Status validate(Entry entry, const ThreadConfiguration& configuration) const
        noexcept
    {
        if (entry == nullptr) {
            return Status::Invalid;
        }
        if (active()) {
            return Status::Already;
        }
        if (configuration.name != nullptr) {
            if (!IS_ENABLED(CONFIG_THREAD_NAME)) {
                return Status::NotSupported;
            }
#if defined(CONFIG_THREAD_NAME)
            if (std::strlen(configuration.name) >= CONFIG_THREAD_MAX_NAME_LEN) {
                return Status::Invalid;
            }
#endif
        }
        return Status::Ok;
    }

    [[nodiscard]] Status create(Entry entry, void* argument, ThreadConfiguration configuration,
                                Timeout delay, bool prepared_only) noexcept
    {
        const auto valid = validate(entry, configuration);
        if (valid != Status::Ok) {
            return valid;
        }
        if (!prepared_only && delay.is_forever()) {
            return Status::Invalid;
        }

        entry_ = entry;
        argument_ = argument;
        state_.store(prepared_only ? ThreadExecutionState::Prepared
                                   : ThreadExecutionState::Scheduled,
                     std::memory_order_release);

        const bool controlled_start = prepared_only || delay.is_no_wait();
        const auto native_delay = controlled_start ? K_FOREVER : delay.native_handle();
        const auto id = k_thread_create(&thread_, stack_, K_KERNEL_STACK_SIZEOF(stack_),
                                        &Thread::trampoline, this, nullptr, nullptr,
                                        configuration.priority.native_handle(),
                                        configuration.options, native_delay);
        if (id == nullptr) {
            state_.store(ThreadExecutionState::Empty, std::memory_order_release);
            return Status::Error;
        }
        id_.store(id, std::memory_order_release);

        if (configuration.name != nullptr) {
            const auto name_status = detail::map_native(k_thread_name_set(id, configuration.name));
            if (name_status != Status::Ok) {
                k_thread_abort(id);
                state_.store(ThreadExecutionState::Aborted, std::memory_order_release);
                return name_status;
            }
        }

        if (!prepared_only && controlled_start) {
            k_thread_start(id);
        }
        return Status::Ok;
    }

    static void trampoline(void* self_pointer, void*, void*) noexcept
    {
        auto& self = *static_cast<Thread*>(self_pointer);
        self.state_.store(ThreadExecutionState::Running, std::memory_order_release);
        self.entry_(self.argument_);
        self.state_.store(ThreadExecutionState::Exited, std::memory_order_release);
    }

    k_thread thread_{};
    K_KERNEL_STACK_MEMBER(stack_, StackBytes);
    Entry entry_{};
    void* argument_{};
    std::atomic<ThreadId> id_{nullptr};
    std::atomic<ThreadExecutionState> state_{ThreadExecutionState::Empty};
};

} // namespace solar::kernel
