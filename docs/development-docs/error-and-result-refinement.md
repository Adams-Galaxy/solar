# Error And Result Refinement

Date: 2026-07-17

Status: implemented

This specification refines Solar's completed static-system implementation
before public documentation is authored. It supersedes the portions of the
static-reform result specification that permit `Status` as the default Result
error or as the normal return type of a public fallible operation.

## 1. Purpose

Solar uses explicit, value-based error propagation. The repository already
uses C++23 `std::expected` and rich subsystem errors extensively, but its
remaining status-only APIs and `Result<T, Status>` default preserve two
competing conventions.

The refined model has four distinct concepts:

- `Status` is a broad, stable classification;
- `solar::Error` is the default concrete error value;
- subsystem and application error types preserve domain detail;
- `Result<T, E>` is the sole normal fallible-return carrier.

The model is similar to exception systems only in providing a common way to
classify unrelated failures. It does not use exceptions, inheritance, virtual
dispatch, heap allocation, or implicit throwing.

## 2. Status Is Classification

`solar::Status` remains Solar's compact errno-compatible classification
vocabulary. It is appropriate for:

- lifecycle, health, Supervisor, and diagnostic records;
- protocol-level summaries;
- comparisons across unrelated error domains;
- mapping Zephyr and driver errno values;
- deliberately lossy subsystem boundaries.

`Status` is not itself an error object after this migration. A fallible public
operation must not return `Status`, and `Status` must not satisfy
`solar::ErrorType`.

`Status::Ok` describes recorded success. A successful operation is represented
by a populated `Result`, not by returning `Status::Ok`.

Functions such as `status_from_errno`, `to_errno`, `to_native_errno`, and
`status_of(error)` continue to return `Status` because classification is their
purpose.

## 3. Default Solar Error

Solar defines a compact default error value:

```cpp
namespace solar
{

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

}
```

`status` contains the broad classification. `native` optionally preserves the
original Zephyr, POSIX, driver, or platform result and is zero when no native
code is relevant.

An `Error` used as a failure must not contain `Status::Ok`. The aggregate shape
is intentional: it is bounded, constexpr-friendly, easy to inspect, and works
with explicit braced failure construction.

## 4. Error Type Contract

Every Result error type must satisfy `solar::ErrorType`.

The mandatory semantic operation is:

```cpp
[[nodiscard]] constexpr solar::Status status_of(const E& error) noexcept;
```

`status_of` is found through ordinary lookup and argument-dependent lookup.
This permits Solar subsystems and applications to define the projection next
to their own error type without specializing Solar templates.

The concept does not require inheritance, a common data layout, copying,
equality, textual formatting, or a particular reason representation. It should
require an object error type, non-throwing destruction, and the exact
non-throwing `Status` projection.

Error types should normally be:

- bounded and allocation-free;
- small value types;
- movable;
- equality comparable where useful;
- safe to retain for the documented lifetime;
- explicit about native values and borrowed views;
- independently serializable only when Remote exposure requires it.

Neither the concept nor `Result` automatically logs, records, serializes, or
publishes an error.

## 5. Typed Errors

Subsystems retain their own error domains:

```cpp
namespace solar::parameters
{

struct Error
{
    Status status{Status::Error};
    Reason reason{Reason::InternalInvariant};
    Operation operation{Operation::Query};
    LocalId parameter{};
    int native_error{};
};

[[nodiscard]] constexpr Status status_of(const Error& error) noexcept
{
    return error.status;
}

}
```

Keeping a `Status` member is recommended when the classification is selected at
the failure site and an O(1) projection is useful. It is not mandatory.

A small enum can instead provide a mapping:

```cpp
enum class MotorError
{
    Disabled,
    OverCurrent,
};

constexpr solar::Status status_of(MotorError error) noexcept
{
    switch (error) {
    case MotorError::Disabled:
        return solar::Status::NotReady;
    case MotorError::OverCurrent:
        return solar::Status::Error;
    }
    return solar::Status::Error;
}
```

Typed errors remain rich until an explicit boundary classifies or converts
them. Solar does not define a universal variant containing every subsystem
error.

## 6. Result

Solar's canonical result remains a transparent alias of `std::expected`:

```cpp
template<typename T, ErrorType E = Error>
using Result = std::expected<T, E>;
```

The normal signatures are:

```cpp
solar::Result<void> initialize();
solar::Result<Reading> read();
solar::Result<Reading, ImuError> read_imu();
```

`Result<void>` replaces status-only success/failure returns. The alias must not
be wrapped in a Solar result class and must preserve the complete C++23
`std::expected` interface.

## 7. Failure Construction

Solar provides an explicitly typed failure helper:

```cpp
template<ErrorType E>
[[nodiscard]] constexpr auto fail(E error) noexcept(/* appropriate traits */)
    -> std::unexpected<E>;
```

The canonical call supplies the selected error type:

```cpp
return solar::fail<solar::Error>({
    .status = solar::Status::NotReady,
});

return solar::fail<imu::Error>({
    .status = solar::Status::Error,
    .reason = imu::Reason::IdentityMismatch,
    .native = result,
});
```

The explicit template argument is required by convention even when deduction
would work. It:

- makes the propagated error domain visible;
- permits direct braced aggregate initialization;
- avoids temporary type repetition;
- keeps failure construction consistent throughout Solar.

`fail(Status)` is not provided. Callers construct `solar::Error` explicitly so
`Status` does not silently continue as an error carrier.

## 8. Error Conversion

Mapping a typed error to `Status` is explicit and lossy:

```cpp
const auto classification = status_of(result.error());
```

Mapping it to the default error is also explicit:

```cpp
return operation().transform_error([](const auto& error) {
    return solar::Error{.status = status_of(error)};
});
```

Solar may later provide a concise named helper for this conversion if repeated
use demonstrates value. The initial implementation should keep lossy
boundaries visible.

There is no implicit conversion between subsystem errors, no base-error object,
and no automatic nested cause chain. A subsystem that needs causal detail
should carry bounded domain-specific context in its own error.

## 9. Public API Rule

Every ordinary public operation that can fail returns `Result<T, E>`.

Examples:

```cpp
solar::Result<void> Mutex::lock(...);
solar::Result<void> Thread::start();
solar::Result<void> Timer::start(...);
solar::Result<void> install_fatal_observer(...);
```

Direct `Status` returns remain valid only for operations whose purpose is
classification, private implementation helpers where no error escapes, and
external callbacks whose ABI Solar does not control.

Predicates and infallible commands retain their natural `bool` or `void`
returns. Stored `Status` fields remain valid and useful.

## 10. Lifecycle Hooks

Lifecycle hooks use Result directly:

```cpp
struct Imu
{
    static solar::Result<void, imu::Error> init();
    static solar::Result<void> start();
    static solar::Result<void> stop();
    static solar::Result<void> deinit();
};
```

The lifecycle engine accepts `Result<void, E>` for any `ErrorType`. It records
`status_of(error)` in generic lifecycle state. The component or owning
subsystem may retain the complete typed error in its own canonical diagnostics.

Lifecycle does not own a type-erased arbitrary error store.

## 11. Kernel And Zephyr Boundaries

Kernel wrappers convert Zephyr integer results into `solar::Error` and return
`Result`. Timeout, no-wait, cancellation, empty, and full distinctions remain
represented by `Status` inside the error.

Native callbacks and Zephyr APIs continue to use their required signatures.
Conversion occurs once at the Solar boundary. Native error values should be
preserved in `Error::native` when useful.

Destructor paths that cannot return a Result must use an explicitly documented
containment or reporting policy; they must not silently pretend a fallible
operation is infallible merely to fit RAII.

## 12. Generic Handling

Generic infrastructure may inspect classification without knowing the error
domain:

```cpp
template<solar::ErrorType E>
void observe_failure(const E& error)
{
    const auto classification = status_of(error);
    // Record only under the caller's explicit observability policy.
}
```

Remote, logging, events, health, and Supervisor must not infer that every
`ErrorType` is automatically serializable or publishable. Classification and
external representation are separate capabilities.

## 13. Rejected Alternatives

The following are rejected:

- retaining `Status` as the default Result error;
- returning `Status` from ordinary fallible public APIs;
- an exception hierarchy or virtual base error;
- a universal error variant;
- heap-backed error type erasure;
- wrapping `std::expected` in a custom Solar class;
- implicit subsystem-error conversion;
- automatic logging or event emission on every returned error;
- accepting both status-only and Result forms indefinitely.

## 14. Migration And Verification

Implementation proceeds as a hard migration:

1. add `solar::Error`, `ErrorType`, the new Result default, and typed `fail`;
2. add `status_of` to every current subsystem and protocol error;
3. migrate direct kernel and protocol Status returns;
4. generalize lifecycle hook and execution-protocol handling;
5. update Remote error schemas and testing links;
6. update all call sites and examples to `fail<ErrorType>({...})`;
7. add compile-fail coverage for Status as a Result error and for error types
   without `status_of`;
8. test default, typed, enum, move-only, constexpr, and monadic Results;
9. run host, native Zephyr, strict/relaxed, disabled-subsystem, generation, and
   firmware integration gates;
10. record storage and binary effects of the larger default error where they
    are material.

Public documentation must use this convention once migration is complete.
