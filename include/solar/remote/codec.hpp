#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

#include "solar/remote/declaration.hpp"

#if defined(CONFIG_ZCBOR)
#include <zcbor_decode.h>
#include <zcbor_encode.h>
#endif

namespace solar::remote::cbor
{

#if defined(CONFIG_ZCBOR)
namespace detail
{

[[nodiscard]] constexpr std::uint16_t float_to_half(float value) noexcept
{
    const auto bits = std::bit_cast<std::uint32_t>(value);
    const auto sign = static_cast<std::uint16_t>((bits >> 16U) & 0x8000U);
    const auto exponent = static_cast<int>((bits >> 23U) & 0xFFU) - 127 + 15;
    auto mantissa = bits & 0x7FFFFFU;
    if (exponent <= 0) {
        if (exponent < -10) {
            return sign;
        }
        mantissa |= 0x800000U;
        const auto shift = static_cast<unsigned>(14 - exponent);
        const auto rounded =
            (mantissa + ((UINT32_C(1) << (shift - 1U)) - 1U) + ((mantissa >> shift) & 1U)) >> shift;
        return static_cast<std::uint16_t>(sign | rounded);
    }
    if (exponent >= 31) {
        return static_cast<std::uint16_t>(sign | 0x7C00U | (mantissa != 0 ? 0x0200U : 0U));
    }
    mantissa += 0x0FFFU + ((mantissa >> 13U) & 1U);
    if ((mantissa & 0x800000U) != 0) {
        mantissa = 0;
        if (exponent + 1 >= 31) {
            return static_cast<std::uint16_t>(sign | 0x7C00U);
        }
        return static_cast<std::uint16_t>(sign | ((exponent + 1) << 10U));
    }
    return static_cast<std::uint16_t>(sign | (exponent << 10U) | (mantissa >> 13U));
}

[[nodiscard]] constexpr float half_to_float(std::uint16_t value) noexcept
{
    const auto sign = static_cast<std::uint32_t>(value & 0x8000U) << 16U;
    auto exponent = static_cast<std::uint32_t>((value >> 10U) & 0x1FU);
    auto mantissa = static_cast<std::uint32_t>(value & 0x03FFU);
    std::uint32_t bits{};
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;
        } else {
            int adjustment{-1};
            do {
                ++adjustment;
                mantissa <<= 1U;
            } while ((mantissa & 0x0400U) == 0);
            mantissa &= 0x03FFU;
            bits =
                sign | static_cast<std::uint32_t>(127 - 15 - adjustment) << 23U | mantissa << 13U;
        }
    } else if (exponent == 31) {
        bits = sign | 0x7F800000U | mantissa << 13U;
    } else {
        bits = sign | (exponent + (127 - 15)) << 23U | mantissa << 13U;
    }
    return std::bit_cast<float>(bits);
}

template <typename T> bool encode_value(zcbor_state_t* state, const T& value)
{
    if constexpr (std::is_same_v<T, bool>) {
        return zcbor_bool_put(state, value);
    } else if constexpr (std::is_enum_v<T>) {
        static_assert(EnumerationSchemaType<T> || StatusSchemaType<T>,
                      "SOLAR_DIAGNOSTIC_REMOTE_MISSING_ENUM_SCHEMA: enum field requires an "
                      "enumeration Schema<T>");
        if constexpr (EnumerationSchemaType<T> &&
                      remote::detail::enum_openness<T>() == EnumOpenness::Closed) {
            if (!declared_enum_value(value)) {
                return false;
            }
        }
        return encode_value(state, static_cast<std::underlying_type_t<T>>(value));
    } else if constexpr (std::unsigned_integral<T>) {
        return zcbor_uint64_put(state, static_cast<std::uint64_t>(value));
    } else if constexpr (std::signed_integral<T>) {
        return zcbor_int64_put(state, static_cast<std::int64_t>(value));
    } else if constexpr (std::is_same_v<T, float>) {
        const auto half = float_to_half(value);
        return half_to_float(half) == value ? zcbor_float16_put(state, value)
                                            : zcbor_float32_put(state, value);
    } else if constexpr (std::is_same_v<T, double>) {
        const auto narrowed = static_cast<float>(value);
        return static_cast<double>(narrowed) == value ? encode_value(state, narrowed)
                                                      : zcbor_float64_put(state, value);
    } else if constexpr (remote::detail::IsBoundedText<T>::value) {
        zcbor_string text{reinterpret_cast<const std::uint8_t*>(value.storage.data()), value.size};
        return zcbor_tstr_encode(state, &text);
    } else if constexpr (remote::detail::IsBoundedBytes<T>::value) {
        zcbor_string bytes{reinterpret_cast<const std::uint8_t*>(value.storage.data()), value.size};
        return zcbor_bstr_encode(state, &bytes);
    }
}

template <typename T> bool decode_value(zcbor_state_t* state, T& value)
{
    if constexpr (std::is_same_v<T, bool>) {
        return zcbor_bool_decode(state, &value);
    } else if constexpr (std::is_enum_v<T>) {
        static_assert(EnumerationSchemaType<T> || StatusSchemaType<T>,
                      "SOLAR_DIAGNOSTIC_REMOTE_MISSING_ENUM_SCHEMA: enum field requires an "
                      "enumeration Schema<T>");
        std::underlying_type_t<T> decoded{};
        if (!decode_value(state, decoded)) {
            return false;
        }
        value = static_cast<T>(decoded);
        if constexpr (EnumerationSchemaType<T> &&
                      remote::detail::enum_openness<T>() == EnumOpenness::Closed) {
            return declared_enum_value(value);
        }
        return true;
    } else if constexpr (std::unsigned_integral<T>) {
        std::uint64_t decoded{};
        if (!zcbor_uint64_decode(state, &decoded) || decoded > UINT64_C(0xFFFFFFFFFFFFFFFF) ||
            decoded > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
            return false;
        }
        value = static_cast<T>(decoded);
        return true;
    } else if constexpr (std::signed_integral<T>) {
        std::int64_t decoded{};
        if (!zcbor_int64_decode(state, &decoded) ||
            decoded < static_cast<std::int64_t>(std::numeric_limits<T>::min()) ||
            decoded > static_cast<std::int64_t>(std::numeric_limits<T>::max())) {
            return false;
        }
        value = static_cast<T>(decoded);
        return true;
    } else if constexpr (std::is_same_v<T, float>) {
        return zcbor_float16_32_decode(state, &value);
    } else if constexpr (std::is_same_v<T, double>) {
        return zcbor_float_decode(state, &value);
    } else if constexpr (remote::detail::IsBoundedText<T>::value ||
                         remote::detail::IsBoundedBytes<T>::value) {
        zcbor_string decoded{};
        const bool okay = remote::detail::IsBoundedText<T>::value
                              ? zcbor_tstr_decode(state, &decoded)
                              : zcbor_bstr_decode(state, &decoded);
        if (!okay || decoded.len > value.storage.size()) {
            return false;
        }
        std::memcpy(value.storage.data(), decoded.value, decoded.len);
        value.size = static_cast<std::uint16_t>(decoded.len);
        return true;
    }
}

template <typename Value, typename FieldsT> struct Codec;

template <typename Value, typename... FieldTypes> struct Codec<Value, Fields<FieldTypes...>>
{
    template <typename FieldT> static std::size_t present(const Value& value)
    {
        if constexpr (remote::detail::is_optional_v<remote::detail::field_member_t<FieldT>>) {
            return static_cast<bool>(value.*FieldT::member) ? 1U : 0U;
        }
        return 1U;
    }

    template <typename FieldT> static bool encode_field(zcbor_state_t* state, const Value& value)
    {
        const auto& member = value.*FieldT::member;
        if constexpr (remote::detail::is_optional_v<std::remove_cvref_t<decltype(member)>>) {
            return !member || (zcbor_uint32_put(state, FieldT::id) && encode_value(state, *member));
        } else {
            return zcbor_uint32_put(state, FieldT::id) && encode_value(state, member);
        }
    }

    static Result<std::size_t, Error> encode(const Value& value,
                                             std::span<std::byte> output) noexcept
    {
        const auto count = (std::size_t{} + ... + present<FieldTypes>(value));
        auto* begin = reinterpret_cast<std::uint8_t*>(output.data());
        ZCBOR_STATE_E(state, 2, begin, output.size(), 1);
        if (!zcbor_map_start_encode(state, count) ||
            !(encode_field<FieldTypes>(state, value) && ...) ||
            !zcbor_map_end_encode(state, count)) {
            return fail<Error>({Status::NoSpace, Reason::NoSpace, Operation::Encode,
                                static_cast<std::uint32_t>(zcbor_peek_error(state))});
        }
        return static_cast<std::size_t>(state[0].payload - begin);
    }

    template <std::size_t Index, typename FieldT>
    static bool decode_field(FieldId key, zcbor_state_t* state, Value& value, std::uint64_t& seen,
                             Reason& reason)
    {
        if (key != FieldT::id) {
            return false;
        }
        const auto mask = UINT64_C(1) << Index;
        if ((seen & mask) != 0) {
            reason = Reason::DuplicateField;
            return true;
        }
        seen |= mask;
        auto& member = value.*FieldT::member;
        if constexpr (remote::detail::is_optional_v<std::remove_cvref_t<decltype(member)>>) {
            typename std::remove_cvref_t<decltype(member)>::value_type decoded{};
            if (!decode_value(state, decoded)) {
                reason = Reason::InvalidValue;
            } else {
                member = decoded;
            }
        } else if (!decode_value(state, member)) {
            reason = Reason::InvalidValue;
        }
        return true;
    }

    template <std::size_t... Indices>
    static bool dispatch(FieldId key, zcbor_state_t* state, Value& value, std::uint64_t& seen,
                         Reason& reason, std::index_sequence<Indices...>)
    {
        return (decode_field<Indices, FieldTypes>(key, state, value, seen, reason) || ...);
    }

    static Result<Value, Error> decode(std::span<const std::byte> input) noexcept
    {
        static_assert(sizeof...(FieldTypes) <= 64,
                      "SOLAR_DIAGNOSTIC_REMOTE_CBOR_FIELDS: initial decoder supports 64 fields");
        auto* begin = reinterpret_cast<const std::uint8_t*>(input.data());
        ZCBOR_STATE_D(state, 3, begin, input.size(), 1, 0);
        Value value{};
        std::uint64_t seen{};
        Reason reason{Reason::None};
        if (!zcbor_map_start_decode(state)) {
            return fail<Error>({Status::ProtocolError, Reason::Malformed, Operation::Decode});
        }
        while (state[0].elem_count > 0) {
            std::uint32_t key{};
            if (!zcbor_uint32_decode(state, &key)) {
                reason = Reason::Malformed;
                break;
            }
            const bool known = dispatch(static_cast<FieldId>(key), state, value, seen, reason,
                                        std::index_sequence_for<FieldTypes...>{});
            if (reason != Reason::None || (!known && !zcbor_any_skip(state, nullptr))) {
                if (reason == Reason::None) {
                    reason = Reason::Malformed;
                }
                break;
            }
        }
        if (reason == Reason::None && !zcbor_map_end_decode(state)) {
            reason = Reason::Malformed;
        }
        constexpr std::uint64_t required = [] {
            std::uint64_t mask{};
            std::size_t index{};
            ((mask |= FieldTypes::required ? (UINT64_C(1) << index) : 0U, ++index), ...);
            return mask;
        }();
        if (reason == Reason::None && (seen & required) != required) {
            reason = Reason::MissingField;
        }
        if (reason == Reason::None && !zcbor_payload_at_end(state)) {
            reason = Reason::TrailingData;
        }
        if (reason != Reason::None) {
            return fail<Error>({Status::ProtocolError, reason, Operation::Decode,
                                static_cast<std::uint32_t>(zcbor_peek_error(state))});
        }
        return value;
    }
};
} // namespace detail

template <typename Value>
[[nodiscard]] Result<std::size_t, Error> encode(const Value& value,
                                                std::span<std::byte> output) noexcept
{
    static_assert(validate_schema<Value>());
    static_assert(Schema<Value>::codec == Codec::Cbor,
                  "SOLAR_DIAGNOSTIC_REMOTE_CBOR_CODEC: Schema must select CBOR");
    if constexpr (std::is_same_v<Value, Status>) {
        auto* begin = reinterpret_cast<std::uint8_t*>(output.data());
        ZCBOR_STATE_E(state, 1, begin, output.size(), 1);
        if (!zcbor_uint32_put(state, static_cast<std::uint8_t>(encode_status(value)))) {
            return fail<Error>({Status::NoSpace, Reason::NoSpace, Operation::Encode});
        }
        return static_cast<std::size_t>(state[0].payload - begin);
    } else {
        return detail::Codec<Value, typename Schema<Value>::Fields>::encode(value, output);
    }
}

template <typename Value>
[[nodiscard]] Result<Value, Error> decode(std::span<const std::byte> input) noexcept
{
    static_assert(validate_schema<Value>());
    static_assert(Schema<Value>::codec == Codec::Cbor,
                  "SOLAR_DIAGNOSTIC_REMOTE_CBOR_CODEC: Schema must select CBOR");
    if constexpr (std::is_same_v<Value, Status>) {
        auto* begin = reinterpret_cast<const std::uint8_t*>(input.data());
        ZCBOR_STATE_D(state, 1, begin, input.size(), 1, 0);
        std::uint32_t encoded{};
        if (!zcbor_uint32_decode(state, &encoded) || !zcbor_payload_at_end(state)) {
            return fail<Error>({Status::ProtocolError, Reason::Malformed, Operation::Decode});
        }
        const auto status = decode_status(static_cast<StatusCode>(encoded));
        if (!status) {
            return fail<Error>({Status::ProtocolError, Reason::InvalidValue, Operation::Decode});
        }
        return *status;
    } else {
        return detail::Codec<Value, typename Schema<Value>::Fields>::decode(input);
    }
}
#else
template <typename Value>
[[nodiscard]] Result<std::size_t, Error> encode(const Value&, std::span<std::byte>) noexcept
{
    return fail<Error>({Status::NotSupported, Reason::Disabled, Operation::Encode});
}

template <typename Value>
[[nodiscard]] Result<Value, Error> decode(std::span<const std::byte>) noexcept
{
    return fail<Error>({Status::NotSupported, Reason::Disabled, Operation::Decode});
}
#endif

} // namespace solar::remote::cbor
