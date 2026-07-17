# Modern C++ And Solar Result Design

Date: 2026-07-13

Status: accepted companion specification

Parent specification:
`development-docs/design-specs/00-design-conventions.md`

## 1. Purpose

This document defines Solar's modern C++ posture and the redesign of
`solar::Status` and `solar::Result` around C++23 `std::expected`.

It is intentionally separate from the shared vocabulary specification because
error representation, conversion, composition, and migration deserve focused
treatment.

## 2. Language Baseline

Solar requires C++23.

Applications using Solar must enable the corresponding Zephyr configuration:

```ini
CONFIG_CPP=y
CONFIG_STD_CPP23=y
CONFIG_REQUIRES_FULL_LIBCPP=y
```

Individual builds may require additional standard-library or libc selection
according to their Zephyr platform configuration.

Solar may use complete standardized C++23 facilities rather than coding to an
older common denominator. In particular, Solar may rely on:

- `std::expected` and `std::unexpected`;
- `std::expected::and_then`;
- `std::expected::transform`;
- `std::expected::or_else`;
- `std::expected::transform_error`;
- concepts and requires-expressions;
- expanded `constexpr` and `consteval` use;
- ranges and views where appropriate;
- standard vocabulary types;
- modern attributes and type traits.

Solar tracks supported Zephyr and Zephyr SDK releases. It does not promise
compatibility with arbitrary older compilers merely because they can parse part
of the public headers.

When a future C++ standard is sufficiently supported and stable in the Zephyr
ecosystem, Solar may raise its baseline. C++26 reflection is particularly
relevant to descriptors, catalogs, schemas, and diagnostics, but no current API
may assume reflection before that baseline is formally adopted.

## 3. Toolchain Contract

Solar should verify required language and library capabilities directly.

Illustrative checks:

```cpp
static_assert(__cplusplus >= 202302L,
              "Solar requires C++23 or newer");

#ifndef __cpp_lib_expected
#error "Solar requires the C++23 <expected> library"
#endif

static_assert(__cpp_lib_expected >= 202211L,
              "Solar requires std::expected with monadic operations");
```

The exact feature-test thresholds must follow the facilities Solar actually
uses. A missing capability should produce one clear Solar diagnostic near the
public include boundary.

Solar must not maintain a reduced substitute implementation merely to support
an underspecified standard library. Toolchain requirements are part of the
framework contract.

## 4. Modern Does Not Mean Unbounded

Modern C++ is used to improve correctness, composition, diagnostics, and
expressiveness. It is not permission for hidden firmware cost.

Solar remains guided by:

- bounded storage;
- allocation-free core paths by default;
- no exceptions in ordinary control flow;
- explicit execution and synchronization;
- deterministic lifecycle;
- visible ownership;
- compile-time validation;
- understandable generated code and diagnostics.

Standard facilities should be evaluated by semantics and generated behavior,
not rejected because they are modern or accepted merely because they are new.

## 5. Status

`solar::Status` is Solar's broad framework error classification.

It is appropriate for:

- lifecycle hook outcomes;
- boot and stop classification;
- generic kernel-wrapper outcomes;
- APIs where callers need only a stable broad reason;
- mapping a richer subsystem failure across a generic boundary.

It is not intended to encode every domain-specific failure.

Representative vocabulary:

```cpp
enum class Status : int
{
    Ok = 0,
    Error,
    Invalid,
    NotReady,
    NotFound,
    NotSupported,
    Busy,
    Already,
    Timeout,
    Cancelled,
    NoMemory,
    NoSpace,
    WouldBlock,
    DependencyFailed,
    UnexpectedExit,
};
```

The final set and errno relationship will be reviewed during implementation.
The semantic rule is fixed: `Status` remains compact, stable, and broadly
interpretable.

## 6. Typed Subsystem Errors

Subsystems should define typed errors when callers need a precise reason or
policy decision.

Examples:

```cpp
namespace solar::parameters
{
enum class Error
{
    NotRegistered,
    ReadOnly,
    ValidationRejected,
    PersistenceUnavailable,
    PersistenceFailed,
    VersionMismatch,
};
}

namespace solar::bus
{
enum class Error
{
    NotRegistered,
    PayloadInvalid,
    QueueFull,
    ExecutorUnavailable,
    ReentrantDeliveryRejected,
};
}
```

Typed errors should be:

- small value types, normally scoped enums or compact structs;
- allocation-free;
- equality comparable;
- suitable for `constexpr` mapping;
- independently describable for logs, events, and Remote errors;
- stable according to their documented public contract.

An error type may carry bounded structured context where a bare enum is
insufficient:

```cpp
struct ValidationError
{
    parameters::Reason reason;
    parameters::ParameterId parameter;
};
```

It must not carry borrowed transient strings or unbounded data by default.

## 7. Result Alias

Solar adopts `std::expected` as its canonical fallible-value representation.

```cpp
#include <expected>

namespace solar
{

template<typename T, typename E = Status>
using Result = std::expected<T, E>;

template<typename E>
using Failure = std::unexpected<E>;

}
```

`solar::Result` is an alias, not a wrapper class.

This preserves:

- direct interoperability with standard C++;
- standard value categories and observers;
- monadic operations;
- constexpr behavior;
- standard-library optimization;
- familiar semantics for users.

Solar must not wrap `std::expected` merely to retain the old `.status()` API.
Compatibility convenience belongs in free functions or temporary migration
helpers.

## 8. Constructing Success And Failure

Success returns a value directly:

```cpp
solar::Result<Reading, SensorError> read()
{
    return Reading{...};
}
```

Failure uses `std::unexpected`:

```cpp
return std::unexpected(SensorError::NotReady);
```

Solar may provide a concise helper:

```cpp
namespace solar
{
template<typename E>
constexpr auto fail(E error)
{
    return std::unexpected<E>{std::move(error)};
}
}
```

Usage:

```cpp
return solar::fail(SensorError::NotReady);
```

The helper must remain a transparent producer of `std::unexpected`, not a new
error transport abstraction.

`Result<void, E>` uses `std::expected<void, E>` directly:

```cpp
solar::Result<void, parameters::Error> save();
```

## 9. Monadic Composition

Solar encourages monadic composition where it makes control flow clearer.

### 9.1 Transforming a value

```cpp
auto speed = parameters::get<DriveScale>()
    .transform([](float scale) {
        return raw_speed * scale;
    });
```

### 9.2 Chaining fallible operations

```cpp
auto result = parameters::get<DriveKp>()
    .and_then([](float kp) {
        return Controller::configure(kp);
    });
```

### 9.3 Recovering or observing failure

```cpp
auto result = Storage::load<DriveKp>()
    .or_else([](StorageError error) {
        events::observe<ParameterLoadFailed>({error});
        return Storage::default_value<DriveKp>();
    });
```

### 9.4 Mapping an error boundary

```cpp
auto result = parameters::load_all()
    .transform_error([](parameters::Error error) {
        return status_of(error);
    });
```

Monadic chains should not be used when a straightforward branch is clearer,
especially when multiple observable side effects or rollback steps are
involved.

The goal is compositional clarity, not point-free style.

## 10. Error Mapping

Typed subsystem errors remain typed until crossing a boundary that explicitly
requires `Status` or another error vocabulary.

Each subsystem should define a mapping where needed:

```cpp
constexpr solar::Status status_of(parameters::Error error)
{
    switch (error)
    {
    case parameters::Error::NotRegistered:
        return solar::Status::NotFound;
    case parameters::Error::ReadOnly:
        return solar::Status::PermissionDenied;
    case parameters::Error::ValidationRejected:
        return solar::Status::Invalid;
    case parameters::Error::PersistenceUnavailable:
        return solar::Status::NotSupported;
    case parameters::Error::PersistenceFailed:
    case parameters::Error::VersionMismatch:
        return solar::Status::Error;
    }
    return solar::Status::Error;
}
```

Mapping is intentionally lossy and therefore must be explicit.

Lifecycle may receive broad status while subsystem diagnostics retain the
original typed reason:

```cpp
static solar::Status init()
{
    auto loaded = solar::parameters::load();
    if (!loaded)
    {
        Parameters::record_failure(loaded.error());
        return solar::status_of(loaded.error());
    }
    return solar::Status::Ok;
}
```

## 11. Status-Only APIs

An operation that has no success value and no useful domain-specific error may
return either:

```cpp
solar::Status
```

or:

```cpp
solar::Result<void>
```

The owning subsystem specification must choose consistently.

Guideline:

- use `Status` at low-level compatibility and lifecycle boundaries;
- use `Result<void, E>` in composable public subsystem APIs;
- avoid offering both spellings for the same operation without a migration
  reason.

Long-term, public subsystem APIs should generally prefer `Result<void, E>` when
they participate in expected-based composition.

## 12. Observing Results Safely

Normal code should test or compose a result before dereferencing it:

```cpp
auto value = parameters::get<DriveKp>();
if (!value)
{
    return solar::fail(value.error());
}

Controller::set_kp(*value);
```

or:

```cpp
return parameters::get<DriveKp>()
    .and_then(Controller::set_kp);
```

Calling `.value()` on an error may involve `std::bad_expected_access` according
to the standard library and build configuration. Solar code should not rely on
that exception path for ordinary control flow.

Unchecked dereference is acceptable only when correctness is established by an
immediately visible condition or compile-time invariant.

## 13. Exceptions

C++23 support does not imply that Solar should use exceptions as ordinary
firmware error flow.

Solar's baseline remains explicit values:

- `Status`;
- typed error values;
- `std::expected`;
- observable failure records.

Applications may independently enable C++ exceptions where Zephyr and their
resource policy support them. Solar APIs must remain usable without exceptions.

Solar should avoid public contracts whose only failure channel is throwing.

## 14. `noexcept`

Solar should use `noexcept` when an operation's implementation and dependencies
genuinely guarantee it.

It should not add `noexcept` decoratively. Incorrect `noexcept` turns an
unexpected throw into termination and can obscure library constraints.

Pure status/expected-based functions using non-throwing primitives are good
candidates. Later subsystem specifications should identify their exception and
allocation assumptions.

## 15. Concepts For Expected-Like Results

Generic Solar internals may need constrained result concepts.

Illustrative shape:

```cpp
template<typename T>
concept ExpectedLike = requires(T value)
{
    typename T::value_type;
    typename T::error_type;
    { value.has_value() } -> std::same_as<bool>;
    value.error();
};
```

Concepts should constrain the semantic contract actually needed by an
algorithm. They should not imitate every member of `std::expected` or enable a
second competing result family.

Public APIs should return `solar::Result`, not merely an arbitrary
`ExpectedLike` type, unless customization is an explicit requirement.

## 16. Lifecycle Hook Results

Lifecycle hooks are optional static functions. Their final accepted return
contract will be settled in the lifecycle specification, but this document
recommends:

```cpp
static solar::Status init();
static solar::Status start();
static solar::Status stop();
static solar::Status deinit();
```

or explicitly supported:

```cpp
static solar::Result<void> init();
```

Solar should not indefinitely normalize arbitrary `bool`, integral, or unknown
return types. Compatibility normalization may exist during migration, then be
removed.

Subsystem-specific lifecycle preparation should retain its rich error in the
subsystem record and map deliberately to `Status` at the hook boundary.

## 17. Disabled Features

Expected-based APIs provide a clear disabled-feature result where runtime use
is intentionally allowed:

```cpp
return solar::fail(Status::NotSupported);
```

or:

```cpp
return solar::fail(parameters::Error::SubsystemUnavailable);
```

However, `std::expected` must not be used to postpone an invalid compile-time
composition decision until runtime. Calls requiring absent canonical state
should fail during compilation when availability is statically known.

## 18. Error Observability

Returning an error and recording an operational fact are separate decisions.

An API may:

- return an error only;
- return an error and update subsystem diagnostics;
- return an error and record an observability event;
- intentionally drop an error under a documented fire-and-forget policy.

The error transport must not automatically log or observe every failure. That
would create hidden side effects and duplicate diagnostics.

Subsystem specs must define which failures are recorded and where canonical
diagnostics live.

## 19. Embedded Cost Model

`std::expected` is a value type and should require no dynamic allocation for
bounded value and error types.

Nevertheless, each result API must consider:

- size of `T` and `E`;
- copying across queue or ISR boundaries;
- destruction cost;
- alignment;
- whether a reference/view can outlive its owner;
- code-size effects of large monadic lambdas and repeated template
  instantiations.

Large payloads should use bounded caller-owned output, views with explicit
lifetime, or subsystem storage rather than assuming `expected` makes copying
free.

## 20. Migration From Current Solar Result

The implementation pass should migrate in deliberate stages.

### Stage 1: baseline

- Enable C++23 in firmware and all Solar tests.
- Require full standard-library support.
- Add central language/library capability checks.
- Introduce the `solar::Result<T, E>` alias and `solar::fail` helper.

### Stage 2: call sites

- Replace `.status()` with `.error()` after checking failure.
- Replace implicit failure construction with `std::unexpected` or
  `solar::fail`.
- Replace custom `Result<void>` behavior with `std::expected<void, E>`.
- Migrate value access to dereference, guarded `.value()`, or monadic
  composition.

### Stage 3: typed errors

- Introduce subsystem errors as each subsystem is redesigned.
- Add explicit `status_of(error)` mappings at generic boundaries.
- Preserve typed reasons in subsystem records.
- Remove temporary broad-status flattening.

### Stage 4: cleanup

- Delete the custom result implementation.
- Delete obsolete normalization helpers.
- Remove compatibility constructors and status accessors.
- Add compile tests for expected-based public contracts.

The migration should avoid maintaining two public Result types for an extended
period.

## 21. Testing Obligations

The implementation must test:

- C++23 and feature-test macro requirements;
- `Result<T>` success and failure;
- `Result<void>` success and failure;
- typed errors;
- move-only values and errors where supported;
- constexpr result composition;
- all four monadic operations;
- explicit typed-error-to-Status mapping;
- no unintended dynamic allocation in core result paths;
- native simulator and target-toolchain compilation;
- clear diagnostics under an unsupported standard-library configuration.

## 22. Rejected Alternatives

### 22.1 Keep the custom `Result<T>` indefinitely

Rejected. It duplicates a standard C++ facility, lacks the standard monadic
interface, and creates maintenance and interoperability cost.

### 22.2 Wrap `std::expected` in a Solar class

Rejected as the default. A wrapper would need to preserve conversions, value
categories, constexpr behavior, observers, and monadic operations while adding
little architectural value.

### 22.3 Use `Status` for every failure

Rejected. It erases domain-specific reasons and makes policy decisions depend
on broad classifications.

### 22.4 Use exceptions as Solar's primary error model

Rejected. Explicit expected values better match deterministic firmware control
flow and remain usable in exception-disabled builds.

### 22.5 Avoid modern library features for hypothetical old toolchains

Rejected. Solar tracks modern supported Zephyr toolchains and declares its
baseline clearly.

### 22.6 Use monadic chains everywhere

Rejected. Monadic operations are encouraged where they improve composition,
not as a stylistic requirement that obscures side effects or rollback.

## 23. Forward Compatibility

Solar expects to adopt later language standards when their implementation is
mature in supported Zephyr toolchains.

Future reflection may simplify:

- descriptor declaration;
- schema generation;
- field metadata;
- compile-time catalogs;
- protocol manifests;
- structured event and parameter definitions.

Current designs should keep metadata concepts clean enough to migrate toward
standard reflection, but must not invent a large proprietary reflection system
solely to predict C++26.

## 24. Final Decisions

- C++23 is required.
- Full C++23 `std::expected` support, including monadic operations, is required.
- Solar may use modern standard features throughout its implementation and
  public APIs.
- `solar::Result<T, E>` is an alias of `std::expected<T, E>`.
- `solar::Status` is the default broad error type.
- Subsystems should use typed errors when callers need precise reasons.
- Error mapping is explicit and intentionally lossy.
- Solar does not use exceptions as its primary error model.
- Modern C++ usage remains bounded, deterministic, and explicit about cost.
- Solar tracks supported Zephyr/SDK releases and may raise its language baseline
  as newer standards mature.

## 25. References

- Zephyr C++ language support:
  `https://docs.zephyrproject.org/latest/develop/languages/cpp/index.html`
- Zephyr 4.4 release notes:
  `https://docs.zephyrproject.org/latest/releases/release-notes-4.4.html`
- Zephyr SDK releases:
  `https://github.com/zephyrproject-rtos/sdk-ng/releases`
- C++23 standard library header `<expected>`.
