#pragma once

#include <cerrno>

#include "solar/core/status.hpp"
#include "solar/kernel/time.hpp"

namespace solar::kernel::detail
{

[[nodiscard]] constexpr Result<void> map_native(int result) noexcept
{
    if (result == 0) {
        return {};
    }
    return fail<Error>(error_from_errno(result));
}

[[nodiscard]] constexpr Result<void> map_wait(int result, Timeout timeout,
                                              Status immediate_status) noexcept
{
    if (result == 0) {
        return {};
    }
    if (timeout.is_no_wait() && (result == -EAGAIN || result == -EBUSY || result == -ENOMSG)) {
        return fail<Error>({.status = immediate_status, .native = result});
    }
    if (result == -EAGAIN) {
        return fail<Error>({.status = Status::Timeout, .native = result});
    }
    if (result == -ENOMSG) {
        return fail<Error>({.status = Status::Cancelled, .native = result});
    }
    return fail<Error>(error_from_errno(result));
}

} // namespace solar::kernel::detail
