#pragma once

#include <zephyr/kernel.h>

#include "solar/core/status.hpp"
#include "solar/kernel/config.hpp"
#include "solar/kernel/deadline.hpp"

namespace solar::kernel
{

class Mutex
{
public:
    Mutex() { k_mutex_init(&mutex_); }

    Status lock(Timeout timeout = Timeout::forever())
    {
        return status_from_native_wait(k_mutex_lock(&mutex_, timeout.native()));
    }

    Status lock(Tick timeout_ticks)
    {
        return lock(Timeout::after_ticks(timeout_ticks));
    }

    Status lock(const Deadline &deadline)
    {
        return lock(deadline.remaining_timeout());
    }

    Status unlock()
    {
        return status_from_native(k_mutex_unlock(&mutex_));
    }

    k_mutex *native_handle()
    {
        return &mutex_;
    }

private:
    k_mutex mutex_{};
};

using RecursiveMutex = Mutex;

/**
 * @brief Small RAII lock guard for Solar mutex wrappers.
 */
template <typename MutexT>
class LockGuard
{
public:
    explicit LockGuard(MutexT &mutex) : mutex_(mutex)
    {
        (void)mutex_.lock();
    }

    ~LockGuard()
    {
        (void)mutex_.unlock();
    }

    LockGuard(const LockGuard &) = delete;
    LockGuard &operator=(const LockGuard &) = delete;

private:
    MutexT &mutex_;
};

template <typename MutexT>
using UniqueLock = LockGuard<MutexT>;

} // namespace solar::kernel
