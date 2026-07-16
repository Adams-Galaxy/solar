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
        __ASSERT_NO_MSG(!claimed_.load(std::memory_order_acquire) &&
                        !k_work_is_pending(&work_.work));
    }

    TriggeredWork(const TriggeredWork&) = delete;
    TriggeredWork& operator=(const TriggeredWork&) = delete;
    TriggeredWork(TriggeredWork&&) = delete;
    TriggeredWork& operator=(TriggeredWork&&) = delete;

    template <std::size_t Capacity>
    [[nodiscard]] Result<void, WorkError>
    submit(PollSet<Capacity>& events, Timeout timeout = Timeout::forever()) noexcept
    {
        if (const auto claimed = claim(events); !claimed) {
            return claimed;
        }
        const int result = k_work_poll_submit(&work_, events.native_events(),
                                              static_cast<int>(events.size()),
                                              timeout.native_handle());
        if (result != 0) {
            claimed_.store(false, std::memory_order_release);
        }
        return submit_result(result);
    }

    template <std::size_t Capacity, detail::WorkTarget Target>
    [[nodiscard]] Result<void, WorkError> submit(PollSet<Capacity>& events, const Target& target,
                                                 Timeout timeout = Timeout::forever()) noexcept
    {
        if (const auto claimed = claim(events); !claimed) {
            return claimed;
        }
        const int result = k_work_poll_submit_to_queue(
            target.native_handle(), &work_, events.native_events(), static_cast<int>(events.size()),
            timeout.native_handle());
        if (result != 0) {
            claimed_.store(false, std::memory_order_release);
        }
        return submit_result(result);
    }

    template <std::size_t Capacity>
    [[nodiscard]] Result<void, WorkError>
    try_submit_isr(PollSet<Capacity>& events, Timeout timeout = Timeout::forever()) noexcept
    {
        return submit(events, timeout);
    }

    template <std::size_t Capacity, detail::WorkTarget Target>
    [[nodiscard]] Result<void, WorkError> try_submit_isr(PollSet<Capacity>& events,
                                                         const Target& target,
                                                         Timeout timeout = Timeout::forever())
        noexcept
    {
        return submit(events, target, timeout);
    }

    [[nodiscard]] Status cancel_trigger() noexcept
    {
        const int result = k_work_poll_cancel(&work_);
        if (result == 0) {
            claimed_.store(false, std::memory_order_release);
        }
        if (result == -EINVAL) {
            return Status::Busy;
        }
        return status_from_errno(result);
    }

    [[nodiscard]] Result<bool, WorkError> cancel_sync() noexcept
    {
        if (in_isr()) {
            return fail(detail::invalid_work_context());
        }
        if (running_on_current_thread()) {
            return fail(detail::work_deadlock());
        }

        const int trigger_result = k_work_poll_cancel(&work_);
        if (trigger_result != 0 && trigger_result != -EINVAL) {
            return fail(detail::work_error(trigger_result));
        }

        k_work_sync sync{};
        const bool cancelled = k_work_cancel_sync(&work_.work, &sync);
        claimed_.store(false, std::memory_order_release);
        return trigger_result == 0 || cancelled;
    }

    [[nodiscard]] Result<bool, WorkError> flush() noexcept
    {
        if (in_isr()) {
            return fail(detail::invalid_work_context());
        }
        if (running_on_current_thread()) {
            return fail(detail::work_deadlock());
        }
        if (claimed_.load(std::memory_order_acquire) &&
            k_work_busy_get(&work_.work) == 0) {
            return fail(detail::work_error(-EBUSY));
        }
        k_work_sync sync{};
        return k_work_flush(&work_.work, &sync);
    }

    [[nodiscard]] WorkState state() const noexcept
    {
        auto state = static_cast<WorkState>(k_work_busy_get(&work_.work));
        if (claimed_.load(std::memory_order_acquire)) {
            state = state | WorkState::Triggered;
        }
        return state;
    }

    [[nodiscard]] bool pending() const noexcept
    {
        return claimed_.load(std::memory_order_acquire) || k_work_is_pending(&work_.work);
    }

    [[nodiscard]] bool running_on_current_thread() const noexcept
    {
        return handler_thread_.load(std::memory_order_acquire) == k_current_get();
    }

    [[nodiscard]] k_work_poll* native_handle() noexcept
    {
        return &work_;
    }

    [[nodiscard]] const k_work_poll* native_handle() const noexcept
    {
        return &work_;
    }

  private:
    template <std::size_t Capacity>
    [[nodiscard]] Result<void, WorkError> claim(const PollSet<Capacity>& events) noexcept
    {
        if (events.size() == 0) {
            return fail(detail::invalid_work_events());
        }
        bool expected = false;
        if (!claimed_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            return fail(detail::work_error(-EBUSY));
        }
        return {};
    }

    [[nodiscard]] static Result<void, WorkError> submit_result(int result) noexcept
    {
        if (result == 0) {
            return {};
        }
        return fail(detail::work_error(result));
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
        self.claimed_.store(false, std::memory_order_release);
    }

    k_work_poll work_{};
    Handler handler_{};
    std::atomic<k_tid_t> handler_thread_{nullptr};
    std::atomic_bool claimed_{false};
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
