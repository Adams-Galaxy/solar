#pragma once

#include <cerrno>
#include <type_traits>
#include <utility>

namespace solar
{

#ifndef SOLAR_STATUS_EMPTY_ERRNO
#ifdef ENODATA
#define SOLAR_STATUS_EMPTY_ERRNO ENODATA
#else
#define SOLAR_STATUS_EMPTY_ERRNO ENOMSG
#endif
#endif

#ifndef SOLAR_STATUS_DEPENDENCY_FAILED_ERRNO
#ifdef ENOLINK
#define SOLAR_STATUS_DEPENDENCY_FAILED_ERRNO ENOLINK
#else
#define SOLAR_STATUS_DEPENDENCY_FAILED_ERRNO EIO
#endif
#endif

#ifndef SOLAR_STATUS_PERMISSION_DENIED_ERRNO
#ifdef EACCES
#define SOLAR_STATUS_PERMISSION_DENIED_ERRNO EACCES
#else
#define SOLAR_STATUS_PERMISSION_DENIED_ERRNO EPERM
#endif
#endif

#ifndef SOLAR_STATUS_NO_BUFFER_ERRNO
#ifdef ENOBUFS
#define SOLAR_STATUS_NO_BUFFER_ERRNO ENOBUFS
#else
#define SOLAR_STATUS_NO_BUFFER_ERRNO ENOMEM
#endif
#endif

#ifndef SOLAR_STATUS_MESSAGE_TOO_LARGE_ERRNO
#ifdef EMSGSIZE
#define SOLAR_STATUS_MESSAGE_TOO_LARGE_ERRNO EMSGSIZE
#else
#define SOLAR_STATUS_MESSAGE_TOO_LARGE_ERRNO ENOSPC
#endif
#endif

#ifndef SOLAR_STATUS_PROTOCOL_ERRNO
#ifdef EPROTO
#define SOLAR_STATUS_PROTOCOL_ERRNO EPROTO
#elif defined(EBADMSG)
#define SOLAR_STATUS_PROTOCOL_ERRNO EBADMSG
#else
#define SOLAR_STATUS_PROTOCOL_ERRNO EIO
#endif
#endif

#ifndef SOLAR_STATUS_OVERFLOW_ERRNO
#ifdef EOVERFLOW
#define SOLAR_STATUS_OVERFLOW_ERRNO EOVERFLOW
#else
#define SOLAR_STATUS_OVERFLOW_ERRNO ERANGE
#endif

#ifndef SOLAR_STATUS_UNEXPECTED_EXIT_ERRNO
#ifdef EPIPE
#define SOLAR_STATUS_UNEXPECTED_EXIT_ERRNO EPIPE
#else
#define SOLAR_STATUS_UNEXPECTED_EXIT_ERRNO EIO
#endif
#endif
#endif

/**
 * @brief Errno-backed status vocabulary shared by Solar core and services.
 *
 * Values intentionally map one-to-one with positive errno values so status
 * codes remain readable and can be converted with a simple integer cast.
 */
enum class Status : int
{
    Ok = 0,
    Error = EIO,
    Timeout = ETIMEDOUT,
    WouldBlock = EAGAIN,
    NoMemory = ENOMEM,
    Invalid = EINVAL,
    NoSpace = ENOSPC,
    Full = NoSpace,
    Empty = SOLAR_STATUS_EMPTY_ERRNO,
    NotReady = ENODEV,
    Busy = EBUSY,
    Already = EALREADY,
    NotFound = ENOENT,
    NotSupported = ENOTSUP,
    Cancelled = ECANCELED,
    Interrupted = EINTR,
    Deadlock = EDEADLK,
    PermissionDenied = SOLAR_STATUS_PERMISSION_DENIED_ERRNO,
    NoBuffer = SOLAR_STATUS_NO_BUFFER_ERRNO,
    MessageTooLarge = SOLAR_STATUS_MESSAGE_TOO_LARGE_ERRNO,
    ProtocolError = SOLAR_STATUS_PROTOCOL_ERRNO,
    Overflow = SOLAR_STATUS_OVERFLOW_ERRNO,
    DependencyFailed = SOLAR_STATUS_DEPENDENCY_FAILED_ERRNO,
    UnexpectedExit = SOLAR_STATUS_UNEXPECTED_EXIT_ERRNO,
};

/**
 * @brief Convenience predicate for status-returning APIs.
 */
constexpr bool ok(Status status)
{
    return status == Status::Ok;
}

constexpr int to_errno(Status status)
{
    return static_cast<std::underlying_type_t<Status>>(status);
}

constexpr int to_native_errno(Status status)
{
    const int error = to_errno(status);
    return error == 0 ? 0 : -error;
}

constexpr Status status_from_errno(int error)
{
    const int code = error < 0 ? -error : error;

    if (code == 0)
    {
        return Status::Ok;
    }
    if (code == EIO)
    {
        return Status::Error;
    }
    if (code == ETIMEDOUT)
    {
        return Status::Timeout;
    }
#ifdef ETIME
    if (code == ETIME)
    {
        return Status::Timeout;
    }
#endif
    if (code == EAGAIN)
    {
        return Status::WouldBlock;
    }
#ifdef EWOULDBLOCK
    if (code == EWOULDBLOCK)
    {
        return Status::WouldBlock;
    }
#endif
    if (code == ENOMEM)
    {
        return Status::NoMemory;
    }
#ifdef ENOSR
    if (code == ENOSR)
    {
        return Status::NoMemory;
    }
#endif
    if (code == EINVAL)
    {
        return Status::Invalid;
    }
#ifdef EBADF
    if (code == EBADF)
    {
        return Status::Invalid;
    }
#endif
#ifdef EFAULT
    if (code == EFAULT)
    {
        return Status::Invalid;
    }
#endif
    if (code == ENOSPC)
    {
        return Status::NoSpace;
    }
#ifdef ENOMSG
    if (code == ENOMSG)
    {
        return Status::Empty;
    }
#endif
#ifdef ENODATA
    if (code == ENODATA)
    {
        return Status::Empty;
    }
#endif
    if (code == ENODEV)
    {
        return Status::NotReady;
    }
#ifdef ENXIO
    if (code == ENXIO)
    {
        return Status::NotReady;
    }
#endif
    if (code == EBUSY)
    {
        return Status::Busy;
    }
    if (code == EALREADY)
    {
        return Status::Already;
    }
#ifdef EEXIST
    if (code == EEXIST)
    {
        return Status::Already;
    }
#endif
    if (code == ENOENT)
    {
        return Status::NotFound;
    }
#ifdef ESRCH
    if (code == ESRCH)
    {
        return Status::NotFound;
    }
#endif
    if (code == ENOTSUP)
    {
        return Status::NotSupported;
    }
#ifdef ENOSYS
    if (code == ENOSYS)
    {
        return Status::NotSupported;
    }
#endif
#ifdef EOPNOTSUPP
    if (code == EOPNOTSUPP)
    {
        return Status::NotSupported;
    }
#endif
    if (code == ECANCELED)
    {
        return Status::Cancelled;
    }
    if (code == EINTR)
    {
        return Status::Interrupted;
    }
    if (code == EDEADLK)
    {
        return Status::Deadlock;
    }
#ifdef EPERM
    if (code == EPERM)
    {
        return Status::PermissionDenied;
    }
#endif
#ifdef EACCES
    if (code == EACCES)
    {
        return Status::PermissionDenied;
    }
#endif
#ifdef ENOBUFS
    if (code == ENOBUFS)
    {
        return Status::NoBuffer;
    }
#endif
#ifdef EMSGSIZE
    if (code == EMSGSIZE)
    {
        return Status::MessageTooLarge;
    }
#endif
#ifdef EPROTO
    if (code == EPROTO)
    {
        return Status::ProtocolError;
    }
#endif
#ifdef EBADMSG
    if (code == EBADMSG)
    {
        return Status::ProtocolError;
    }
#endif
#ifdef EILSEQ
    if (code == EILSEQ)
    {
        return Status::ProtocolError;
    }
#endif
#ifdef ERANGE
    if (code == ERANGE)
    {
        return Status::Overflow;
    }
#endif
#ifdef EOVERFLOW
    if (code == EOVERFLOW)
    {
        return Status::Overflow;
    }
#endif
#if SOLAR_STATUS_DEPENDENCY_FAILED_ERRNO != EIO
    if (code == SOLAR_STATUS_DEPENDENCY_FAILED_ERRNO)
    {
        return Status::DependencyFailed;
    }
#endif
#if SOLAR_STATUS_UNEXPECTED_EXIT_ERRNO != EIO
    if (code == SOLAR_STATUS_UNEXPECTED_EXIT_ERRNO)
    {
        return Status::UnexpectedExit;
    }
#endif

    return Status::Error;
}

/**
 * @brief Deterministic result type for APIs that return either a value or a
 * `Status` without exceptions or heap allocation.
 */
template <typename T>
class Result
{
public:
    constexpr Result(Status status) : status_(status), value_{} {}
    constexpr Result(const T &value) : status_(Status::Ok), value_(value) {}
    constexpr Result(T &&value) : status_(Status::Ok), value_(std::move(value)) {}

    constexpr bool has_value() const
    {
        return status_ == Status::Ok;
    }

    constexpr explicit operator bool() const
    {
        return has_value();
    }

    constexpr Status status() const
    {
        return status_;
    }

    constexpr T &value()
    {
        return value_;
    }

    constexpr const T &value() const
    {
        return value_;
    }

private:
    Status status_;
    T value_;
};

/**
 * @brief Void specialization for status-only operations with result semantics.
 */
template <>
class Result<void>
{
public:
    constexpr Result(Status status = Status::Ok) : status_(status) {}

    constexpr bool has_value() const
    {
        return status_ == Status::Ok;
    }

    constexpr explicit operator bool() const
    {
        return has_value();
    }

    constexpr Status status() const
    {
        return status_;
    }

private:
    Status status_;
};

} // namespace solar
