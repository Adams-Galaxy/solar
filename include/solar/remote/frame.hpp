#pragma once

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include "solar/remote/protocol.hpp"

#if defined(__ZEPHYR__)
#include <zephyr/data/cobs.h>
#include <zephyr/sys/crc.h>
#endif

namespace solar::remote::frame
{

[[nodiscard]] constexpr std::size_t max_encoded_size(std::size_t decoded_size) noexcept
{
    return decoded_size + decoded_size / 254U + 2U;
}

[[nodiscard]] constexpr std::uint32_t crc32c(std::span<const std::byte> input) noexcept
{
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const auto value : input) {
        crc ^= std::to_integer<std::uint8_t>(value);
        for (unsigned bit{}; bit < 8; ++bit) {
            crc = (crc >> 1U) ^ (0x82F63B78U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

[[nodiscard]] inline std::uint32_t integrity_crc32c(std::span<const std::byte> input) noexcept
{
#if defined(__ZEPHYR__)
    return crc32_c(0, reinterpret_cast<const std::uint8_t*>(input.data()), input.size(), true, true);
#else
    return crc32c(input);
#endif
}

[[nodiscard]] inline Result<std::size_t, Error> cobs_encode(std::span<const std::byte> input,
                                                            std::span<std::byte> output) noexcept
{
#if defined(__ZEPHYR__)
    struct Output
    {
        std::span<std::byte> bytes;
        std::size_t size{};
    } context{output};
    auto copy = +[](const std::uint8_t* data, std::size_t size, void* user) -> int {
        auto& destination = *static_cast<Output*>(user);
        if (destination.size + size > destination.bytes.size()) {
            return -ENOMEM;
        }
        if (data != nullptr && size != 0) {
            std::memcpy(destination.bytes.data() + destination.size, data, size);
            destination.size += size;
        }
        return 0;
    };
    cobs_encoder encoder{};
    if (cobs_encoder_init(&encoder, copy, &context, COBS_FLAG_TRAILING_DELIMITER) != 0 ||
        cobs_encoder_write(&encoder, reinterpret_cast<const std::uint8_t*>(input.data()),
                           input.size()) != static_cast<int>(input.size()) ||
        cobs_encoder_close(&encoder) != 0) {
        return fail(Error{Status::NoSpace, Reason::NoSpace, Operation::FrameEncode});
    }
    return context.size;
#else
    if (output.size() < max_encoded_size(input.size())) {
        return fail(Error{Status::NoSpace, Reason::NoSpace, Operation::FrameEncode});
    }
    std::size_t read{};
    std::size_t write{1};
    std::size_t code_index{};
    std::uint8_t code{1};

    while (read < input.size()) {
        if (input[read] == std::byte{}) {
            output[code_index] = static_cast<std::byte>(code);
            code = 1;
            code_index = write++;
            ++read;
        } else {
            output[write++] = input[read++];
            if (++code == 0xFFU) {
                output[code_index] = static_cast<std::byte>(code);
                code = 1;
                code_index = write++;
            }
        }
    }
    output[code_index] = static_cast<std::byte>(code);
    output[write++] = std::byte{};
    return write;
#endif
}

[[nodiscard]] inline Result<std::size_t, Error> cobs_decode(std::span<const std::byte> input,
                                                            std::span<std::byte> output) noexcept
{
#if defined(__ZEPHYR__)
    struct Output
    {
        std::span<std::byte> bytes;
        std::size_t size{};
    } context{output};
    auto copy = +[](const std::uint8_t* data, std::size_t size, void* user) -> int {
        auto& destination = *static_cast<Output*>(user);
        if (data == nullptr && size == 0) {
            return 0;
        }
        if (destination.size + size > destination.bytes.size()) {
            return -ENOMEM;
        }
        std::memcpy(destination.bytes.data() + destination.size, data, size);
        destination.size += size;
        return 0;
    };
    cobs_decoder decoder{};
    if (cobs_decoder_init(&decoder, copy, &context, COBS_FLAG_TRAILING_DELIMITER) != 0 ||
        cobs_decoder_write(&decoder, reinterpret_cast<const std::uint8_t*>(input.data()),
                           input.size()) != static_cast<int>(input.size()) ||
        cobs_decoder_close(&decoder) != 0) {
        return fail(Error{Status::ProtocolError, Reason::Malformed, Operation::FrameDecode});
    }
    return context.size;
#else
    if (!input.empty() && input.back() == std::byte{}) {
        input = input.first(input.size() - 1);
    }
    std::size_t read{};
    std::size_t write{};
    while (read < input.size()) {
        const auto code = std::to_integer<std::uint8_t>(input[read++]);
        if (code == 0 || read + static_cast<std::size_t>(code - 1U) > input.size()) {
            return fail(Error{Status::ProtocolError, Reason::Malformed, Operation::FrameDecode});
        }
        if (write + code - 1U > output.size()) {
            return fail(Error{Status::NoSpace, Reason::NoSpace, Operation::FrameDecode});
        }
        for (std::uint8_t index{1}; index < code; ++index) {
            output[write++] = input[read++];
        }
        if (code != 0xFFU && read < input.size()) {
            if (write == output.size()) {
                return fail(Error{Status::NoSpace, Reason::NoSpace, Operation::FrameDecode});
            }
            output[write++] = std::byte{};
        }
    }
    return write;
#endif
}

[[nodiscard]] inline Result<std::size_t, Error>
encode(const protocol::Envelope& authored, std::span<const std::byte> payload,
       std::span<std::byte> scratch, std::span<std::byte> output) noexcept
{
    if (payload.size() > UINT16_MAX || scratch.size() < protocol::envelope_size + payload.size() +
                                                         protocol::crc_size) {
        return fail(Error{Status::MessageTooLarge, Reason::Oversized, Operation::FrameEncode});
    }
#if defined(CONFIG_SOLAR_REMOTE_MAX_FRAME_BYTES)
    if (protocol::envelope_size + payload.size() + protocol::crc_size >
        CONFIG_SOLAR_REMOTE_MAX_FRAME_BYTES) {
        return fail(Error{Status::MessageTooLarge, Reason::Oversized, Operation::FrameEncode});
    }
#endif
    auto envelope = authored;
    envelope.payload_size = static_cast<std::uint16_t>(payload.size());
    auto encoded_envelope = protocol::encode(envelope);
    if (!encoded_envelope) {
        return fail(encoded_envelope.error());
    }
    std::memcpy(scratch.data(), encoded_envelope->data(), encoded_envelope->size());
    std::memcpy(scratch.data() + protocol::envelope_size, payload.data(), payload.size());
    const auto content_size = protocol::envelope_size + payload.size();
    const auto checksum = integrity_crc32c(scratch.first(content_size));
    protocol::detail::put_u32(scratch, content_size, checksum);
    return cobs_encode(scratch.first(content_size + protocol::crc_size), output);
}

struct Decoded
{
    protocol::Envelope envelope{};
    std::span<const std::byte> payload{};
};

[[nodiscard]] inline Result<Decoded, Error> decode(std::span<const std::byte> encoded,
                                                   std::span<std::byte> scratch) noexcept
{
    auto decoded_size = cobs_decode(encoded, scratch);
    if (!decoded_size) {
        return fail(decoded_size.error());
    }
    if (*decoded_size < protocol::envelope_size + protocol::crc_size) {
        return fail(Error{Status::ProtocolError, Reason::Malformed, Operation::FrameDecode});
    }
#if defined(CONFIG_SOLAR_REMOTE_MAX_FRAME_BYTES)
    if (*decoded_size > CONFIG_SOLAR_REMOTE_MAX_FRAME_BYTES) {
        return fail(Error{Status::MessageTooLarge, Reason::Oversized, Operation::FrameDecode});
    }
#endif
    const auto content_size = *decoded_size - protocol::crc_size;
    const auto expected = protocol::detail::get_u32(scratch.first(*decoded_size), content_size);
    if (integrity_crc32c(scratch.first(content_size)) != expected) {
        return fail(
            Error{Status::ProtocolError, Reason::IntegrityFailure, Operation::FrameDecode});
    }
    auto envelope = protocol::decode(scratch.first(protocol::envelope_size));
    if (!envelope) {
        return fail(envelope.error());
    }
    if (envelope->payload_size != content_size - protocol::envelope_size) {
        return fail(Error{Status::ProtocolError, Reason::Malformed, Operation::FrameDecode});
    }
    return Decoded{*envelope,
                   scratch.subspan(protocol::envelope_size, envelope->payload_size)};
}

struct FeedRecord
{
    std::size_t accepted{};
    std::size_t rejected{};
    std::size_t overflowed{};
};

template <std::size_t MaxEncodedBytes, std::size_t MaxDecodedBytes> class StreamDecoder
{
    static_assert(MaxEncodedBytes > 0 && MaxDecodedBytes >= protocol::envelope_size +
                                                              protocol::crc_size);
#if defined(CONFIG_SOLAR_REMOTE_MAX_FRAME_BYTES)
    static_assert(MaxDecodedBytes <= CONFIG_SOLAR_REMOTE_MAX_FRAME_BYTES,
                  "SOLAR_DIAGNOSTIC_REMOTE_FRAME_CEILING: decoder exceeds configured frame "
                  "ceiling");
#endif
  public:
    template <typename Handler>
    FeedRecord feed(std::span<const std::byte> input, Handler&& handler) noexcept
    {
        FeedRecord record{};
        for (const auto byte : input) {
            if (byte == std::byte{}) {
                if (dropping_) {
                    dropping_ = false;
                    encoded_size_ = 0;
                    ++record.overflowed;
                } else if (encoded_size_ != 0) {
                    auto decoded = frame::decode(std::span{encoded_}.first(encoded_size_), scratch_);
                    encoded_size_ = 0;
                    if (decoded) {
                        ++record.accepted;
                        handler(*decoded);
                    } else {
                        ++record.rejected;
                    }
                }
                continue;
            }
            if (dropping_) {
                continue;
            }
            if (encoded_size_ == encoded_.size()) {
                dropping_ = true;
                encoded_size_ = 0;
                continue;
            }
            encoded_[encoded_size_++] = byte;
        }
        totals_.accepted += record.accepted;
        totals_.rejected += record.rejected;
        totals_.overflowed += record.overflowed;
        return record;
    }

    [[nodiscard]] constexpr FeedRecord record() const noexcept { return totals_; }
    [[nodiscard]] constexpr std::size_t pending_bytes() const noexcept { return encoded_size_; }

    constexpr void reset() noexcept
    {
        encoded_size_ = 0;
        dropping_ = false;
        totals_ = {};
    }

  private:
    std::array<std::byte, MaxEncodedBytes> encoded_{};
    std::array<std::byte, MaxDecodedBytes> scratch_{};
    std::size_t encoded_size_{};
    bool dropping_{};
    FeedRecord totals_{};
};

} // namespace solar::remote::frame
