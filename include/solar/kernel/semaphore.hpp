#pragma once

#include <cstdint>

#include <zephyr/kernel.h>
#include <zephyr/sys/__assert.h>

#include "solar/core/status.hpp"
#include "solar/kernel/deadline.hpp"
#include "solar/kernel/error.hpp"

namespace solar::kernel
{

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
        k_sem_give(&semaphore_);
    }

    void give_isr() noexcept
    {
        k_sem_give(&semaphore_);
    }

    [[nodiscard]] Result<void> take(Timeout timeout = Timeout::forever()) noexcept
    {
        return detail::map_wait(k_sem_take(&semaphore_, timeout.native_handle()), timeout,
                                Status::WouldBlock);
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
        return take(Timeout::no_wait());
    }

    void reset() noexcept
    {
        k_sem_reset(&semaphore_);
    }

    [[nodiscard]] std::uint32_t count() const noexcept
    {
        return k_sem_count_get(const_cast<k_sem*>(&semaphore_));
    }

    [[nodiscard]] k_sem* native_handle() noexcept
    {
        return &semaphore_;
    }

    [[nodiscard]] const k_sem* native_handle() const noexcept
    {
        return &semaphore_;
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
