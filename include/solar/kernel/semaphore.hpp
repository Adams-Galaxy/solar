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
    Semaphore() noexcept : Semaphore(0, 1, ValidatedConfiguration{}) {}

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
    struct ValidatedConfiguration
    {};

    Semaphore(std::uint32_t initial_count, std::uint32_t limit, ValidatedConfiguration) noexcept
    {
        const int result = k_sem_init(&semaphore_, initial_count, limit);
        __ASSERT_NO_MSG(result == 0);
        (void)result;
    }

    friend class BinarySemaphore;
    template <std::uint32_t, std::uint32_t> friend class CountingSemaphore;

    k_sem semaphore_{};
};

class BinarySemaphore : public Semaphore
{
  public:
    explicit BinarySemaphore(bool initially_available = false) noexcept
        : Semaphore(initially_available ? 1U : 0U, 1U, ValidatedConfiguration{})
    {}
};

/** Semaphore whose capacity and initial count are validated at compile time. */
template <std::uint32_t Limit, std::uint32_t InitialCount = 0>
class CountingSemaphore : public Semaphore
{
    static_assert(Limit > 0,
                  "SOLAR_DIAGNOSTIC_SEMAPHORE_ZERO_LIMIT: semaphore limit must be non-zero");
    static_assert(Limit <= K_SEM_MAX_LIMIT,
                  "SOLAR_DIAGNOSTIC_SEMAPHORE_LIMIT_OVERFLOW: limit exceeds Zephyr's maximum");
    static_assert(InitialCount <= Limit,
                  "SOLAR_DIAGNOSTIC_SEMAPHORE_INITIAL_OVERFLOW: initial count exceeds limit");

  public:
    CountingSemaphore() noexcept : Semaphore(InitialCount, Limit, ValidatedConfiguration{}) {}
};

} // namespace solar::kernel
