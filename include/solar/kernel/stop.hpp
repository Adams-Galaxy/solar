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
        __ASSERT_NO_MSG(k_mutex_init(&mutex) == 0);
        __ASSERT_NO_MSG(k_condvar_init(&condition) == 0);
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

    [[nodiscard]] Status wait(Timeout timeout = Timeout::forever()) const noexcept
    {
        if (state_ == nullptr) {
            return Status::NotSupported;
        }
        if (stop_requested()) {
            return Status::Ok;
        }
        if (in_isr()) {
            return Status::Invalid;
        }
        if (timeout.is_no_wait()) {
            return Status::WouldBlock;
        }

        const auto deadline = Deadline::after(timeout);
        const int lock_result = k_mutex_lock(&state_->mutex, K_FOREVER);
        if (lock_result != 0) {
            return status_from_errno(lock_result);
        }

        Status status = Status::Ok;
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
        if (status == Status::Ok && unlock_result != 0) {
            status = status_from_errno(unlock_result);
        }
        return status;
    }

    template <typename Rep, typename Period>
    [[nodiscard]] Status wait(std::chrono::duration<Rep, Period> timeout) const noexcept
    {
        return wait(Timeout::after(timeout));
    }

    [[nodiscard]] Status wait(const Deadline& deadline) const noexcept
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
            return fail(Status::Invalid);
        }

        bool expected = false;
        if (!state_.requested.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            return false;
        }

        const int lock_result = k_mutex_lock(&state_.mutex, K_FOREVER);
        if (lock_result != 0) {
            return fail(status_from_errno(lock_result));
        }
        const int woken = k_condvar_broadcast(&state_.condition);
        const int unlock_result = k_mutex_unlock(&state_.mutex);
        if (woken < 0) {
            return fail(status_from_errno(woken));
        }
        if (unlock_result != 0) {
            return fail(status_from_errno(unlock_result));
        }
        return true;
    }

  private:
    detail::StopState state_{};
};

} // namespace solar::kernel

namespace solar
{

using StopToken = kernel::StopToken;

} // namespace solar
