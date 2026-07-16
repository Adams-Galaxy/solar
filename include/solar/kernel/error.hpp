#pragma once

#include <cerrno>

#include "solar/core/status.hpp"
#include "solar/kernel/time.hpp"

namespace solar::kernel::detail
{

[[nodiscard]] constexpr Status map_native(int result) noexcept
{
    return result == 0 ? Status::Ok : status_from_errno(result);
}

[[nodiscard]] constexpr Status map_wait(int result, Timeout timeout,
                                        Status immediate_status) noexcept
{
    if (result == 0) {
        return Status::Ok;
    }
    if (timeout.is_no_wait() && (result == -EAGAIN || result == -EBUSY || result == -ENOMSG)) {
        return immediate_status;
    }
    if (result == -EAGAIN) {
        return Status::Timeout;
    }
    if (result == -ENOMSG) {
        return Status::Cancelled;
    }
    return status_from_errno(result);
}

} // namespace solar::kernel::detail
