#pragma once

#include <cstdint>

#include <zephyr/kernel.h>
#include <zephyr/sys/__assert.h>

#include "solar/core/status.hpp"
#include "solar/kernel/deadline.hpp"
#include "solar/kernel/error.hpp"
#include "solar/kernel/interrupt.hpp"

namespace solar::kernel
{

/** Non-owning access to an initialized Zephyr semaphore. */
class SemaphoreRef
{
  public:
    explicit constexpr SemaphoreRef(k_sem& semaphore) noexcept : semaphore_(&semaphore) {}

    void give() const noexcept
    {
        k_sem_give(semaphore_);
    }

    [[nodiscard]] Result<void> take(Timeout timeout = Timeout::forever()) const noexcept
    {
        if (in_isr()) {
            return fail<Error>({.status = Status::Invalid});
        }
        return take_native(timeout);
    }

    [[nodiscard]] Result<void> take(const Deadline& deadline) const noexcept
    {
        return take(deadline.remaining());
    }

    [[nodiscard]] Result<void> try_take() const noexcept
    {
        return take(Timeout::no_wait());
    }

    [[nodiscard]] Result<void> try_take_isr() const noexcept
    {
        return take_native(Timeout::no_wait());
    }

    void reset() const noexcept
    {
        k_sem_reset(semaphore_);
    }

    [[nodiscard]] std::uint32_t count() const noexcept
    {
        return k_sem_count_get(semaphore_);
    }

    [[nodiscard]] constexpr k_sem* native_handle() const noexcept
    {
        return semaphore_;
    }

  private:
    [[nodiscard]] Result<void> take_native(Timeout timeout) const noexcept
    {
        return detail::map_wait(k_sem_take(semaphore_, timeout.native_handle()), timeout,
                                Status::WouldBlock);
    }

    k_sem* semaphore_;
};

class Semaphore
{
  public:
    explicit Semaphore(std::uint32_t initial_count = 0, std::uint32_t limit = 1) noexcept
    {
        __ASSERT_NO_MSG(k_sem_init(&semaphore_, initial_count, limit) == 0);
    }

    Semaphore(const Semaphore&) = delete;
    Semaphore& operator=(const Semaphore&) = delete;
    Semaphore(Semaphore&&) = delete;
    Semaphore& operator=(Semaphore&&) = delete;

    void give() noexcept
    {
        ref().give();
    }

    [[nodiscard]] Result<void> take(Timeout timeout = Timeout::forever()) noexcept
    {
        return ref().take(timeout);
    }

    [[nodiscard]] Result<void> take(const Deadline& deadline) noexcept
    {
        return take(deadline.remaining());
    }

    [[nodiscard]] Result<void> try_take() noexcept
    {
        return take(Timeout::no_wait());
    }

    [[nodiscard]] Result<void> try_take_isr() noexcept
    {
        return ref().try_take_isr();
    }

    void reset() noexcept
    {
        ref().reset();
    }

    [[nodiscard]] std::uint32_t count() const noexcept
    {
        return SemaphoreRef{const_cast<k_sem&>(semaphore_)}.count();
    }

    [[nodiscard]] SemaphoreRef ref() noexcept
    {
        return SemaphoreRef{semaphore_};
    }

  private:
    k_sem semaphore_{};
};

class BinarySemaphore : public Semaphore
{
  public:
    explicit BinarySemaphore(bool initially_available = false) noexcept
        : Semaphore(initially_available ? 1U : 0U, 1U)
    {}
};

} // namespace solar::kernel
