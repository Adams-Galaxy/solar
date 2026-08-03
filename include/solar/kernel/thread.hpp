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
#include "solar/kernel/time.hpp"

namespace solar::kernel
{

using ThreadId = k_tid_t;

/** Non-owning identity for an initialized native Zephyr thread. */
class ThreadRef
{
  public:
    explicit constexpr ThreadRef(k_thread& thread) noexcept : thread_(&thread) {}

    /** Return the native thread identity; lifecycle mutation remains wrapped. */
    [[nodiscard]] constexpr ThreadId id() const noexcept
    {
        return thread_;
    }

    [[nodiscard]] Result<Priority> priority() const noexcept
    {
        return Priority::from_native(k_thread_priority_get(thread_));
    }

    void set_priority(Priority priority) const noexcept
    {
        k_thread_priority_set(thread_, priority.native_handle());
    }

    void wakeup() const noexcept
    {
        k_wakeup(thread_);
    }

    void suspend() const noexcept
    {
        k_thread_suspend(thread_);
    }

    void resume() const noexcept
    {
        k_thread_resume(thread_);
    }

    void abort() const noexcept
    {
        k_thread_abort(thread_);
    }

    [[nodiscard]] Result<void> join(Timeout timeout = Timeout::forever()) const noexcept
    {
        if (in_isr()) {
            return fail<Error>({.status = Status::Invalid});
        }
        return detail::map_wait(k_thread_join(thread_, timeout.native_handle()), timeout,
                                Status::WouldBlock);
    }

    [[nodiscard]] Result<void> join(const Deadline& deadline) const noexcept
    {
        return join(deadline.remaining());
    }

    [[nodiscard]] Result<bool> exited() const noexcept
    {
        const int result = k_thread_join(thread_, K_NO_WAIT);
        if (result == 0) {
            return true;
        }
        if (result == -EBUSY) {
            return false;
        }
        return fail<Error>(error_from_errno(result));
    }

#if defined(CONFIG_SYS_CLOCK_EXISTS)
    [[nodiscard]] TimePoint wake_deadline() const noexcept
    {
        return TimePoint{TickDuration{k_thread_timeout_expires_ticks(thread_)}};
    }

    [[nodiscard]] TickDuration wake_remaining() const noexcept
    {
        return TickDuration{k_thread_timeout_remaining_ticks(thread_)};
    }
#endif

  private:
    k_thread* thread_;
};

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
    Priority priority;
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
        (void)id;
    }

    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;
    Thread(Thread&&) = delete;
    Thread& operator=(Thread&&) = delete;

    [[nodiscard]] Result<void> prepare(Entry entry, ThreadConfiguration configuration) noexcept
    {
        return prepare(entry, nullptr, configuration);
    }

    [[nodiscard]] Result<void> prepare(Entry entry, void* argument,
                                       ThreadConfiguration configuration) noexcept
    {
        return create(entry, argument, configuration, Timeout::forever(), true);
    }

    [[nodiscard]] Result<void> launch(Entry entry, ThreadConfiguration configuration,
                                      Timeout delay = Timeout::no_wait()) noexcept
    {
        return launch(entry, nullptr, configuration, delay);
    }

    [[nodiscard]] Result<void> launch(Entry entry, void* argument,
                                      ThreadConfiguration configuration,
                                      Timeout delay = Timeout::no_wait()) noexcept
    {
        return create(entry, argument, configuration, delay, false);
    }

    [[nodiscard]] Result<void> start() noexcept
    {
        ThreadExecutionState expected = ThreadExecutionState::Prepared;
        if (!state_.compare_exchange_strong(expected, ThreadExecutionState::Scheduled,
                                            std::memory_order_acq_rel)) {
            return fail<Error>({.status = expected == ThreadExecutionState::Scheduled ||
                                                  expected == ThreadExecutionState::Running
                                              ? Status::Already
                                              : Status::NotReady});
        }
        k_thread_start(&thread_);
        return {};
    }

    [[nodiscard]] Result<void> suspend() noexcept
    {
        const auto id = id_.load(std::memory_order_acquire);
        if (id == nullptr) {
            return fail<Error>({.status = Status::NotReady});
        }
        if (id == k_current_get()) {
            return fail<Error>({.status = Status::Invalid});
        }
        if (!active()) {
            return fail<Error>({.status = Status::NotReady});
        }
        ThreadRef{*id}.suspend();
        state_.store(ThreadExecutionState::Suspended, std::memory_order_release);
        return {};
    }

    [[nodiscard]] Result<void> resume() noexcept
    {
        const auto id = id_.load(std::memory_order_acquire);
        if (id == nullptr || state() != ThreadExecutionState::Suspended) {
            return fail<Error>({.status = Status::NotReady});
        }
        state_.store(ThreadExecutionState::Scheduled, std::memory_order_release);
        ThreadRef{*id}.resume();
        return {};
    }

    [[nodiscard]] Result<void> join(Timeout timeout = Timeout::forever()) noexcept
    {
        if (id_.load(std::memory_order_acquire) == nullptr) {
            return fail<Error>({.status = Status::NotReady});
        }
        if (state() == ThreadExecutionState::Prepared) {
            return fail<Error>({.status = Status::NotReady});
        }
        if (in_isr()) {
            return fail<Error>({.status = Status::Invalid});
        }

        const auto status = ThreadRef{thread_}.join(timeout);
        if (status && state() != ThreadExecutionState::Aborted) {
            state_.store(ThreadExecutionState::Exited, std::memory_order_release);
        }
        return status;
    }

    [[nodiscard]] Result<void> join(const Deadline& deadline) noexcept
    {
        return join(deadline.remaining());
    }

    [[nodiscard]] Result<bool> exited() const noexcept
    {
        if (id_.load(std::memory_order_acquire) == nullptr) {
            return fail<solar::Error>({.status = solar::Status::NotReady});
        }
        if (state() == ThreadExecutionState::Prepared) {
            return false;
        }

        return ThreadRef{const_cast<k_thread&>(thread_)}.exited();
    }

    [[nodiscard]] Result<void> abort() noexcept
    {
        const auto id = id_.load(std::memory_order_acquire);
        if (id == nullptr) {
            return fail<Error>({.status = Status::NotReady});
        }
        if (id == k_current_get()) {
            return fail<Error>({.status = Status::Invalid});
        }
        const auto already_exited = exited();
        if (already_exited && *already_exited) {
            return fail<Error>({.status = Status::Already});
        }
        if (!already_exited && status_of(already_exited.error()) != Status::NotReady) {
            return fail<Error>(already_exited.error());
        }

        ThreadRef{*id}.abort();
        state_.store(ThreadExecutionState::Aborted, std::memory_order_release);
        return {};
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

    [[nodiscard]] Result<ThreadRef> ref() noexcept
    {
        const auto id = id_.load(std::memory_order_acquire);
        if (id == nullptr) {
            return fail<Error>({.status = Status::NotReady});
        }
        return ThreadRef{*id};
    }

    [[nodiscard]] static constexpr std::size_t stack_size() noexcept
    {
        return K_KERNEL_STACK_SIZEOF(stack_);
    }

  private:
    [[nodiscard]] Result<void> validate(Entry entry,
                                        const ThreadConfiguration& configuration) const noexcept
    {
        if (entry == nullptr) {
            return fail<Error>({.status = Status::Invalid});
        }
        if (active()) {
            return fail<Error>({.status = Status::Already});
        }
        if (configuration.name != nullptr) {
            if (!IS_ENABLED(CONFIG_THREAD_NAME)) {
                return fail<Error>({.status = Status::NotSupported});
            }
#if defined(CONFIG_THREAD_NAME)
            if (std::strlen(configuration.name) >= CONFIG_THREAD_MAX_NAME_LEN) {
                return fail<Error>({.status = Status::Invalid});
            }
#endif
        }
        return {};
    }

    [[nodiscard]] Result<void> create(Entry entry, void* argument,
                                      ThreadConfiguration configuration, Timeout delay,
                                      bool prepared_only) noexcept
    {
        if (in_isr()) {
            return fail<Error>({.status = Status::Invalid});
        }
        const auto valid = validate(entry, configuration);
        if (!valid) {
            return valid;
        }
        if (!prepared_only && delay.is_forever()) {
            return fail<Error>({.status = Status::Invalid});
        }

        entry_ = entry;
        argument_ = argument;
        state_.store(prepared_only ? ThreadExecutionState::Prepared
                                   : ThreadExecutionState::Scheduled,
                     std::memory_order_release);

        const bool controlled_start = prepared_only || delay.is_no_wait();
        const auto native_delay = controlled_start ? K_FOREVER : delay.native_handle();
        const auto id = k_thread_create(
            &thread_, stack_, K_KERNEL_STACK_SIZEOF(stack_), &Thread::trampoline, this, nullptr,
            nullptr, configuration.priority.native_handle(), configuration.options, native_delay);
        if (id == nullptr) {
            state_.store(ThreadExecutionState::Empty, std::memory_order_release);
            return fail<Error>({.status = Status::Error});
        }
        id_.store(id, std::memory_order_release);

        if (configuration.name != nullptr) {
            const auto name_status = detail::map_native(k_thread_name_set(id, configuration.name));
            if (!name_status) {
                k_thread_abort(id);
                state_.store(ThreadExecutionState::Aborted, std::memory_order_release);
                return name_status;
            }
        }

        if (!prepared_only && controlled_start) {
            k_thread_start(id);
        }
        return {};
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
