#pragma once

#include <chrono>
#include <cstdint>
#include <limits>

#include <zephyr/kernel.h>

#include "solar/core/status.hpp"
#include "solar/kernel/deadline.hpp"
#include "solar/kernel/interrupt.hpp"
#include "solar/kernel/native.hpp"
#include "solar/kernel/priority.hpp"
#include "solar/kernel/thread.hpp"

namespace solar::kernel::this_thread
{

[[nodiscard]] inline NativeThread id()
{
    return k_current_get();
}

[[nodiscard]] inline Result<ThreadRef> ref()
{
    if (in_isr()) {
        return fail<Error>({.status = Status::Invalid});
    }
    return ThreadRef{*k_current_get()};
}

[[nodiscard]] inline Result<Priority> priority()
{
    if (in_isr()) {
        return fail<Error>({.status = Status::Invalid});
    }
    return Priority::from_native(k_thread_priority_get(k_current_get()));
}

[[nodiscard]] inline Result<void> set_priority(Priority priority)
{
    if (in_isr()) {
        return fail<Error>({.status = Status::Invalid});
    }
    k_thread_priority_set(k_current_get(), priority.native_handle());
    return {};
}

[[nodiscard]] inline Result<Milliseconds> sleep_for(Timeout timeout)
{
    if (in_isr()) {
        return fail<Error>({.status = Status::Invalid});
    }
    return Milliseconds{k_sleep(timeout.native_handle())};
}

template <typename Rep, typename Period>
[[nodiscard]] inline Result<Milliseconds>
sleep_for(std::chrono::duration<Rep, Period> duration)
{
    return sleep_for(Timeout::after(duration));
}

[[nodiscard]] inline Result<Milliseconds> sleep_until(const Deadline& deadline)
{
    return sleep_for(deadline.remaining());
}

[[nodiscard]] inline Result<void> yield()
{
    if (!k_can_yield()) {
        return fail<Error>({.status = in_isr() ? Status::Invalid : Status::NotReady});
    }
    k_yield();
    return {};
}

/** Suspend the current thread until another context resumes it. */
[[nodiscard]] inline Result<void> suspend()
{
    if (in_isr()) {
        return fail<Error>({.status = Status::Invalid});
    }
    k_thread_suspend(k_current_get());
    return {};
}

/** Abort the current thread. On success this function cannot return. */
[[nodiscard]] inline Result<void> abort()
{
    if (in_isr()) {
        return fail<Error>({.status = Status::Invalid});
    }
    k_thread_abort(k_current_get());
    CODE_UNREACHABLE;
}

template <typename Rep, typename Period>
[[nodiscard]] inline Result<void>
busy_wait_for(std::chrono::duration<Rep, Period> duration)
{
    if (duration <= std::chrono::duration<Rep, Period>::zero()) {
        return {};
    }

    const auto microseconds = std::chrono::ceil<Microseconds>(duration).count();
    if (microseconds > std::numeric_limits<std::uint32_t>::max()) {
        return fail<Error>({.status = Status::Invalid});
    }
    k_busy_wait(static_cast<std::uint32_t>(microseconds));
    return {};
}

} // namespace solar::kernel::this_thread
