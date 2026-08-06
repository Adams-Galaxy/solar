#pragma once

#include <array>
#include <cerrno>
#include <concepts>
#include <cstdint>
#include <expected>
#include <source_location>
#include <string_view>
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

/** Stable, errno-compatible classification shared across error domains. */
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

[[nodiscard]] constexpr bool ok(Status status)
{
    return status == Status::Ok;
}

[[nodiscard]] constexpr int to_errno(Status status)
{
    return static_cast<std::underlying_type_t<Status>>(status);
}

[[nodiscard]] constexpr int to_native_errno(Status status)
{
    const auto error = to_errno(status);
    return error == 0 ? 0 : -error;
}

[[nodiscard]] constexpr Status status_from_errno(int error)
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

/** Default bounded Solar error for operations without a richer domain error. */
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

[[nodiscard]] constexpr Error error_from_errno(int error)
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

/** Error value accepted by Result.
 *
 * The error must provide a non-throwing `status_of(error)` projection. `Status`
 * itself is deliberately not an error value.
 */
template <typename E>
concept ErrorType = std::is_object_v<E> && !std::same_as<std::remove_cv_t<E>, Status> &&
                    std::is_nothrow_destructible_v<E> && requires(const E& error) {
                        { status_of(error) } noexcept -> std::same_as<Status>;
                    };

/** Fallible result backed directly by C++23 `std::expected`.
 * @tparam T Success value, or `void` for a command.
 * @tparam E Concrete bounded error type.
 */
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

/** Construct an explicitly typed failed Result.
 * @tparam E Concrete error domain.
 * @param error Error value to move into `std::unexpected`.
 */
template <ErrorType E>
[[nodiscard]] constexpr auto fail(E error) noexcept(std::is_nothrow_move_constructible_v<E>)
    -> Failure<E>
{
    return Failure<E>{std::move(error)};
}

/** Project-wide trace depth for `Traced<E, N>`. Zero (the default) means no
 * storage and a no-op `with_context` — see `docs/concepts/result-and-errors.md`.
 * Set via `CONFIG_SOLAR_ERROR_TRACE_DEPTH` (Zephyr Kconfig) or the
 * `SOLAR_ERROR_TRACE_DEPTH` host CMake cache variable of the same name.
 */
inline constexpr std::size_t kErrorTraceDepth =
#if defined(CONFIG_SOLAR_ERROR_TRACE_DEPTH)
    CONFIG_SOLAR_ERROR_TRACE_DEPTH;
#else
    0;
#endif

/** One recorded boundary crossing in an error's trace. */
struct Frame
{
    std::source_location loc;
    Status status{Status::Error};
    std::string_view tag;
};

/** Error value optionally carrying a fixed-size trace of boundary crossings.
 *
 * `N` is fixed at compile time by `kErrorTraceDepth`, one project-wide knob —
 * there is no per-call-site or per-module override. At `N == 0` (the
 * specialization below) this has identical layout to `E` and `with_context`
 * is a no-op, so turning tracing on or off is a single rebuild, not a
 * call-site change. See `docs/concepts/result-and-errors.md`.
 */
template <ErrorType E, std::size_t N = kErrorTraceDepth>
struct Traced
{
    E error;
    std::array<Frame, N> frames{};
    std::uint8_t count{};

    [[nodiscard]] constexpr Traced with_context(
        std::string_view tag,
        std::source_location loc = std::source_location::current()) const noexcept
    {
        Traced traced = *this;
        if (traced.count < N) {
            traced.frames[traced.count] = Frame{.loc = loc, .status = status_of(error), .tag = tag};
            ++traced.count;
        }
        return traced;
    }
};

template <ErrorType E>
struct Traced<E, 0>
{
    E error;

    [[nodiscard]] constexpr Traced with_context(
        std::string_view, std::source_location = std::source_location::current()) const noexcept
    {
        return *this;
    }
};

template <ErrorType E, std::size_t N>
[[nodiscard]] constexpr Status status_of(const Traced<E, N>& traced) noexcept
{
    return status_of(traced.error);
}

static_assert(sizeof(Traced<Error, 0>) == sizeof(Error),
              "Traced<E, 0> must be a zero-cost wrapper around E");

namespace detail
{

template <typename T> struct is_traced : std::false_type
{
};

template <ErrorType E, std::size_t N> struct is_traced<Traced<E, N>> : std::true_type
{
};

} // namespace detail

/** Re-wrap another error domain's status as a failed Result in this one.
 *
 * For crossing a domain boundary where only Status is meaningful upstream
 * (the common case propagating a lower layer's failure through a caller
 * with its own richer error type). Every field of Target other than status
 * takes its default value: this carries status only, not the source
 * error's other fields, so prefer building Target explicitly wherever a
 * richer field (reason, member, ...) should be preserved instead of
 * defaulted.
 *
 * The result is always `Traced<Target>`, even at `kErrorTraceDepth == 0`
 * (where it is a zero-cost wrapper): this crossing is recorded as a new
 * frame, and if `source` is itself already `Traced<X, N>`, that existing
 * chain is carried forward rather than discarded.
 * @tparam Target Concrete error domain to fail as.
 * @param source Source error to project through `status_of`.
 * @param tag Optional short label for this crossing, recorded in the trace.
 * @param loc Call site of this crossing; defaults to the caller's location.
 */
template <ErrorType Target, ErrorType Source>
[[nodiscard]] constexpr auto fail_as(
    const Source& source, std::string_view tag = {},
    std::source_location loc = std::source_location::current()) -> Failure<Traced<Target>>
{
    Traced<Target> traced{.error = Target{.status = status_of(source)}};
    if constexpr (kErrorTraceDepth > 0 && detail::is_traced<Source>::value) {
        traced.frames = source.frames;
        traced.count = source.count;
    }
    return fail<Traced<Target>>(traced.with_context(tag, loc));
}

/** `fail_as` from an already-failed Result, rather than its extracted error. */
template <ErrorType Target, typename T, ErrorType Source>
[[nodiscard]] constexpr auto fail_as(
    const Result<T, Source>& result, std::string_view tag = {},
    std::source_location loc = std::source_location::current()) -> Failure<Traced<Target>>
{
    return fail_as<Target>(result.error(), tag, loc);
}

} // namespace solar
