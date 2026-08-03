#pragma once

#include <atomic>

#include <zephyr/kernel.h>
#include <zephyr/sys/__assert.h>

#include "solar/core/status.hpp"
#include "solar/kernel/deadline.hpp"
#include "solar/kernel/error.hpp"
#include "solar/kernel/interrupt.hpp"

namespace solar::kernel
{

namespace detail
{

struct StopState
{
    StopState() noexcept
    {
        const int mutex_result = k_mutex_init(&mutex);
        const int condition_result = k_condvar_init(&condition);
        __ASSERT_NO_MSG(mutex_result == 0);
        __ASSERT_NO_MSG(condition_result == 0);
        (void)mutex_result;
        (void)condition_result;
    }

    std::atomic_bool requested{false};
    k_mutex mutex{};
    k_condvar condition{};
};

} // namespace detail

class StopToken
{
  public:
    StopToken() = default;

    [[nodiscard]] bool stop_possible() const noexcept
    {
        return state_ != nullptr;
    }

    [[nodiscard]] bool stop_requested() const noexcept
    {
        return state_ != nullptr && state_->requested.load(std::memory_order_acquire);
    }

    [[nodiscard]] Result<void> wait(Timeout timeout = Timeout::forever()) const noexcept
    {
        if (state_ == nullptr) {
            return fail<Error>({.status = Status::NotSupported});
        }
        if (stop_requested()) {
            return {};
        }
        if (in_isr()) {
            return fail<Error>({.status = Status::Invalid});
        }
        if (timeout.is_no_wait()) {
            return fail<Error>({.status = Status::WouldBlock});
        }

        const auto deadline = Deadline::after(timeout);
        const int lock_result = k_mutex_lock(&state_->mutex, K_FOREVER);
        if (lock_result != 0) {
            return fail<Error>(error_from_errno(lock_result));
        }

        Result<void> status{};
        while (!state_->requested.load(std::memory_order_acquire)) {
            const auto remaining = deadline.remaining();
            const int result =
                k_condvar_wait(&state_->condition, &state_->mutex, remaining.native_handle());
            if (result != 0) {
                status = detail::map_wait(result, remaining, Status::WouldBlock);
                break;
            }
        }

        const int unlock_result = k_mutex_unlock(&state_->mutex);
        if (status && unlock_result != 0) {
            status = fail<Error>(error_from_errno(unlock_result));
        }
        return status;
    }

    template <typename Rep, typename Period>
    [[nodiscard]] Result<void> wait(std::chrono::duration<Rep, Period> timeout) const noexcept
    {
        return wait(Timeout::after(timeout));
    }

    [[nodiscard]] Result<void> wait(const Deadline& deadline) const noexcept
    {
        return wait(deadline.remaining());
    }

  private:
    explicit StopToken(detail::StopState& state) noexcept : state_(&state) {}

    friend class StopSource;

    detail::StopState* state_{};
};

class StopSource
{
  public:
    StopSource() = default;

    StopSource(const StopSource&) = delete;
    StopSource& operator=(const StopSource&) = delete;
    StopSource(StopSource&&) = delete;
    StopSource& operator=(StopSource&&) = delete;

    [[nodiscard]] StopToken token() noexcept
    {
        return StopToken{state_};
    }

    [[nodiscard]] bool stop_requested() const noexcept
    {
        return state_.requested.load(std::memory_order_acquire);
    }

    [[nodiscard]] Result<bool> request_stop() noexcept
    {
        if (in_isr()) {
            return fail<solar::Error>({.status = solar::Status::Invalid});
        }

        bool expected = false;
        if (!state_.requested.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            return false;
        }

        const int lock_result = k_mutex_lock(&state_.mutex, K_FOREVER);
        if (lock_result != 0) {
            return fail<Error>(error_from_errno(lock_result));
        }
        const int woken = k_condvar_broadcast(&state_.condition);
        const int unlock_result = k_mutex_unlock(&state_.mutex);
        if (woken < 0) {
            return fail<Error>(error_from_errno(woken));
        }
        if (unlock_result != 0) {
            return fail<Error>(error_from_errno(unlock_result));
        }
        return true;
    }

    /** Re-arm a stopped source after every user of its previous token exited. */
    [[nodiscard]] Result<void> reset() noexcept
    {
        if (in_isr()) {
            return fail<solar::Error>({.status = solar::Status::Invalid});
        }
        const int lock_result = k_mutex_lock(&state_.mutex, K_FOREVER);
        if (lock_result != 0) {
            return fail<Error>(error_from_errno(lock_result));
        }
        state_.requested.store(false, std::memory_order_release);
        const int unlock_result = k_mutex_unlock(&state_.mutex);
        return unlock_result == 0 ? Result<void>{}
                                  : Result<void>{fail<Error>(error_from_errno(unlock_result))};
    }

  private:
    detail::StopState state_{};
};

} // namespace solar::kernel

namespace solar
{

using StopToken = kernel::StopToken;

} // namespace solar
