#pragma once

#include <cstddef>
#include <utility>

#include <zephyr/kernel.h>
#include <zephyr/sys/__assert.h>

#include "solar/core/status.hpp"
#include "solar/kernel/deadline.hpp"
#include "solar/kernel/error.hpp"
#include "solar/kernel/interrupt.hpp"
#include "solar/kernel/mutex.hpp"

namespace solar::kernel
{

class ConditionVariable
{
  public:
    ConditionVariable() noexcept
    {
        __ASSERT_NO_MSG(k_condvar_init(&condition_) == 0);
    }

    ConditionVariable(const ConditionVariable&) = delete;
    ConditionVariable& operator=(const ConditionVariable&) = delete;
    ConditionVariable(ConditionVariable&&) = delete;
    ConditionVariable& operator=(ConditionVariable&&) = delete;

    [[nodiscard]] Result<void> notify_one() noexcept
    {
        if (in_isr()) {
            return fail<Error>({.status = Status::Invalid});
        }
        return detail::map_native(k_condvar_signal(&condition_));
    }

    [[nodiscard]] Result<std::size_t> notify_all() noexcept
    {
        if (in_isr()) {
            return fail<solar::Error>({.status = solar::Status::Invalid});
        }
        const int result = k_condvar_broadcast(&condition_);
        if (result < 0) {
            return fail<Error>(error_from_errno(result));
        }
        return static_cast<std::size_t>(result);
    }

    [[nodiscard]] Result<void> wait(UniqueLock<Mutex>& lock,
                                    Timeout timeout = Timeout::forever()) noexcept
    {
        if (in_isr()) {
            return fail<Error>({.status = Status::Invalid});
        }
        if (!lock.owns_lock() || lock.mutex() == nullptr) {
            return fail<Error>({.status = Status::PermissionDenied});
        }

        auto& mutex = *lock.mutex();
        const auto begin = mutex.begin_condition_wait();
        if (!begin) {
            return begin;
        }

        const int result =
            k_condvar_wait(&condition_, mutex.native_handle(), timeout.native_handle());
        mutex.end_condition_wait();
        return detail::map_wait(result, timeout, Status::WouldBlock);
    }

    template <typename Rep, typename Period>
    [[nodiscard]] Result<void> wait(UniqueLock<Mutex>& lock,
                                    std::chrono::duration<Rep, Period> timeout) noexcept
    {
        return wait(lock, Timeout::after(timeout));
    }

    [[nodiscard]] Result<void> wait(UniqueLock<Mutex>& lock, const Deadline& deadline) noexcept
    {
        return wait(lock, deadline.remaining());
    }

    template <typename Predicate>
    [[nodiscard]] Result<void> wait(UniqueLock<Mutex>& lock, Predicate&& predicate,
                                    Timeout timeout = Timeout::forever()) noexcept
    {
        if (timeout.is_no_wait()) {
            return predicate() ? Result<void>{}
                               : Result<void>{fail<Error>({.status = Status::WouldBlock})};
        }

        const auto deadline = Deadline::after(timeout);
        while (!predicate()) {
            const auto status = wait(lock, deadline);
            if (!status) {
                return status;
            }
        }
        return {};
    }

    template <typename Predicate, typename Rep, typename Period>
    [[nodiscard]] Result<void> wait(UniqueLock<Mutex>& lock, Predicate&& predicate,
                                    std::chrono::duration<Rep, Period> timeout) noexcept
    {
        return wait(lock, std::forward<Predicate>(predicate), Timeout::after(timeout));
    }

    [[nodiscard]] k_condvar* native_handle() noexcept
    {
        return &condition_;
    }

    [[nodiscard]] const k_condvar* native_handle() const noexcept
    {
        return &condition_;
    }

  private:
    k_condvar condition_{};
};

} // namespace solar::kernel
