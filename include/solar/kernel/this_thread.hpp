#pragma once

#include <chrono>
#include <cstdint>
#include <limits>

#include <zephyr/kernel.h>

#include "solar/core/status.hpp"
#include "solar/kernel/deadline.hpp"
#include "solar/kernel/native.hpp"
#include "solar/kernel/priority.hpp"

namespace solar::kernel::this_thread
{

[[nodiscard]] inline NativeThread id() noexcept
{
    return k_current_get();
}

[[nodiscard]] inline Priority priority() noexcept
{
    return *Priority::from_native(k_thread_priority_get(k_current_get()));
}

inline void set_priority(Priority priority) noexcept
{
    k_thread_priority_set(k_current_get(), priority.native_handle());
}

[[nodiscard]] inline Milliseconds sleep_for(Timeout timeout) noexcept
{
    return Milliseconds{k_sleep(timeout.native_handle())};
}

template <typename Rep, typename Period>
[[nodiscard]] inline Milliseconds sleep_for(std::chrono::duration<Rep, Period> duration) noexcept
{
    return sleep_for(Timeout::after(duration));
}

[[nodiscard]] inline Milliseconds sleep_until(const Deadline& deadline) noexcept
{
    return sleep_for(deadline.remaining());
}

[[nodiscard]] inline Status yield() noexcept
{
    if (!k_can_yield()) {
        return Status::NotReady;
    }
    k_yield();
    return Status::Ok;
}

template <typename Rep, typename Period>
[[nodiscard]] inline Status busy_wait_for(std::chrono::duration<Rep, Period> duration) noexcept
{
    if (duration <= std::chrono::duration<Rep, Period>::zero()) {
        return Status::Ok;
    }

    const auto microseconds = std::chrono::ceil<Microseconds>(duration).count();
    if (microseconds > std::numeric_limits<std::uint32_t>::max()) {
        return Status::Invalid;
    }
    k_busy_wait(static_cast<std::uint32_t>(microseconds));
    return Status::Ok;
}

} // namespace solar::kernel::this_thread
