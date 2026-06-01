#pragma once

#include "low_level/rtos/sync.hpp"

namespace solar::rtos
{

using Mutex = low_level::rtos::Mutex;
using RecursiveMutex = low_level::rtos::RecursiveMutex;

/**
 * @brief Small RAII lock guard for Solar/low-level mutex types.
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
