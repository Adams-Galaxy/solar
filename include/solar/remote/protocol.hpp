#pragma once

#include <cstddef>
#include <cstdint>

#include "solar/core/status.hpp"

namespace solar::remote
{

inline constexpr std::uint8_t ProtocolVersion = 1;
inline constexpr std::size_t HeaderBytes = 14;
inline constexpr std::size_t CrcBytes = 2;

/**
 * @brief Wire frame categories understood by Remote.
 */
enum class FrameKind : std::uint8_t
{
    Hello = 1,
    HelloAck = 2,
    Request = 3,
    Response = 4,
    Error = 5,
    Subscribe = 6,
    Unsubscribe = 7,
    TopicData = 8,
    Heartbeat = 9,
    ResetSession = 10,
};

/**
 * @brief Protocol-level error codes used in error frames.
 */
enum class ErrorCode : std::uint16_t
{
    None = 0,
    UnsupportedVersion = 1,
    MalformedFrame = 2,
    CrcFailure = 3,
    OversizedPayload = 4,
    UnknownTarget = 5,
    DecodeFailure = 6,
    HandlerFailure = 7,
    NotReady = 8,
    InternalError = 9,
};

/**
 * @brief Decoded Remote frame envelope.
 *
 * Payload memory is caller-owned. `encode_frame` COBS-frames this structure;
 * `decode_frame` fills it from a COBS-delimited input frame.
 */
struct Frame
{
    std::uint8_t version = ProtocolVersion;
    FrameKind kind = FrameKind::Request;
    std::uint8_t flags = 0;
    std::uint16_t sequence = 0;
    std::uint16_t correlation = 0;
    std::uint32_t target_id = 0;
    const std::uint8_t *payload = nullptr;
    std::uint16_t payload_size = 0;
};

/**
 * @brief CRC-16/CCITT used over raw Remote frame bytes before COBS encoding.
 */
constexpr std::uint16_t crc16_ccitt(const std::uint8_t *data, std::size_t size)
{
    std::uint16_t crc = 0xFFFFu;
    for (std::size_t i = 0; i < size; ++i)
    {
        crc ^= static_cast<std::uint16_t>(data[i]) << 8U;
        for (std::uint8_t bit = 0; bit < 8U; ++bit)
        {
            crc = (crc & 0x8000u) != 0U ? static_cast<std::uint16_t>((crc << 1U) ^ 0x1021u)
                                         : static_cast<std::uint16_t>(crc << 1U);
        }
    }
    return crc;
}

inline bool cobs_encode(const std::uint8_t *input,
                        std::size_t input_size,
                        std::uint8_t *output,
                        std::size_t output_capacity,
                        std::size_t &output_size)
{
    if (output_capacity == 0U)
    {
        return false;
    }

    std::size_t read_index = 0;
    std::size_t write_index = 1;
    std::size_t code_index = 0;
    std::uint8_t code = 1;

    while (read_index < input_size)
    {
        if (write_index >= output_capacity)
        {
            return false;
        }

        if (input[read_index] == 0U)
        {
            output[code_index] = code;
            code = 1;
            code_index = write_index++;
            ++read_index;
            continue;
        }

        output[write_index++] = input[read_index++];
        ++code;
        if (code == 0xFFU)
        {
            output[code_index] = code;
            code = 1;
            code_index = write_index++;
        }
    }

    if (code_index >= output_capacity)
    {
        return false;
    }
    output[code_index] = code;
    output_size = write_index;
    return true;
}

inline bool cobs_decode(const std::uint8_t *input,
                        std::size_t input_size,
                        std::uint8_t *output,
                        std::size_t output_capacity,
                        std::size_t &output_size)
{
    std::size_t read_index = 0;
    std::size_t write_index = 0;

    while (read_index < input_size)
    {
        const std::uint8_t code = input[read_index++];
        if (code == 0U)
        {
            return false;
        }
        for (std::uint8_t i = 1; i < code; ++i)
        {
            if (read_index >= input_size || write_index >= output_capacity)
            {
                return false;
            }
            output[write_index++] = input[read_index++];
        }
        if (code != 0xFFU && read_index < input_size)
        {
            if (write_index >= output_capacity)
            {
                return false;
            }
            output[write_index++] = 0U;
        }
    }

    output_size = write_index;
    return true;
}

inline bool encode_frame(Frame const &frame,
                         std::uint8_t *out,
                         std::size_t capacity,
                         std::size_t &encoded_size)
{
    const std::size_t raw_size = HeaderBytes + frame.payload_size + CrcBytes;
    if (capacity < raw_size + 2U || (frame.payload == nullptr && frame.payload_size != 0U))
    {
        return false;
    }

    std::uint8_t raw[HeaderBytes + 1024U + CrcBytes]{};
    if (raw_size > sizeof(raw))
    {
        return false;
    }

    raw[0] = frame.version;
    raw[1] = static_cast<std::uint8_t>(frame.kind);
    raw[2] = frame.flags;
    raw[3] = 0;
    raw[4] = static_cast<std::uint8_t>(frame.sequence & 0xFFU);
    raw[5] = static_cast<std::uint8_t>(frame.sequence >> 8U);
    raw[6] = static_cast<std::uint8_t>(frame.correlation & 0xFFU);
    raw[7] = static_cast<std::uint8_t>(frame.correlation >> 8U);
    raw[8] = static_cast<std::uint8_t>(frame.target_id & 0xFFU);
    raw[9] = static_cast<std::uint8_t>((frame.target_id >> 8U) & 0xFFU);
    raw[10] = static_cast<std::uint8_t>((frame.target_id >> 16U) & 0xFFU);
    raw[11] = static_cast<std::uint8_t>((frame.target_id >> 24U) & 0xFFU);
    raw[12] = static_cast<std::uint8_t>(frame.payload_size & 0xFFU);
    raw[13] = static_cast<std::uint8_t>(frame.payload_size >> 8U);

    for (std::uint16_t i = 0; i < frame.payload_size; ++i)
    {
        raw[HeaderBytes + i] = frame.payload[i];
    }

    const std::uint16_t crc = crc16_ccitt(raw, HeaderBytes + frame.payload_size);
    raw[HeaderBytes + frame.payload_size] = static_cast<std::uint8_t>(crc & 0xFFU);
    raw[HeaderBytes + frame.payload_size + 1U] = static_cast<std::uint8_t>(crc >> 8U);

    std::size_t cobs_size = 0;
    if (!cobs_encode(raw, raw_size, out, capacity - 1U, cobs_size))
    {
        return false;
    }
    out[cobs_size] = 0;
    encoded_size = cobs_size + 1U;
    return true;
}

inline bool decode_frame(const std::uint8_t *encoded,
                         std::size_t encoded_size,
                         std::uint8_t *payload_out,
                         std::size_t payload_capacity,
                         Frame &frame)
{
    if (encoded_size == 0U)
    {
        return false;
    }
    if (encoded[encoded_size - 1U] == 0U)
    {
        --encoded_size;
    }

    std::uint8_t raw[HeaderBytes + 1024U + CrcBytes]{};
    std::size_t raw_size = 0;
    if (!cobs_decode(encoded, encoded_size, raw, sizeof(raw), raw_size) || raw_size < HeaderBytes + CrcBytes)
    {
        return false;
    }

    const std::uint16_t expected_crc = static_cast<std::uint16_t>(raw[raw_size - 2U]) |
                                       (static_cast<std::uint16_t>(raw[raw_size - 1U]) << 8U);
    if (expected_crc != crc16_ccitt(raw, raw_size - CrcBytes))
    {
        return false;
    }

    const std::uint16_t payload_size = static_cast<std::uint16_t>(raw[12]) |
                                       (static_cast<std::uint16_t>(raw[13]) << 8U);
    if (raw_size != HeaderBytes + payload_size + CrcBytes || payload_size > payload_capacity)
    {
        return false;
    }

    frame.version = raw[0];
    frame.kind = static_cast<FrameKind>(raw[1]);
    frame.flags = raw[2];
    frame.sequence = static_cast<std::uint16_t>(raw[4]) | (static_cast<std::uint16_t>(raw[5]) << 8U);
    frame.correlation = static_cast<std::uint16_t>(raw[6]) | (static_cast<std::uint16_t>(raw[7]) << 8U);
    frame.target_id = static_cast<std::uint32_t>(raw[8]) |
                      (static_cast<std::uint32_t>(raw[9]) << 8U) |
                      (static_cast<std::uint32_t>(raw[10]) << 16U) |
                      (static_cast<std::uint32_t>(raw[11]) << 24U);
    frame.payload_size = payload_size;
    frame.payload = payload_out;
    for (std::uint16_t i = 0; i < payload_size; ++i)
    {
        payload_out[i] = raw[HeaderBytes + i];
    }
    return frame.version == ProtocolVersion;
}

} // namespace solar::remote
