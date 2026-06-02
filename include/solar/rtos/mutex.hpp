#pragma once

#include <zephyr/kernel.h>

#include "solar/core/status.hpp"
#include "solar/rtos/deadline.hpp"

namespace solar::rtos
{

class Mutex
{
public:
    Mutex() { k_mutex_init(&mutex_); }

    Status lock(Tick timeout_ticks = WaitForever)
    {
        return k_mutex_lock(&mutex_, to_timeout(timeout_ticks)) == 0 ? Status::Ok : Status::Timeout;
    }

    Status lock(const Deadline &deadline)
    {
        return lock(deadline.remaining_ticks());
    }

    Status unlock()
    {
        return k_mutex_unlock(&mutex_) == 0 ? Status::Ok : Status::Error;
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

} // namespace solar::rtos
