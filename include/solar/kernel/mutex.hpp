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
class ConditionVariableRef;

/** Non-owning access to an initialized recursive Zephyr mutex. */
class RecursiveMutexRef
{
  public:
    explicit constexpr RecursiveMutexRef(k_mutex& mutex) : mutex_(&mutex) {}

    [[nodiscard]] Result<void> lock(Timeout timeout = Timeout::forever()) const
    {
        if (in_isr()) {
            return fail<Error>({.status = Status::Invalid});
        }
        return detail::map_wait(k_mutex_lock(mutex_, timeout.native_handle()), timeout,
                                Status::WouldBlock);
    }

    [[nodiscard]] Result<void> lock(const Deadline& deadline) const
    {
        return lock(deadline.remaining());
    }

    [[nodiscard]] Result<void> try_lock() const
    {
        return lock(Timeout::no_wait());
    }

    [[nodiscard]] Result<void> unlock() const
    {
        if (in_isr()) {
            return fail<Error>({.status = Status::Invalid});
        }
        return detail::map_native(k_mutex_unlock(mutex_));
    }

    [[nodiscard]] constexpr k_mutex* native_handle() const
    {
        return mutex_;
    }

  private:
    k_mutex* mutex_;
};

class Mutex
{
  public:
    Mutex()
    {
        const int result = k_mutex_init(&mutex_);
        __ASSERT_NO_MSG(result == 0);
        (void)result;
    }

    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;
    Mutex(Mutex&&) = delete;
    Mutex& operator=(Mutex&&) = delete;

    [[nodiscard]] Result<void> lock(Timeout timeout = Timeout::forever())
    {
        if (in_isr()) {
            return fail<Error>({.status = Status::Invalid});
        }

        const auto current = k_current_get();
        if (owner_.load(std::memory_order_relaxed) == current) {
            return fail<Error>({.status = Status::Deadlock});
        }

        const auto status = detail::map_wait(k_mutex_lock(&mutex_, timeout.native_handle()),
                                             timeout, Status::WouldBlock);
        if (status) {
            owner_.store(current, std::memory_order_release);
        }
        return status;
    }

    [[nodiscard]] Result<void> lock(const Deadline& deadline)
    {
        return lock(deadline.remaining());
    }

    [[nodiscard]] Result<void> try_lock()
    {
        return lock(Timeout::no_wait());
    }

    [[nodiscard]] Result<void> unlock()
    {
        if (in_isr()) {
            return fail<Error>({.status = Status::Invalid});
        }
        if (owner_.load(std::memory_order_acquire) != k_current_get()) {
            return fail<Error>({.status = Status::PermissionDenied});
        }

        const auto current = k_current_get();
        owner_.store(nullptr, std::memory_order_release);
        const auto status = detail::map_native(k_mutex_unlock(&mutex_));
        if (!status) {
            owner_.store(current, std::memory_order_release);
        }
        return status;
    }

  private:
    [[nodiscard]] Result<void> begin_condition_wait()
    {
        if (owner_.load(std::memory_order_acquire) != k_current_get()) {
            return fail<Error>({.status = Status::PermissionDenied});
        }
        owner_.store(nullptr, std::memory_order_release);
        return {};
    }

    void end_condition_wait()
    {
        owner_.store(k_current_get(), std::memory_order_release);
    }

    [[nodiscard]] k_mutex* native_for_condition()
    {
        return &mutex_;
    }

    friend class ConditionVariable;
    friend class ConditionVariableRef;

    k_mutex mutex_{};
    std::atomic<k_tid_t> owner_{nullptr};
};

class RecursiveMutex
{
  public:
    RecursiveMutex()
    {
        const int result = k_mutex_init(&mutex_);
        __ASSERT_NO_MSG(result == 0);
        (void)result;
    }

    RecursiveMutex(const RecursiveMutex&) = delete;
    RecursiveMutex& operator=(const RecursiveMutex&) = delete;
    RecursiveMutex(RecursiveMutex&&) = delete;
    RecursiveMutex& operator=(RecursiveMutex&&) = delete;

    [[nodiscard]] Result<void> lock(Timeout timeout = Timeout::forever())
    {
        return ref().lock(timeout);
    }

    [[nodiscard]] Result<void> lock(const Deadline& deadline)
    {
        return lock(deadline.remaining());
    }

    [[nodiscard]] Result<void> try_lock()
    {
        return lock(Timeout::no_wait());
    }

    [[nodiscard]] Result<void> unlock()
    {
        return ref().unlock();
    }

    [[nodiscard]] RecursiveMutexRef ref()
    {
        return RecursiveMutexRef{mutex_};
    }

  private:
    k_mutex mutex_{};
};

template <typename T>
concept Lockable = requires(T& mutex, Timeout timeout) {
    { mutex.lock(timeout) } -> std::same_as<Result<void>>;
    { mutex.try_lock() } -> std::same_as<Result<void>>;
    { mutex.unlock() } -> std::same_as<Result<void>>;
};

template <Lockable MutexType> class LockGuard
{
  public:
    [[nodiscard]] static Result<LockGuard> acquire(MutexType& mutex,
                                                   Timeout timeout = Timeout::forever())
    {
        const auto status = mutex.lock(timeout);
        if (!status) {
            return fail<Error>(status.error());
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
    explicit LockGuard(MutexType& mutex) : mutex_(&mutex) {}

    MutexType* mutex_{};
};

template <Lockable MutexType> class UniqueLock
{
  public:
    UniqueLock() = default;

    explicit UniqueLock(MutexType& mutex) : mutex_(&mutex) {}

    [[nodiscard]] static Result<UniqueLock> acquire(MutexType& mutex,
                                                    Timeout timeout = Timeout::forever())
    {
        UniqueLock lock{mutex};
        const auto status = lock.lock(timeout);
        if (!status) {
            return fail<Error>(status.error());
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

    [[nodiscard]] Result<void> lock(Timeout timeout = Timeout::forever())
    {
        if (mutex_ == nullptr) {
            return fail<Error>({.status = Status::Invalid});
        }
        if (owns_) {
            return fail<Error>({.status = Status::Already});
        }
        const auto status = mutex_->lock(timeout);
        owns_ = status.has_value();
        return status;
    }

    [[nodiscard]] Result<void> try_lock()
    {
        return lock(Timeout::no_wait());
    }

    [[nodiscard]] Result<void> unlock()
    {
        if (mutex_ == nullptr || !owns_) {
            return fail<Error>({.status = Status::Invalid});
        }
        const auto status = mutex_->unlock();
        if (status) {
            owns_ = false;
        }
        return status;
    }

    [[nodiscard]] bool owns_lock() const
    {
        return owns_;
    }

    explicit operator bool() const
    {
        return owns_lock();
    }

    [[nodiscard]] MutexType* mutex() const
    {
        return mutex_;
    }

    [[nodiscard]] MutexType* release()
    {
        owns_ = false;
        return std::exchange(mutex_, nullptr);
    }

  private:
    void disown_after_native_release()
    {
        owns_ = false;
    }

    friend class ConditionVariableRef;

    MutexType* mutex_{};
    bool owns_ = false;
};

template <Lockable MutexType>
[[nodiscard]] Result<LockGuard<MutexType>> lock_guard(MutexType& mutex,
                                                      Timeout timeout = Timeout::forever())
{
    return LockGuard<MutexType>::acquire(mutex, timeout);
}

template <Lockable MutexType>
[[nodiscard]] Result<UniqueLock<MutexType>>
unique_lock(MutexType& mutex, Timeout timeout = Timeout::forever())
{
    return UniqueLock<MutexType>::acquire(mutex, timeout);
}

} // namespace solar::kernel
