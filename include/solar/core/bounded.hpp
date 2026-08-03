#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace solar
{

template <std::size_t Capacity> struct BoundedText
{
    static_assert(Capacity <= UINT16_MAX);
    std::array<char, Capacity> storage{};
    std::uint16_t size{};

    [[nodiscard]] constexpr std::string_view view() const noexcept
    {
        return {storage.data(), size};
    }
};

template <std::size_t Capacity> struct BoundedBytes
{
    static_assert(Capacity <= UINT16_MAX);
    std::array<std::byte, Capacity> storage{};
    std::uint16_t size{};

    [[nodiscard]] constexpr std::span<const std::byte> view() const noexcept
    {
        return {storage.data(), size};
    }
};

/** Fixed storage with a bounded runtime length and no allocation. */
template <typename Value, std::size_t Capacity> struct BoundedVector
{
    static_assert(Capacity <= UINT16_MAX);
    std::array<Value, Capacity> storage{};
    std::uint16_t size{};

    [[nodiscard]] constexpr std::span<Value> view() noexcept
    {
        return {storage.data(), size};
    }

    [[nodiscard]] constexpr std::span<const Value> view() const noexcept
    {
        return {storage.data(), size};
    }
};

} // namespace solar
