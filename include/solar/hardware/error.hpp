#pragma once

#include <cerrno>
#include <cstdint>
#include <string_view>

#include "solar/core/status.hpp"

namespace solar::hardware
{

enum class Operation : std::uint8_t
{
    RequireReady,
    Configure,
    Read,
    Write,
    Toggle,
    InterruptConfigure,
    CallbackInstall,
    CallbackRemove,
    Pending,
    Transceive,
    Recover,
    Enable,
    Disable,
    Abort,
    Install,
    Feed,
    Convert,
    Start,
    Stop,
    Release,
    Submit,
    Complete,
    Cancel,
};

enum class Reason : std::uint8_t
{
    NotReady,
    Unsupported,
    InvalidConfiguration,
    Busy,
    Timeout,
    Cancelled,
    ResourceExhausted,
    AlreadyOwned,
    StaleCompletion,
    DriverFailure,
};

struct Error
{
    Status status{Status::Error};
    Reason reason{Reason::DriverFailure};
    Operation operation{Operation::RequireReady};
    int native{};
    std::string_view endpoint{};
};

namespace detail
{

[[nodiscard]] constexpr Reason reason_from(Status status) noexcept
{
    switch (status) {
    case Status::NotReady:
        return Reason::NotReady;
    case Status::NotSupported:
        return Reason::Unsupported;
    case Status::Invalid:
        return Reason::InvalidConfiguration;
    case Status::Busy:
    case Status::WouldBlock:
    case Status::Already:
        return Reason::Busy;
    case Status::Timeout:
        return Reason::Timeout;
    case Status::Cancelled:
        return Reason::Cancelled;
    case Status::NoMemory:
    case Status::NoSpace:
    case Status::NoBuffer:
        return Reason::ResourceExhausted;
    default:
        return Reason::DriverFailure;
    }
}

[[nodiscard]] constexpr Error native_error(int result, Operation operation,
                                           std::string_view endpoint = {}) noexcept
{
    const auto status = status_from_errno(result);
    return {.status = status,
            .reason = reason_from(status),
            .operation = operation,
            .native = result,
            .endpoint = endpoint};
}

[[nodiscard]] constexpr Result<void, Error> native_result(int result, Operation operation,
                                                          std::string_view endpoint = {}) noexcept
{
    if (result == 0) {
        return {};
    }
    return fail<Error>(native_error(result, operation, endpoint));
}

} // namespace detail

} // namespace solar::hardware
