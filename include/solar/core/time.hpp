#pragma once

#include <chrono>
#include <cstdint>
#include <limits>
#include <ratio>

namespace solar
{

/**
 * @brief Common duration aliases used by Solar public APIs.
 *
 * Platform-specific tick conversion is kept under `solar::kernel`; these
 * aliases are stable application vocabulary.
 */
using Duration = std::chrono::milliseconds;
using Milliseconds = std::chrono::milliseconds;
using Microseconds = std::chrono::microseconds;
using Seconds = std::chrono::seconds;

/**
 * Compile-time duration value suitable for use as a C++ non-type template
 * parameter. `std::chrono::duration` is not a structural type in C++23.
 */
struct DurationValue
{
    std::int64_t nanoseconds{};

    template <typename Rep, typename Period>
    consteval DurationValue(std::chrono::duration<Rep, Period> value)
        : nanoseconds(std::chrono::duration_cast<std::chrono::nanoseconds>(value).count())
    {}

    [[nodiscard]] constexpr std::chrono::nanoseconds duration() const noexcept
    {
        return std::chrono::nanoseconds{nanoseconds};
    }

    [[nodiscard]] constexpr bool positive() const noexcept
    {
        return nanoseconds > 0;
    }

    friend constexpr bool operator==(DurationValue, DurationValue) noexcept = default;
    friend constexpr auto operator<=>(DurationValue, DurationValue) noexcept = default;
};

namespace literals
{

namespace detail
{

template <std::int64_t Scale>
[[nodiscard]] consteval DurationValue duration_literal(unsigned long long value)
{
    static_assert(Scale > 0);
    if (value > static_cast<unsigned long long>(std::numeric_limits<std::int64_t>::max() / Scale)) {
        __builtin_trap();
    }
    return DurationValue{std::chrono::nanoseconds{static_cast<std::int64_t>(value) * Scale}};
}

} // namespace detail

[[nodiscard]] consteval DurationValue operator""_ns(unsigned long long value)
{
    return detail::duration_literal<1>(value);
}

[[nodiscard]] consteval DurationValue operator""_us(unsigned long long value)
{
    return detail::duration_literal<1'000>(value);
}

[[nodiscard]] consteval DurationValue operator""_ms(unsigned long long value)
{
    return detail::duration_literal<1'000'000>(value);
}

[[nodiscard]] consteval DurationValue operator""_s(unsigned long long value)
{
    return detail::duration_literal<1'000'000'000>(value);
}

} // namespace literals

} // namespace solar
