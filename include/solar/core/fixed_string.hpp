#pragma once

#include <cstddef>
#include <string_view>

namespace solar
{

/**
 * @brief Class-type string literal wrapper for compile-time identity helpers.
 *
 * `FixedString` exists so names can be template arguments, allowing components
 * to expose stable identities such as `solar::Name<"imu">` without runtime
 * registration or allocation.
 */
template <std::size_t N> struct FixedString
{
    char value[N]{};

    constexpr FixedString(const char (&text)[N])
    {
        for (std::size_t i = 0; i < N; ++i) {
            value[i] = text[i];
        }
    }

    constexpr const char* c_str() const
    {
        return value;
    }

    constexpr std::string_view view() const
    {
        return {value, N - 1};
    }

    static constexpr std::size_t size()
    {
        return N - 1;
    }

    static constexpr bool empty()
    {
        return N == 1;
    }

    constexpr bool operator==(const FixedString&) const = default;
};

template <std::size_t N> FixedString(const char (&)[N]) -> FixedString<N>;

/**
 * @brief Type-level stable name used by graph components and catalogs.
 */
template <FixedString Text> struct Name
{
    static constexpr auto text = Text;

    static constexpr const char* c_str()
    {
        return text.c_str();
    }

    static constexpr std::string_view view()
    {
        return text.view();
    }
};

} // namespace solar
