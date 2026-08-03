#pragma once

#include <atomic>

namespace solar
{

/** Small allocation-free mutex for short, non-blocking framework state updates. */
class SpinMutex
{
  public:
    void lock() noexcept
    {
        while (locked_.test_and_set(std::memory_order_acquire)) {
        }
    }

    void unlock() noexcept
    {
        locked_.clear(std::memory_order_release);
    }

    [[nodiscard]] bool try_lock() noexcept
    {
        return !locked_.test_and_set(std::memory_order_acquire);
    }

  private:
    std::atomic_flag locked_ = ATOMIC_FLAG_INIT;
};

class SpinGuard
{
  public:
    explicit SpinGuard(SpinMutex& mutex) noexcept : mutex_{mutex}
    {
        mutex_.lock();
    }
    ~SpinGuard()
    {
        mutex_.unlock();
    }
    SpinGuard(const SpinGuard&) = delete;
    SpinGuard& operator=(const SpinGuard&) = delete;

  private:
    SpinMutex& mutex_;
};

} // namespace solar
