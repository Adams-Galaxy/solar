#pragma once

#include <utility>

namespace solar
{

/**
 * @brief Small status vocabulary shared by Solar core, RTOS wrappers, and HAL.
 */
enum class Status
{
    Ok,
    Timeout,
    NoMemory,
    Invalid,
    Full,
    Empty,
    NotReady,
    NotFound,
    DependencyFailed,
    Error,
};

/**
 * @brief Convenience predicate for status-returning APIs.
 */
constexpr bool ok(Status status)
{
    return status == Status::Ok;
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
