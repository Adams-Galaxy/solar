# Result And Errors

Every ordinary fallible Solar operation returns:

```cpp
template<typename T, solar::ErrorType E = solar::Error>
using Result = std::expected<T, E>;
```

`Result<void>` represents a command that can fail. Success is a populated
Result, not `Status::Ok`.

`solar::Error` is the compact default error:

```cpp
struct Error {
    solar::Status status;
    int native;
};
```

Subsystems use richer error types with reason, operation, declaration identity,
or bounded context. Every error satisfies `ErrorType` by exposing a non-throwing
`status_of(error)` projection.

Construct failures explicitly:

```cpp
return solar::fail<solar::Error>({
    .status = solar::Status::NotReady,
});
```

Keep rich errors until a boundary intentionally classifies them. Returning an
error does not automatically log, publish an event, update Health, or serialize
it through Remote.

Because Result is `std::expected`, C++23 operations such as `and_then`,
`transform`, `or_else`, and `transform_error` are available without a wrapper.

## Crossing Error Boundaries: `fail_as`

`fail_as<Target>(source)` converts an error from one module's error domain
into another's, projecting through `status_of` when the source doesn't carry
a richer, intentional mapping. This conversion is deliberately lossy on its
own — it keeps only what `status_of` can recover. Preserving more detail
across a boundary is the job of `Traced`, below, not of hand-building a
richer `Target` at every call site.

## Exceptions And RTTI

Solar is exceptions-off and RTTI-off, everywhere it builds — including the
host test toolchain, not only the Zephyr target where `CONFIG_CPP_EXCEPTIONS`
and `CONFIG_CPP_RTTI` happen to default to `n`. This is enforced by Solar's
own build configuration and by a compile-fail fixture, not left as an
inherited platform default that a different toolchain could silently
disagree with.

One consequence: `noexcept` is largely documentation, not a guarantee the
compiler needed help enforcing, since throwing is not physically reachable.
It stays load-bearing in a narrow set of places where it affects something
other than "can this throw" — destructors, move construction/assignment, and
swap (library and container behavior keys off these), and anywhere a
concept or `static_assert` inspects a `std::is_nothrow_*` trait, which
includes `ErrorType`'s nothrow-destructible requirement. Everywhere else, an
absent `noexcept` on Solar or firmware code is not a gap to fill in.

## Error Traces

Converting an error at a boundary (`fail_as`, or a hand-built `Target`)
necessarily discards the deposed error's extra fields. What's lost is not
gone by accident — `fail_as` is explicit about it — but by default nothing
records *where* it was lost or what the call chain looked like on the way
up.

`Traced<E, N>` is an optional, self-contained wrapper around any `ErrorType`
that accumulates a bounded chain of boundary crossings:

```cpp
template <ErrorType E, std::size_t N = kErrorTraceDepth>
struct Traced {
    E error;
    std::array<Frame, N> frames{};
    std::uint8_t count = 0;
};
```

`N` is fixed project-wide by `kErrorTraceDepth`, a single compile-time
constant (`CONFIG_SOLAR_ERROR_TRACE_DEPTH` on Zephyr, an equivalent CMake
cache variable on host), not chosen per call site or per module. At the
default depth of `0`, `Traced<E, 0>` has identical layout and codegen to `E`
itself, and `.with_context(...)` compiles away entirely — normal and
production builds pay nothing for this feature. Flipping the depth to a
small positive `N` for a debug build turns on a real, self-contained
breadcrumb trail: each `fail_as` crossing appends a frame (source location,
status, and a short tag) instead of discarding one, and the accumulated
value can be formatted at the point it's finally handled, with no
dependency on log capture having been running at the time.

This was deliberately chosen over two alternatives:

- A pure log-correlation id (attach a monotonic id to the error, log the
  detail, reconcile later by grepping the id): cheaper in `sizeof`, but the
  causal chain lives only in the log stream — if logs aren't captured when
  the fault happens, the id resolves to nothing.
- A shared ring buffer of frames with only a compact handle stored in the
  error: gets the same storage cost as the id-correlation scheme with
  structured (not text) frames, but introduces ownership questions (which
  context owns the buffer, how it behaves under concurrent writers, what an
  independent/isolated module is supposed to do) that a self-contained value
  type does not have.

The immediate log line Solar already emits at a `fail_as` crossing is
unrelated to trace depth and always fires — trace depth controls only
whether the *value* also carries the chain, not whether the crossing is
observable in the log.
