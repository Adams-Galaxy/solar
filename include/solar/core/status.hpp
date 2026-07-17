#pragma once

#include <cerrno>
#include <concepts>
#include <expected>
#include <type_traits>
#include <utility>

#include "solar/core/language.hpp"

namespace solar
{

namespace detail
{

constexpr int empty_errno =
#ifdef ENODATA
    ENODATA;
#else
    ENOMSG;
#endif

constexpr int dependency_failed_errno =
#ifdef ENOLINK
    ENOLINK;
#else
    EIO;
#endif

constexpr int no_buffer_errno =
#ifdef ENOBUFS
    ENOBUFS;
#else
    ENOMEM;
#endif

constexpr int message_too_large_errno =
#ifdef EMSGSIZE
    EMSGSIZE;
#else
    ENOSPC;
#endif

constexpr int protocol_errno =
#ifdef EPROTO
    EPROTO;
#elif defined(EBADMSG)
    EBADMSG;
#else
                    EIO;
#endif

constexpr int overflow_errno =
#ifdef EOVERFLOW
    EOVERFLOW;
#else
    ERANGE;
#endif

constexpr int unexpected_exit_errno =
#ifdef EPIPE
    EPIPE;
#else
    EIO;
#endif

} // namespace detail

enum class Status : int
{
    Ok = 0,
    Error = EIO,
    Invalid = EINVAL,
    NotReady = ENODEV,
    NotFound = ENOENT,
    NotSupported = ENOTSUP,
    Busy = EBUSY,
    Already = EALREADY,
    Timeout = ETIMEDOUT,
    Cancelled = ECANCELED,
    NoMemory = ENOMEM,
    NoSpace = ENOSPC,
    Full = NoSpace,
    WouldBlock = EAGAIN,
    Empty = detail::empty_errno,
    Interrupted = EINTR,
    Deadlock = EDEADLK,
    PermissionDenied = EACCES,
    NoBuffer = detail::no_buffer_errno,
    MessageTooLarge = detail::message_too_large_errno,
    ProtocolError = detail::protocol_errno,
    Overflow = detail::overflow_errno,
    DependencyFailed = detail::dependency_failed_errno,
    UnexpectedExit = detail::unexpected_exit_errno,
};

[[nodiscard]] constexpr bool ok(Status status) noexcept
{
    return status == Status::Ok;
}

[[nodiscard]] constexpr int to_errno(Status status) noexcept
{
    return static_cast<std::underlying_type_t<Status>>(status);
}

[[nodiscard]] constexpr int to_native_errno(Status status) noexcept
{
    const auto error = to_errno(status);
    return error == 0 ? 0 : -error;
}

[[nodiscard]] constexpr Status status_from_errno(int error) noexcept
{
    const auto code = error < 0 ? -error : error;

    switch (code) {
    case 0:
        return Status::Ok;
    case EIO:
        return Status::Error;
    case EINVAL:
        return Status::Invalid;
    case ENODEV:
        return Status::NotReady;
    case ENOENT:
        return Status::NotFound;
    case ENOTSUP:
    case ENOSYS:
        return Status::NotSupported;
    case EBUSY:
        return Status::Busy;
    case EALREADY:
        return Status::Already;
    case ETIMEDOUT:
        return Status::Timeout;
    case ECANCELED:
        return Status::Cancelled;
    case ENOMEM:
        return Status::NoMemory;
    case ENOSPC:
        return Status::NoSpace;
    case EAGAIN:
        return Status::WouldBlock;
    case EINTR:
        return Status::Interrupted;
    case EDEADLK:
        return Status::Deadlock;
    case EACCES:
    case EPERM:
        return Status::PermissionDenied;
    default:
        break;
    }

    if (code == detail::empty_errno) {
        return Status::Empty;
    }
    if (code == detail::no_buffer_errno) {
        return Status::NoBuffer;
    }
    if (code == detail::message_too_large_errno) {
        return Status::MessageTooLarge;
    }
    if (code == detail::protocol_errno) {
        return Status::ProtocolError;
    }
    if (code == detail::overflow_errno) {
        return Status::Overflow;
    }
    if (code == detail::dependency_failed_errno) {
        return Status::DependencyFailed;
    }
    if (code == detail::unexpected_exit_errno) {
        return Status::UnexpectedExit;
    }

    return Status::Error;
}

struct Error
{
    Status status{Status::Error};
    int native{};

    constexpr bool operator==(const Error&) const = default;
};

[[nodiscard]] constexpr Status status_of(const Error& error) noexcept
{
    return error.status;
}

[[nodiscard]] constexpr Error error_from_errno(int error) noexcept
{
    return {.status = status_from_errno(error), .native = error};
}

template <typename E>
    requires(!std::same_as<std::remove_cv_t<E>, Error> &&
             requires(const E& error) {
                 requires std::same_as<std::remove_cvref_t<decltype(error.status)>, Status>;
             })
[[nodiscard]] constexpr Status status_of(const E& error) noexcept
{
    return error.status;
}

template <typename E>
concept ErrorType = std::is_object_v<E> && !std::same_as<std::remove_cv_t<E>, Status> &&
                    std::is_nothrow_destructible_v<E> && requires(const E& error) {
                        { status_of(error) } noexcept -> std::same_as<Status>;
                    };

template <typename T, ErrorType E = Error> using Result = std::expected<T, E>;

template <typename R>
concept ResultType = requires {
    typename std::remove_cvref_t<R>::value_type;
    typename std::remove_cvref_t<R>::error_type;
} && ErrorType<typename std::remove_cvref_t<R>::error_type>;

template <typename R>
concept VoidResult =
    ResultType<R> && std::same_as<typename std::remove_cvref_t<R>::value_type, void>;

template <ErrorType E> using Failure = std::unexpected<E>;

template <ErrorType E>
[[nodiscard]] constexpr auto fail(E error) noexcept(std::is_nothrow_move_constructible_v<E>)
    -> Failure<E>
{
    return Failure<E>{std::move(error)};
}

} // namespace solar
