#pragma once

#include <atomic>
#include <concepts>
#include <utility>

#include <zephyr/kernel.h>
#include <zephyr/sys/__assert.h>

#include "solar/core/status.hpp"
#include "solar/kernel/deadline.hpp"
#include "solar/kernel/error.hpp"
#include "solar/kernel/interrupt.hpp"

namespace solar::kernel
{

class ConditionVariable;

class Mutex
{
  public:
    Mutex() noexcept
    {
        __ASSERT_NO_MSG(k_mutex_init(&mutex_) == 0);
    }

    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;
    Mutex(Mutex&&) = delete;
    Mutex& operator=(Mutex&&) = delete;

    [[nodiscard]] Status lock(Timeout timeout = Timeout::forever()) noexcept
    {
        if (in_isr()) {
            return Status::Invalid;
        }

        const auto current = k_current_get();
        if (owner_.load(std::memory_order_relaxed) == current) {
            return Status::Deadlock;
        }

        const auto status = detail::map_wait(k_mutex_lock(&mutex_, timeout.native_handle()),
                                             timeout, Status::WouldBlock);
        if (status == Status::Ok) {
            owner_.store(current, std::memory_order_release);
        }
        return status;
    }

    [[nodiscard]] Status lock(const Deadline& deadline) noexcept
    {
        return lock(deadline.remaining());
    }

    [[nodiscard]] Status try_lock() noexcept
    {
        return lock(Timeout::no_wait());
    }

    [[nodiscard]] Status unlock() noexcept
    {
        if (in_isr()) {
            return Status::Invalid;
        }
        if (owner_.load(std::memory_order_acquire) != k_current_get()) {
            return Status::PermissionDenied;
        }

        const auto current = k_current_get();
        owner_.store(nullptr, std::memory_order_release);
        const auto status = detail::map_native(k_mutex_unlock(&mutex_));
        if (status != Status::Ok) {
            owner_.store(current, std::memory_order_release);
        }
        return status;
    }

    [[nodiscard]] k_mutex* native_handle() noexcept
    {
        return &mutex_;
    }

    [[nodiscard]] const k_mutex* native_handle() const noexcept
    {
        return &mutex_;
    }

  private:
    [[nodiscard]] Status begin_condition_wait() noexcept
    {
        if (owner_.load(std::memory_order_acquire) != k_current_get()) {
            return Status::PermissionDenied;
        }
        owner_.store(nullptr, std::memory_order_release);
        return Status::Ok;
    }

    void end_condition_wait() noexcept
    {
        owner_.store(k_current_get(), std::memory_order_release);
    }

    friend class ConditionVariable;

    k_mutex mutex_{};
    std::atomic<k_tid_t> owner_{nullptr};
};

class RecursiveMutex
{
  public:
    RecursiveMutex() noexcept
    {
        __ASSERT_NO_MSG(k_mutex_init(&mutex_) == 0);
    }

    RecursiveMutex(const RecursiveMutex&) = delete;
    RecursiveMutex& operator=(const RecursiveMutex&) = delete;
    RecursiveMutex(RecursiveMutex&&) = delete;
    RecursiveMutex& operator=(RecursiveMutex&&) = delete;

    [[nodiscard]] Status lock(Timeout timeout = Timeout::forever()) noexcept
    {
        if (in_isr()) {
            return Status::Invalid;
        }
        return detail::map_wait(k_mutex_lock(&mutex_, timeout.native_handle()), timeout,
                                Status::WouldBlock);
    }

    [[nodiscard]] Status lock(const Deadline& deadline) noexcept
    {
        return lock(deadline.remaining());
    }

    [[nodiscard]] Status try_lock() noexcept
    {
        return lock(Timeout::no_wait());
    }

    [[nodiscard]] Status unlock() noexcept
    {
        if (in_isr()) {
            return Status::Invalid;
        }
        return detail::map_native(k_mutex_unlock(&mutex_));
    }

    [[nodiscard]] k_mutex* native_handle() noexcept
    {
        return &mutex_;
    }

    [[nodiscard]] const k_mutex* native_handle() const noexcept
    {
        return &mutex_;
    }

  private:
    k_mutex mutex_{};
};

template <typename T>
concept Lockable = requires(T& mutex, Timeout timeout) {
    { mutex.lock(timeout) } -> std::same_as<Status>;
    { mutex.try_lock() } -> std::same_as<Status>;
    { mutex.unlock() } -> std::same_as<Status>;
};

template <Lockable MutexType> class LockGuard
{
  public:
    [[nodiscard]] static Result<LockGuard> acquire(MutexType& mutex,
                                                   Timeout timeout = Timeout::forever()) noexcept
    {
        const auto status = mutex.lock(timeout);
        if (status != Status::Ok) {
            return fail(status);
        }
        return LockGuard{mutex};
    }

    ~LockGuard()
    {
        if (mutex_ != nullptr) {
            (void)mutex_->unlock();
        }
    }

    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;

    LockGuard(LockGuard&& other) noexcept : mutex_(std::exchange(other.mutex_, nullptr)) {}

    LockGuard& operator=(LockGuard&&) = delete;

  private:
    explicit LockGuard(MutexType& mutex) noexcept : mutex_(&mutex) {}

    MutexType* mutex_{};
};

template <Lockable MutexType> class UniqueLock
{
  public:
    UniqueLock() = default;

    explicit UniqueLock(MutexType& mutex) noexcept : mutex_(&mutex) {}

    [[nodiscard]] static Result<UniqueLock> acquire(MutexType& mutex,
                                                    Timeout timeout = Timeout::forever()) noexcept
    {
        UniqueLock lock{mutex};
        const auto status = lock.lock(timeout);
        if (status != Status::Ok) {
            return fail(status);
        }
        return lock;
    }

    ~UniqueLock()
    {
        if (owns_) {
            (void)mutex_->unlock();
        }
    }

    UniqueLock(const UniqueLock&) = delete;
    UniqueLock& operator=(const UniqueLock&) = delete;

    UniqueLock(UniqueLock&& other) noexcept
        : mutex_(std::exchange(other.mutex_, nullptr)), owns_(std::exchange(other.owns_, false))
    {}

    UniqueLock& operator=(UniqueLock&& other) noexcept
    {
        if (this == &other) {
            return *this;
        }
        if (owns_) {
            (void)mutex_->unlock();
        }
        mutex_ = std::exchange(other.mutex_, nullptr);
        owns_ = std::exchange(other.owns_, false);
        return *this;
    }

    [[nodiscard]] Status lock(Timeout timeout = Timeout::forever()) noexcept
    {
        if (mutex_ == nullptr) {
            return Status::Invalid;
        }
        if (owns_) {
            return Status::Already;
        }
        const auto status = mutex_->lock(timeout);
        owns_ = status == Status::Ok;
        return status;
    }

    [[nodiscard]] Status try_lock() noexcept
    {
        return lock(Timeout::no_wait());
    }

    [[nodiscard]] Status unlock() noexcept
    {
        if (mutex_ == nullptr || !owns_) {
            return Status::Invalid;
        }
        const auto status = mutex_->unlock();
        if (status == Status::Ok) {
            owns_ = false;
        }
        return status;
    }

    [[nodiscard]] bool owns_lock() const noexcept
    {
        return owns_;
    }

    explicit operator bool() const noexcept
    {
        return owns_lock();
    }

    [[nodiscard]] MutexType* mutex() const noexcept
    {
        return mutex_;
    }

    [[nodiscard]] MutexType* release() noexcept
    {
        owns_ = false;
        return std::exchange(mutex_, nullptr);
    }

  private:
    MutexType* mutex_{};
    bool owns_ = false;
};

template <Lockable MutexType>
[[nodiscard]] Result<LockGuard<MutexType>> lock_guard(MutexType& mutex,
                                                      Timeout timeout = Timeout::forever()) noexcept
{
    return LockGuard<MutexType>::acquire(mutex, timeout);
}

template <Lockable MutexType>
[[nodiscard]] Result<UniqueLock<MutexType>>
unique_lock(MutexType& mutex, Timeout timeout = Timeout::forever()) noexcept
{
    return UniqueLock<MutexType>::acquire(mutex, timeout);
}

} // namespace solar::kernel
