#pragma once

#include <optional>
#include <utility>

#include <zephyr/spinlock.h>

namespace solar::kernel
{

class SpinLockRef;

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
        Guard(k_spinlock& lock, k_spinlock_key_t key) : lock_(&lock), key_(key) {}

        friend class SpinLock;
        friend class SpinLockRef;

        k_spinlock* lock_{};
        k_spinlock_key_t key_{};
    };

    SpinLock() = default;

    SpinLock(const SpinLock&) = delete;
    SpinLock& operator=(const SpinLock&) = delete;
    SpinLock(SpinLock&&) = delete;
    SpinLock& operator=(SpinLock&&) = delete;

    [[nodiscard]] Guard acquire();

    [[nodiscard]] std::optional<Guard> try_acquire();

    [[nodiscard]] SpinLockRef ref();

  private:
    k_spinlock lock_{};
};

/** Non-owning access to an initialized native Zephyr spinlock. */
class SpinLockRef
{
  public:
    explicit constexpr SpinLockRef(k_spinlock& lock) : lock_(&lock) {}

    [[nodiscard]] SpinLock::Guard acquire() const
    {
        return SpinLock::Guard{*lock_, k_spin_lock(lock_)};
    }

    [[nodiscard]] std::optional<SpinLock::Guard> try_acquire() const
    {
        k_spinlock_key_t key{};
        if (k_spin_trylock(lock_, &key) != 0) {
            return std::nullopt;
        }
        return SpinLock::Guard{*lock_, key};
    }

    [[nodiscard]] constexpr k_spinlock* native_handle() const
    {
        return lock_;
    }

  private:
    k_spinlock* lock_;
};

inline SpinLockRef SpinLock::ref()
{
    return SpinLockRef{lock_};
}

inline SpinLock::Guard SpinLock::acquire()
{
    return ref().acquire();
}

inline std::optional<SpinLock::Guard> SpinLock::try_acquire()
{
    return ref().try_acquire();
}

} // namespace solar::kernel
