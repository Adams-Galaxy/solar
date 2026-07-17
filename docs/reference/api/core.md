# Core API

## Version

```{doxygenstruct} solar::Version
:members:
```

```{doxygenvariable} solar::version
```

## Results And Errors

```{doxygenenum} solar::Status
```

```{doxygenstruct} solar::Error
:members:
```

```cpp
template <typename E>
concept ErrorType = /* bounded error with status_of(error) */;

template <typename T, ErrorType E = Error>
using Result = std::expected<T, E>;

template <ErrorType E>
using Failure = std::unexpected<E>;

template <ErrorType E>
[[nodiscard]] constexpr auto fail(E error) -> Failure<E>;
```

`Result<T, E>` is Solar's direct C++23 `std::expected` result. Use
`Result<void>` for fallible commands and `fail<ErrorType>({...})` to return a
domain error explicitly.
