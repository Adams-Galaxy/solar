#pragma once

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

#include "solar/core/status.hpp"

namespace solar::parameters
{

namespace detail
{

template <typename T, bool Enum = std::is_enum_v<T>, bool Floating = std::is_floating_point_v<T>>
struct ScalarRepresentation;

template <typename T> struct ScalarRepresentation<T, false, false>
{
    using type = std::make_unsigned_t<T>;
};

template <> struct ScalarRepresentation<bool, false, false>
{
    using type = std::uint8_t;
};

template <typename T> struct ScalarRepresentation<T, true, false>
{
    using type = std::make_unsigned_t<std::underlying_type_t<T>>;
};

template <typename T> struct ScalarRepresentation<T, false, true>
{
    using type = std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>;
};

template <typename T> using scalar_representation_t = typename ScalarRepresentation<T>::type;

template <typename T> [[nodiscard]] constexpr scalar_representation_t<T> scalar_bits(T value)
{
    using Rep = scalar_representation_t<T>;
    if constexpr (std::is_floating_point_v<T>) {
        static_assert(sizeof(T) == 4 || sizeof(T) == 8,
                      "SOLAR_DIAGNOSTIC_PARAMETER_FLOAT_CODEC: only 32-bit and 64-bit floating "
                      "point values have a default codec");
        return std::bit_cast<Rep>(value);
    } else if constexpr (std::is_enum_v<T>) {
        return static_cast<Rep>(static_cast<std::underlying_type_t<T>>(value));
    } else {
        return static_cast<Rep>(value);
    }
}

template <typename T> [[nodiscard]] constexpr T scalar_value(scalar_representation_t<T> bits)
{
    if constexpr (std::is_floating_point_v<T>) {
        return std::bit_cast<T>(bits);
    } else {
        return static_cast<T>(bits);
    }
}

} // namespace detail

template <typename T> struct ScalarCodec
{
    static_assert(std::is_integral_v<T> || std::is_enum_v<T> || std::is_floating_point_v<T>,
                  "SOLAR_DIAGNOSTIC_PARAMETER_DEFAULT_CODEC: default codec supports scalar "
                  "integral, enum, and floating-point values only");

    using Representation = detail::scalar_representation_t<T>;
    static constexpr std::size_t encoded_size = sizeof(Representation);

    [[nodiscard]] static constexpr Result<std::size_t> encode(T value,
                                                              std::span<std::byte> output) noexcept
    {
        if (output.size() < encoded_size) {
            return fail<solar::Error>({.status = solar::Status::NoBuffer});
        }
        auto bits = detail::scalar_bits(value);
        for (std::size_t index = 0; index < encoded_size; ++index) {
            output[index] = static_cast<std::byte>((bits >> (index * 8U)) & 0xffU);
        }
        return encoded_size;
    }

    [[nodiscard]] static constexpr Result<T> decode(std::span<const std::byte> input) noexcept
    {
        if (input.size() != encoded_size) {
            return fail<solar::Error>({.status = solar::Status::MessageTooLarge});
        }
        Representation bits{};
        for (std::size_t index = 0; index < encoded_size; ++index) {
            bits |= static_cast<Representation>(std::to_integer<unsigned int>(input[index]))
                    << (index * 8U);
        }
        return detail::scalar_value<T>(bits);
    }
};

namespace detail
{

template <typename Parameter, typename = void> struct CodecFor
{
    using type = ScalarCodec<typename Parameter::Value>;
};

template <typename Parameter> struct CodecFor<Parameter, std::void_t<typename Parameter::Codec>>
{
    using type = typename Parameter::Codec;
};

template <typename Parameter> using codec_for_t = typename CodecFor<Parameter>::type;

template <typename Codec, typename Value>
concept CodecForValue =
    requires(Value value, std::span<std::byte> output, std::span<const std::byte> input) {
        { Codec::encoded_size } -> std::convertible_to<std::size_t>;
        { Codec::encode(value, output) } -> std::same_as<Result<std::size_t>>;
        { Codec::decode(input) } -> std::same_as<Result<Value>>;
    };

} // namespace detail

} // namespace solar::parameters
