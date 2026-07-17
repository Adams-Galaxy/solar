#pragma once

#include <atomic>
#include <chrono>
#include <concepts>
#include <cstdint>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "solar/core/status.hpp"
#include "solar/kernel/interrupt.hpp"
#include "solar/kernel/time.hpp"

namespace solar::kernel
{

enum class WorkSubmission : std::uint8_t
{
    AlreadyQueued,
    Queued,
    RequeuedAfterCurrent,
};

enum class WorkErrorReason : std::uint8_t
{
    Busy,
    InvalidQueue,
    InvalidEvents,
    QueueNotStarted,
    DifferentQueue,
    InvalidContext,
    Deadlock,
    Native,
};

struct WorkError
{
    Status status{Status::Error};
    WorkErrorReason reason{WorkErrorReason::Native};
    int native_error{};
};

enum class WorkState : std::uint32_t
{
    Idle = 0,
    Running = K_WORK_RUNNING,
    Cancelling = K_WORK_CANCELING,
    Queued = K_WORK_QUEUED,
    Delayed = K_WORK_DELAYED,
    Flushing = K_WORK_FLUSHING,
    Triggered = (1U << 31U),
};

[[nodiscard]] constexpr WorkState operator|(WorkState left, WorkState right) noexcept
{
    return static_cast<WorkState>(static_cast<std::uint32_t>(left) |
                                  static_cast<std::uint32_t>(right));
}

[[nodiscard]] constexpr bool has_state(WorkState value, WorkState flag) noexcept
{
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0;
}

namespace detail
{

[[nodiscard]] constexpr WorkError work_error(int native_error) noexcept
{
    switch (native_error) {
    case -EBUSY:
        return {.status = solar::Status::Busy,
                .reason = WorkErrorReason::Busy,
                .native_error = native_error};
    case -EINVAL:
        return {.status = solar::Status::Invalid,
                .reason = WorkErrorReason::InvalidQueue,
                .native_error = native_error};
    case -ENODEV:
        return {.status = solar::Status::NotReady,
                .reason = WorkErrorReason::QueueNotStarted,
                .native_error = native_error};
    case -EADDRINUSE:
        return {.status = solar::Status::Already,
                .reason = WorkErrorReason::DifferentQueue,
                .native_error = native_error};
    default:
        return {.status = status_from_errno(native_error),
                .reason = WorkErrorReason::Native,
                .native_error = native_error};
    }
}

[[nodiscard]] inline Result<WorkSubmission, WorkError> work_submission(int result) noexcept
{
    switch (result) {
    case 0:
        return WorkSubmission::AlreadyQueued;
    case 1:
        return WorkSubmission::Queued;
    case 2:
        return WorkSubmission::RequeuedAfterCurrent;
    default:
        return fail<WorkError>(work_error(result));
    }
}

[[nodiscard]] constexpr WorkError invalid_work_context() noexcept
{
    return {.status = solar::Status::Invalid,
            .reason = WorkErrorReason::InvalidContext,
            .native_error = 0};
}

[[nodiscard]] constexpr WorkError invalid_work_events() noexcept
{
    return {.status = solar::Status::Invalid,
            .reason = WorkErrorReason::InvalidEvents,
            .native_error = 0};
}

[[nodiscard]] constexpr WorkError work_deadlock() noexcept
{
    return {
        .status = solar::Status::Deadlock, .reason = WorkErrorReason::Deadlock, .native_error = 0};
}

template <typename Target>
concept WorkTarget = requires(const Target& target) {
    { target.native_handle() } -> std::same_as<k_work_q*>;
};

} // namespace detail

class Work
{
  public:
    using Handler = void (*)(Work&) noexcept;

    explicit Work(Handler handler = nullptr) noexcept : handler_(handler)
    {
        k_work_init(&work_, &Work::invoke);
    }

    ~Work()
    {
        __ASSERT_NO_MSG(!pending());
    }

    Work(const Work&) = delete;
    Work& operator=(const Work&) = delete;
    Work(Work&&) = delete;
    Work& operator=(Work&&) = delete;

    [[nodiscard]] Result<WorkSubmission, WorkError> submit() noexcept
    {
        return detail::work_submission(k_work_submit(&work_));
    }

    template <detail::WorkTarget Target>
    [[nodiscard]] Result<WorkSubmission, WorkError> submit(const Target& target) noexcept
    {
        return detail::work_submission(k_work_submit_to_queue(target.native_handle(), &work_));
    }

    [[nodiscard]] Result<WorkSubmission, WorkError> try_submit_isr() noexcept
    {
        return submit();
    }

    template <detail::WorkTarget Target>
    [[nodiscard]] Result<WorkSubmission, WorkError> try_submit_isr(const Target& target) noexcept
    {
        return submit(target);
    }

    [[nodiscard]] WorkState cancel() noexcept
    {
        return static_cast<WorkState>(k_work_cancel(&work_));
    }

    [[nodiscard]] WorkState try_cancel_isr() noexcept
    {
        return cancel();
    }

    [[nodiscard]] Result<bool, WorkError> cancel_sync() noexcept
    {
        if (in_isr()) {
            return fail<WorkError>(detail::invalid_work_context());
        }
        if (running_on_current_thread()) {
            return fail<WorkError>(detail::work_deadlock());
        }
        k_work_sync sync{};
        return k_work_cancel_sync(&work_, &sync);
    }

    [[nodiscard]] Result<bool, WorkError> flush() noexcept
    {
        if (in_isr()) {
            return fail<WorkError>(detail::invalid_work_context());
        }
        if (running_on_current_thread()) {
            return fail<WorkError>(detail::work_deadlock());
        }
        k_work_sync sync{};
        return k_work_flush(&work_, &sync);
    }

    [[nodiscard]] WorkState state() const noexcept
    {
        return static_cast<WorkState>(k_work_busy_get(&work_));
    }

    [[nodiscard]] bool pending() const noexcept
    {
        return k_work_is_pending(&work_);
    }

    [[nodiscard]] bool running_on_current_thread() const noexcept
    {
        return handler_thread_.load(std::memory_order_acquire) == k_current_get();
    }

    [[nodiscard]] k_work* native_handle() noexcept
    {
        return &work_;
    }

    [[nodiscard]] const k_work* native_handle() const noexcept
    {
        return &work_;
    }

  private:
    static void invoke(k_work* work) noexcept
    {
        auto& self = *CONTAINER_OF(work, Work, work_);
        self.handler_thread_.store(k_current_get(), std::memory_order_release);
        if (self.handler_ != nullptr) {
            self.handler_(self);
        }
        self.handler_thread_.store(nullptr, std::memory_order_release);
    }

    k_work work_{};
    Handler handler_{};
    std::atomic<k_tid_t> handler_thread_{nullptr};
};

class DelayableWork
{
  public:
    using Handler = void (*)(DelayableWork&) noexcept;

    explicit DelayableWork(Handler handler = nullptr) noexcept : handler_(handler)
    {
        k_work_init_delayable(&work_, &DelayableWork::invoke);
    }

    ~DelayableWork()
    {
        __ASSERT_NO_MSG(!pending());
    }

    DelayableWork(const DelayableWork&) = delete;
    DelayableWork& operator=(const DelayableWork&) = delete;
    DelayableWork(DelayableWork&&) = delete;
    DelayableWork& operator=(DelayableWork&&) = delete;

    [[nodiscard]] Result<WorkSubmission, WorkError>
    schedule(Timeout delay = Timeout::no_wait()) noexcept
    {
        return detail::work_submission(k_work_schedule(&work_, delay.native_handle()));
    }

    template <typename Rep, typename Period>
    [[nodiscard]] Result<WorkSubmission, WorkError>
    schedule(std::chrono::duration<Rep, Period> delay) noexcept
    {
        return schedule(Timeout::after(delay));
    }

    template <detail::WorkTarget Target>
    [[nodiscard]] Result<WorkSubmission, WorkError> schedule(const Target& target,
                                                             Timeout delay) noexcept
    {
        return detail::work_submission(
            k_work_schedule_for_queue(target.native_handle(), &work_, delay.native_handle()));
    }

    template <detail::WorkTarget Target, typename Rep, typename Period>
    [[nodiscard]] Result<WorkSubmission, WorkError>
    schedule(const Target& target, std::chrono::duration<Rep, Period> delay) noexcept
    {
        return schedule(target, Timeout::after(delay));
    }

    [[nodiscard]] Result<WorkSubmission, WorkError>
    reschedule(Timeout delay = Timeout::no_wait()) noexcept
    {
        return detail::work_submission(k_work_reschedule(&work_, delay.native_handle()));
    }

    template <typename Rep, typename Period>
    [[nodiscard]] Result<WorkSubmission, WorkError>
    reschedule(std::chrono::duration<Rep, Period> delay) noexcept
    {
        return reschedule(Timeout::after(delay));
    }

    template <detail::WorkTarget Target>
    [[nodiscard]] Result<WorkSubmission, WorkError> reschedule(const Target& target,
                                                               Timeout delay) noexcept
    {
        return detail::work_submission(
            k_work_reschedule_for_queue(target.native_handle(), &work_, delay.native_handle()));
    }

    template <detail::WorkTarget Target, typename Rep, typename Period>
    [[nodiscard]] Result<WorkSubmission, WorkError>
    reschedule(const Target& target, std::chrono::duration<Rep, Period> delay) noexcept
    {
        return reschedule(target, Timeout::after(delay));
    }

    [[nodiscard]] WorkState cancel() noexcept
    {
        return static_cast<WorkState>(k_work_cancel_delayable(&work_));
    }

    [[nodiscard]] WorkState try_cancel_isr() noexcept
    {
        return cancel();
    }

    [[nodiscard]] Result<bool, WorkError> cancel_sync() noexcept
    {
        if (in_isr()) {
            return fail<WorkError>(detail::invalid_work_context());
        }
        if (running_on_current_thread()) {
            return fail<WorkError>(detail::work_deadlock());
        }
        k_work_sync sync{};
        return k_work_cancel_delayable_sync(&work_, &sync);
    }

    [[nodiscard]] Result<bool, WorkError> flush() noexcept
    {
        if (in_isr()) {
            return fail<WorkError>(detail::invalid_work_context());
        }
        if (running_on_current_thread()) {
            return fail<WorkError>(detail::work_deadlock());
        }
        k_work_sync sync{};
        return k_work_flush_delayable(&work_, &sync);
    }

    [[nodiscard]] WorkState state() const noexcept
    {
        return static_cast<WorkState>(k_work_delayable_busy_get(&work_));
    }

    [[nodiscard]] bool pending() const noexcept
    {
        return k_work_delayable_is_pending(&work_);
    }

    [[nodiscard]] bool running_on_current_thread() const noexcept
    {
        return handler_thread_.load(std::memory_order_acquire) == k_current_get();
    }

    [[nodiscard]] TickDuration remaining() const noexcept
    {
        return from_ticks(static_cast<Tick>(k_work_delayable_remaining_get(&work_)));
    }

    [[nodiscard]] TimePoint expires_at() const noexcept
    {
        return TimePoint{TickDuration{static_cast<Tick>(k_work_delayable_expires_get(&work_))}};
    }

    [[nodiscard]] k_work_delayable* native_handle() noexcept
    {
        return &work_;
    }

    [[nodiscard]] const k_work_delayable* native_handle() const noexcept
    {
        return &work_;
    }

  private:
    static void invoke(k_work* work) noexcept
    {
        auto* native = k_work_delayable_from_work(work);
        auto& self = *CONTAINER_OF(native, DelayableWork, work_);
        self.handler_thread_.store(k_current_get(), std::memory_order_release);
        if (self.handler_ != nullptr) {
            self.handler_(self);
        }
        self.handler_thread_.store(nullptr, std::memory_order_release);
    }

    k_work_delayable work_{};
    Handler handler_{};
    std::atomic<k_tid_t> handler_thread_{nullptr};
};

} // namespace solar::kernel
