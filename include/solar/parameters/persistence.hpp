#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>

#include "solar/core/status.hpp"

namespace solar::parameters::persistence
{

enum class RecordKind : std::uint8_t
{
    Parameter = 1,
    Group = 2,
};

struct Key
{
    RecordKind kind{RecordKind::Parameter};
    std::uint64_t stable_id{};
};

template <typename Store>
concept Adapter = requires(Key key, std::span<std::byte> output, std::span<const std::byte> input) {
    { Store::initialize() } -> std::same_as<Result<void>>;
    { Store::load(key, output) } -> std::same_as<Result<std::size_t>>;
    { Store::save(key, input) } -> std::same_as<Result<void>>;
    { Store::erase(key) } -> std::same_as<Result<void>>;
};

namespace detail
{

inline constexpr std::uint32_t record_magic = 0x534f4c50U;
inline constexpr std::size_t envelope_size = 24;

template <std::unsigned_integral Value>
constexpr void write_little(std::span<std::byte> output, std::size_t offset, Value value) noexcept
{
    for (std::size_t index = 0; index < sizeof(Value); ++index) {
        output[offset + index] =
            static_cast<std::byte>((value >> (index * 8U)) & static_cast<Value>(0xffU));
    }
}

template <std::unsigned_integral Value>
[[nodiscard]] constexpr Value read_little(std::span<const std::byte> input,
                                          std::size_t offset) noexcept
{
    Value value{};
    for (std::size_t index = 0; index < sizeof(Value); ++index) {
        value |= static_cast<Value>(std::to_integer<unsigned int>(input[offset + index]))
                 << (index * 8U);
    }
    return value;
}

[[nodiscard]] constexpr std::uint32_t checksum(std::span<const std::byte> input) noexcept
{
    std::uint32_t hash = 2166136261U;
    for (const auto byte : input) {
        hash ^= std::to_integer<std::uint8_t>(byte);
        hash *= 16777619U;
    }
    return hash;
}

struct DecodedRecord
{
    Key key{};
    std::uint16_t version{};
    std::span<const std::byte> payload{};
};

[[nodiscard]] constexpr Result<std::size_t> encode_record(Key key, std::uint16_t version,
                                                          std::span<const std::byte> payload,
                                                          std::span<std::byte> output) noexcept
{
    const auto total = envelope_size + payload.size();
    if (output.size() < total) {
        return fail<solar::Error>({.status = solar::Status::NoBuffer});
    }
    write_little<std::uint32_t>(output, 0, record_magic);
    output[4] = static_cast<std::byte>(key.kind);
    output[5] = std::byte{};
    write_little<std::uint16_t>(output, 6, version);
    write_little<std::uint64_t>(output, 8, key.stable_id);
    write_little<std::uint32_t>(output, 16, static_cast<std::uint32_t>(payload.size()));
    write_little<std::uint32_t>(output, 20, checksum(payload));
    for (std::size_t index = 0; index < payload.size(); ++index) {
        output[envelope_size + index] = payload[index];
    }
    return total;
}

[[nodiscard]] constexpr Result<DecodedRecord>
decode_record(std::span<const std::byte> input) noexcept
{
    if (input.size() < envelope_size || read_little<std::uint32_t>(input, 0) != record_magic) {
        return fail<solar::Error>({.status = solar::Status::ProtocolError});
    }
    const auto payload_size = read_little<std::uint32_t>(input, 16);
    if (payload_size > input.size() - envelope_size ||
        envelope_size + payload_size != input.size()) {
        return fail<solar::Error>({.status = solar::Status::MessageTooLarge});
    }
    const auto payload = input.subspan(envelope_size, payload_size);
    if (checksum(payload) != read_little<std::uint32_t>(input, 20)) {
        return fail<solar::Error>({.status = solar::Status::ProtocolError});
    }
    return DecodedRecord{
        .key = {.kind = static_cast<RecordKind>(std::to_integer<std::uint8_t>(input[4])),
                .stable_id = read_little<std::uint64_t>(input, 8)},
        .version = read_little<std::uint16_t>(input, 6),
        .payload = payload,
    };
}

} // namespace detail

} // namespace solar::parameters::persistence
