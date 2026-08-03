#pragma once

#include <atomic>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "solar/kernel/poll.hpp"
#include "solar/kernel/work.hpp"

namespace solar::kernel
{

inline constexpr bool triggered_work_available = IS_ENABLED(CONFIG_POLL);

#if defined(CONFIG_POLL)

namespace detail
{

[[nodiscard]] constexpr WorkError triggered_work_error(int native_error) noexcept
{
    if (native_error == -EINVAL) {
        return {
            .status = Status::Busy, .reason = WorkErrorReason::Busy, .native_error = native_error};
    }
    return work_error(native_error);
}

} // namespace detail

class TriggeredWork
{
  public:
    using Handler = void (*)(TriggeredWork&) noexcept;

    explicit TriggeredWork(Handler handler = nullptr) noexcept : handler_(handler)
    {
        k_work_poll_init(&work_, &TriggeredWork::invoke);
    }

    ~TriggeredWork()
    {
        __ASSERT_NO_MSG(!armed() && !k_work_is_pending(&work_.work));
    }

    TriggeredWork(const TriggeredWork&) = delete;
    TriggeredWork& operator=(const TriggeredWork&) = delete;
    TriggeredWork(TriggeredWork&&) = delete;
    TriggeredWork& operator=(TriggeredWork&&) = delete;

    template <std::size_t Capacity>
    [[nodiscard]] Result<void, WorkError> arm(PollSet<Capacity>& events,
                                              Timeout timeout = Timeout::forever()) noexcept
    {
        if (const auto claimed = claim_initial(events); !claimed) {
            return claimed;
        }
        const int result =
            k_work_poll_submit(&work_, events.native_events(), static_cast<int>(events.size()),
                               timeout.native_handle());
        if (result != 0) {
            release_events();
        }
        return submit_result(result);
    }

    template <std::size_t Capacity>
    [[nodiscard]] Result<void, WorkError> arm(PollSet<Capacity>& events, WorkQueueTarget target,
                                              Timeout timeout = Timeout::forever()) noexcept
    {
        if (const auto claimed = claim_initial(events); !claimed) {
            return claimed;
        }
        const int result =
            k_work_poll_submit_to_queue(target.native_queue(), &work_, events.native_events(),
                                        static_cast<int>(events.size()), timeout.native_handle());
        if (result != 0) {
            release_events();
        }
        return submit_result(result);
    }

    template <std::size_t Capacity>
    [[nodiscard]] Result<void, WorkError> replace(PollSet<Capacity>& events,
                                                  Timeout timeout = Timeout::forever()) noexcept
    {
        return replace_on(events, system_work_queue, timeout);
    }

    template <std::size_t Capacity>
    [[nodiscard]] Result<void, WorkError> replace(PollSet<Capacity>& events, WorkQueueTarget target,
                                                  Timeout timeout = Timeout::forever()) noexcept
    {
        return replace_on(events, target, timeout);
    }

    [[nodiscard]] Result<void> cancel_trigger() noexcept
    {
        const int result = k_work_poll_cancel(&work_);
        if (result == 0) {
            release_events();
        }
        if (result == -EINVAL) {
            return fail<Error>({.status = Status::Busy, .native = result});
        }
        return result == 0 ? Result<void>{} : Result<void>{fail<Error>(error_from_errno(result))};
    }

    [[nodiscard]] Result<bool, WorkError> cancel_sync() noexcept
    {
        if (in_isr()) {
            return fail<WorkError>(detail::invalid_work_context());
        }
        if (running_on_current_thread()) {
            return fail<WorkError>(detail::work_deadlock());
        }

        const int trigger_result = k_work_poll_cancel(&work_);
        if (trigger_result != 0 && trigger_result != -EINVAL) {
            return fail<WorkError>(detail::triggered_work_error(trigger_result));
        }

        k_work_sync sync{};
        const bool cancelled = k_work_cancel_sync(&work_.work, &sync);
        release_events();
        return trigger_result == 0 || cancelled;
    }

    [[nodiscard]] Result<bool, WorkError> flush() noexcept
    {
        if (in_isr()) {
            return fail<WorkError>(detail::invalid_work_context());
        }
        if (running_on_current_thread()) {
            return fail<WorkError>(detail::work_deadlock());
        }
        if (armed() && k_work_busy_get(&work_.work) == 0) {
            return fail<WorkError>(detail::triggered_work_error(-EBUSY));
        }
        k_work_sync sync{};
        return k_work_flush(&work_.work, &sync);
    }

    [[nodiscard]] WorkState state() const noexcept
    {
        auto state = static_cast<WorkState>(k_work_busy_get(&work_.work));
        if (armed()) {
            state = state | WorkState::Triggered;
        }
        return state;
    }

    [[nodiscard]] bool pending() const noexcept
    {
        return armed() || k_work_is_pending(&work_.work);
    }

    [[nodiscard]] bool running_on_current_thread() const noexcept
    {
        return handler_thread_.load(std::memory_order_acquire) == k_current_get();
    }

  private:
    [[nodiscard]] bool armed() const noexcept
    {
        return claimed_.load(std::memory_order_acquire);
    }

    template <std::size_t Capacity>
    [[nodiscard]] Result<void, WorkError> claim_initial(PollSet<Capacity>& events) noexcept
    {
        if (events.size() == 0) {
            return fail<WorkError>(detail::invalid_work_events());
        }
        bool expected = false;
        if (!claimed_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            return fail<WorkError>(detail::triggered_work_error(-EBUSY));
        }
        if (!events.claim(this)) {
            claimed_.store(false, std::memory_order_release);
            return fail<WorkError>(detail::triggered_work_error(-EBUSY));
        }
        events.reset_states();
        const auto key = k_spin_lock(&state_lock_);
        bind_events_locked(events);
        k_spin_unlock(&state_lock_, key);
        return {};
    }

    template <std::size_t Capacity>
    [[nodiscard]] Result<void, WorkError>
    replace_on(PollSet<Capacity>& events, WorkQueueTarget target, Timeout timeout) noexcept
    {
        const auto key = k_spin_lock(&state_lock_);
        if (!armed()) {
            k_spin_unlock(&state_lock_, key);
            return fail<WorkError>(
                {.status = Status::NotReady, .reason = WorkErrorReason::Busy, .native_error = 0});
        }

        const bool same_events = events_ == &events;
        if (!same_events && !events.claim(this)) {
            k_spin_unlock(&state_lock_, key);
            return fail<WorkError>(detail::triggered_work_error(-EBUSY));
        }
        events.reset_states();

        const int result =
            k_work_poll_submit_to_queue(target.native_queue(), &work_, events.native_events(),
                                        static_cast<int>(events.size()), timeout.native_handle());
        if (result != 0) {
            if (!same_events) {
                events.release(this);
            }
            k_spin_unlock(&state_lock_, key);
            return fail<WorkError>(detail::triggered_work_error(result));
        }

        if (!same_events) {
            unbind_events_locked();
            bind_events_locked(events);
        }
        k_spin_unlock(&state_lock_, key);
        return {};
    }

    void release_events() noexcept
    {
        const auto key = k_spin_lock(&state_lock_);
        unbind_events_locked();
        claimed_.store(false, std::memory_order_release);
        k_spin_unlock(&state_lock_, key);
    }

    void unbind_events_locked() noexcept
    {
        if (events_ != nullptr && release_events_ != nullptr) {
            release_events_(events_, this);
        }
        events_ = nullptr;
        release_events_ = nullptr;
    }

    template <std::size_t Capacity> void bind_events_locked(PollSet<Capacity>& events) noexcept
    {
        events_ = &events;
        release_events_ = [](void* set, const void* owner) noexcept {
            static_cast<PollSet<Capacity>*>(set)->release(owner);
        };
    }

    [[nodiscard]] static Result<void, WorkError> submit_result(int result) noexcept
    {
        if (result == 0) {
            return {};
        }
        return fail<WorkError>(detail::triggered_work_error(result));
    }

    static void invoke(k_work* work) noexcept
    {
        auto* native = CONTAINER_OF(work, k_work_poll, work);
        auto& self = *CONTAINER_OF(native, TriggeredWork, work_);
        self.handler_thread_.store(k_current_get(), std::memory_order_release);
        if (self.handler_ != nullptr) {
            self.handler_(self);
        }
        self.handler_thread_.store(nullptr, std::memory_order_release);
        self.release_events();
    }

    k_work_poll work_{};
    Handler handler_{};
    std::atomic<k_tid_t> handler_thread_{nullptr};
    std::atomic_bool claimed_{false};
    k_spinlock state_lock_{};
    void* events_{};
    void (*release_events_)(void*, const void*) noexcept {};
};

#else

template <typename> inline constexpr bool triggered_work_dependent_false = false;

class TriggeredWork
{
  public:
    template <typename Disabled = void> TriggeredWork()
    {
        static_assert(triggered_work_dependent_false<Disabled>,
                      "SOLAR_DIAGNOSTIC_TRIGGERED_WORK_DISABLED: enable CONFIG_POLL before using "
                      "triggered work");
    }
};

#endif

} // namespace solar::kernel
