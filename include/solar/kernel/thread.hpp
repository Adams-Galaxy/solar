#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
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

#if defined(CONFIG_SCHED_DEADLINE)
    [[nodiscard]] Result<void> set_deadline(CycleDuration deadline) const noexcept
    {
        if (in_isr()) {
            return fail<Error>({.status = Status::Invalid});
        }
        k_thread_deadline_set(thread_, deadline.count());
        return {};
    }

    [[nodiscard]] Result<void> set_absolute_deadline(CycleTimePoint deadline) const noexcept
    {
        if (in_isr()) {
            return fail<Error>({.status = Status::Invalid});
        }
        k_thread_absolute_deadline_set(thread_, static_cast<int>(deadline.count()));
        return {};
    }
#endif

#if defined(CONFIG_TIMESLICE_PER_THREAD)
    [[nodiscard]] Result<void> set_time_slice(TickDuration slice,
                                              k_thread_timeslice_fn_t expired = nullptr,
                                              void* user_data = nullptr) const noexcept
    {
        if (in_isr()) {
            return fail<Error>({.status = Status::Invalid});
        }
        if (slice.count() < 0 ||
            static_cast<std::uint64_t>(slice.count()) >
                static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
            return fail<Error>({.status = Status::Invalid});
        }
        k_thread_time_slice_set(thread_, static_cast<std::int32_t>(slice.count()), expired,
                                user_data);
        return {};
    }
#endif

  private:
    k_thread* thread_;
};

/** Lifecycle transitions the owning wrapper can prove without guessing scheduler state. */
enum class ThreadLifecycleState : std::uint8_t
{
    Empty,
    Prepared,
    Started,
    Finished,
    Aborted,
};

/** Validated native option bits for Solar-owned supervisor threads. */
class ThreadOptions
{
  public:
    [[nodiscard]] static constexpr ThreadOptions none() noexcept
    {
        return ThreadOptions{};
    }

#if defined(K_FP_REGS)
    [[nodiscard]] static constexpr ThreadOptions floating_point() noexcept
    {
        return ThreadOptions{K_FP_REGS};
    }
#endif

    [[nodiscard]] constexpr std::uint32_t native_handle() const noexcept
    {
        return value_;
    }

  private:
    explicit constexpr ThreadOptions(std::uint32_t value = 0) noexcept : value_(value) {}

    std::uint32_t value_{};
};

struct ThreadConfiguration
{
    Priority priority;
    const char* name{};
    ThreadOptions options{ThreadOptions::none()};
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
        ThreadLifecycleState expected = ThreadLifecycleState::Prepared;
        if (!lifecycle_.compare_exchange_strong(expected, ThreadLifecycleState::Started,
                                                std::memory_order_acq_rel)) {
            return fail<Error>({.status = expected == ThreadLifecycleState::Started
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
        return {};
    }

    [[nodiscard]] Result<void> resume() noexcept
    {
        const auto id = id_.load(std::memory_order_acquire);
        if (id == nullptr || !active()) {
            return fail<Error>({.status = Status::NotReady});
        }
        ThreadRef{*id}.resume();
        return {};
    }

    [[nodiscard]] Result<void> join(Timeout timeout = Timeout::forever()) noexcept
    {
        if (id_.load(std::memory_order_acquire) == nullptr) {
            return fail<Error>({.status = Status::NotReady});
        }
        if (lifecycle() == ThreadLifecycleState::Prepared) {
            return fail<Error>({.status = Status::NotReady});
        }
        if (in_isr()) {
            return fail<Error>({.status = Status::Invalid});
        }

        const auto status = ThreadRef{thread_}.join(timeout);
        if (status && lifecycle() != ThreadLifecycleState::Aborted) {
            lifecycle_.store(ThreadLifecycleState::Finished, std::memory_order_release);
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
        if (lifecycle() == ThreadLifecycleState::Prepared) {
            return false;
        }

        const auto result = ThreadRef{const_cast<k_thread&>(thread_)}.exited();
        if (result && *result && lifecycle() == ThreadLifecycleState::Started) {
            lifecycle_.store(ThreadLifecycleState::Finished, std::memory_order_release);
        }
        return result;
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
        lifecycle_.store(ThreadLifecycleState::Aborted, std::memory_order_release);
        return {};
    }

    [[nodiscard]] ThreadLifecycleState lifecycle() const noexcept
    {
        return lifecycle_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool active() const noexcept
    {
        const auto current = lifecycle();
        return current == ThreadLifecycleState::Prepared ||
               current == ThreadLifecycleState::Started;
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
        lifecycle_.store(prepared_only ? ThreadLifecycleState::Prepared
                                       : ThreadLifecycleState::Started,
                         std::memory_order_release);

        const bool controlled_start = prepared_only || delay.is_no_wait();
        const auto native_delay = controlled_start ? K_FOREVER : delay.native_handle();
        const auto id =
            k_thread_create(&thread_, stack_, K_KERNEL_STACK_SIZEOF(stack_), &Thread::trampoline,
                            this, nullptr, nullptr, configuration.priority.native_handle(),
                            configuration.options.native_handle(), native_delay);
        if (id == nullptr) {
            lifecycle_.store(ThreadLifecycleState::Empty, std::memory_order_release);
            return fail<Error>({.status = Status::Error});
        }
        id_.store(id, std::memory_order_release);

        if (configuration.name != nullptr) {
            const auto name_status = detail::map_native(k_thread_name_set(id, configuration.name));
            if (!name_status) {
                k_thread_abort(id);
                lifecycle_.store(ThreadLifecycleState::Aborted, std::memory_order_release);
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
        self.entry_(self.argument_);
        self.lifecycle_.store(ThreadLifecycleState::Finished, std::memory_order_release);
    }

    k_thread thread_{};
    K_KERNEL_STACK_MEMBER(stack_, StackBytes);
    Entry entry_{};
    void* argument_{};
    std::atomic<ThreadId> id_{nullptr};
    mutable std::atomic<ThreadLifecycleState> lifecycle_{ThreadLifecycleState::Empty};
};

} // namespace solar::kernel
