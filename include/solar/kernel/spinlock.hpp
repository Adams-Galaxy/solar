#pragma once

#include <optional>
#include <utility>

#include <zephyr/spinlock.h>

namespace solar::kernel
{

class SpinLock
{
  public:
    class Guard
    {
      public:
        ~Guard()
        {
            if (lock_ != nullptr) {
                k_spin_unlock(lock_, key_);
            }
        }

        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;

        Guard(Guard&& other) noexcept : lock_(std::exchange(other.lock_, nullptr)), key_(other.key_)
        {}

        Guard& operator=(Guard&&) = delete;

      private:
        Guard(k_spinlock& lock, k_spinlock_key_t key) noexcept : lock_(&lock), key_(key) {}

        friend class SpinLock;

        k_spinlock* lock_{};
        k_spinlock_key_t key_{};
    };

    SpinLock() = default;

    SpinLock(const SpinLock&) = delete;
    SpinLock& operator=(const SpinLock&) = delete;
    SpinLock(SpinLock&&) = delete;
    SpinLock& operator=(SpinLock&&) = delete;

    [[nodiscard]] Guard acquire() noexcept
    {
        return Guard{lock_, k_spin_lock(&lock_)};
    }

    [[nodiscard]] std::optional<Guard> try_acquire() noexcept
    {
        k_spinlock_key_t key{};
        if (k_spin_trylock(&lock_, &key) != 0) {
            return std::nullopt;
        }
        return Guard{lock_, key};
    }

    [[nodiscard]] k_spinlock* native_handle() noexcept
    {
        return &lock_;
    }

    [[nodiscard]] const k_spinlock* native_handle() const noexcept
    {
        return &lock_;
    }

  private:
    k_spinlock lock_{};
};

} // namespace solar::kernel
