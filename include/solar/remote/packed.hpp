#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>

#include "solar/remote/declaration.hpp"

namespace solar::remote::packed
{

namespace detail
{
template <typename T> struct WireType
{
    using type = std::conditional_t<std::is_enum_v<T>, std::underlying_type_t<T>, T>;
};

template <typename T>
    requires(!std::is_enum_v<T>)
struct WireType<T>
{
    using type = T;
};

template <typename T> using wire_type_t = typename WireType<T>::type;

template <typename T>
inline constexpr bool supported_v =
    std::is_same_v<T, bool> || std::integral<T> || std::is_enum_v<T> || std::is_same_v<T, float> ||
    std::is_same_v<T, double>;

template <typename T> [[nodiscard]] constexpr auto bits(T value)
{
    using Wire = wire_type_t<T>;
    if constexpr (std::is_same_v<Wire, float>) {
        return std::bit_cast<std::uint32_t>(value);
    } else if constexpr (std::is_same_v<Wire, double>) {
        return std::bit_cast<std::uint64_t>(value);
    } else if constexpr (std::is_same_v<Wire, bool>) {
        return static_cast<std::uint8_t>(value);
    } else {
        return static_cast<std::make_unsigned_t<Wire>>(static_cast<Wire>(value));
    }
}

template <typename T, typename Bits> [[nodiscard]] constexpr T from_bits(Bits value)
{
    using Wire = wire_type_t<T>;
    if constexpr (std::is_same_v<Wire, float>) {
        return std::bit_cast<float>(static_cast<std::uint32_t>(value));
    } else if constexpr (std::is_same_v<Wire, double>) {
        return std::bit_cast<double>(static_cast<std::uint64_t>(value));
    } else if constexpr (std::is_same_v<Wire, bool>) {
        return value != 0;
    } else if constexpr (std::is_enum_v<T>) {
        return static_cast<T>(static_cast<Wire>(value));
    } else {
        return static_cast<T>(value);
    }
}

template <typename T> bool write(T value, std::span<std::byte> output, std::size_t& offset) noexcept
{
    static_assert(supported_v<T>,
                  "SOLAR_DIAGNOSTIC_REMOTE_PACKED_FIELD: Packed supports fixed scalar fields");
    using Bits = decltype(bits(value));
    if (offset + sizeof(Bits) > output.size()) {
        return false;
    }
    auto encoded = bits(value);
    for (std::size_t index{}; index < sizeof(Bits); ++index) {
        output[offset++] = static_cast<std::byte>((encoded >> (index * 8U)) & 0xFFU);
    }
    return true;
}

template <typename T>
bool read(T& value, std::span<const std::byte> input, std::size_t& offset) noexcept
{
    static_assert(supported_v<T>,
                  "SOLAR_DIAGNOSTIC_REMOTE_PACKED_FIELD: Packed supports fixed scalar fields");
    using Bits = decltype(bits(value));
    if (offset + sizeof(Bits) > input.size()) {
        return false;
    }
    Bits encoded{};
    for (std::size_t index{}; index < sizeof(Bits); ++index) {
        encoded |= static_cast<Bits>(std::to_integer<std::uint8_t>(input[offset++]))
                   << (index * 8U);
    }
    value = from_bits<T>(encoded);
    return true;
}

template <typename Value, typename FieldsT> struct Codec;

template <typename Value, typename... FieldTypes> struct Codec<Value, Fields<FieldTypes...>>
{
    static constexpr bool valid = (supported_v<remote::detail::field_member_t<FieldTypes>> && ...);
    static constexpr std::size_t size =
        (sizeof(wire_type_t<remote::detail::field_member_t<FieldTypes>>) + ... + 0U);

    static Result<std::size_t, Error> encode(const Value& value,
                                             std::span<std::byte> output) noexcept
    {
        static_assert((supported_v<remote::detail::field_member_t<FieldTypes>> && ...),
                      "SOLAR_DIAGNOSTIC_REMOTE_PACKED_FIELD: Packed schema contains a non-fixed "
                      "field");
        std::size_t offset{};
        if (!(write(value.*FieldTypes::member, output, offset) && ...)) {
            return fail<Error>({Status::NoSpace, Reason::NoSpace, Operation::Pack});
        }
        return offset;
    }

    static Result<Value, Error> decode(std::span<const std::byte> input) noexcept
    {
        if (input.size() != size) {
            return fail<Error>({Status::ProtocolError, Reason::Malformed, Operation::Unpack});
        }
        Value value{};
        std::size_t offset{};
        if (!(read(value.*FieldTypes::member, input, offset) && ...)) {
            return fail<Error>({Status::ProtocolError, Reason::Malformed, Operation::Unpack});
        }
        return value;
    }
};
} // namespace detail

template <typename Value>
inline constexpr std::size_t encoded_size = [] {
    static_assert(validate_schema<Value>());
    static_assert(Schema<Value>::codec == Codec::Packed,
                  "SOLAR_DIAGNOSTIC_REMOTE_PACKED_CODEC: Schema must explicitly select Packed");
    static_assert(detail::Codec<Value, typename Schema<Value>::Fields>::valid,
                  "SOLAR_DIAGNOSTIC_REMOTE_PACKED_FIELD: Packed schema contains a non-fixed "
                  "field");
    return detail::Codec<Value, typename Schema<Value>::Fields>::size;
}();

template <typename Value>
[[nodiscard]] Result<std::size_t, Error> encode(const Value& value,
                                                std::span<std::byte> output) noexcept
{
    (void)encoded_size<Value>;
    return detail::Codec<Value, typename Schema<Value>::Fields>::encode(value, output);
}

template <typename Value>
[[nodiscard]] Result<Value, Error> decode(std::span<const std::byte> input) noexcept
{
    (void)encoded_size<Value>;
    return detail::Codec<Value, typename Schema<Value>::Fields>::decode(input);
}

} // namespace solar::remote::packed
