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
