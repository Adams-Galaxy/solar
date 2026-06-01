#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <type_traits>

namespace solar::remote
{

/**
 * @brief Fixed-capacity UTF-8-ish string used by generated Remote payloads.
 */
template <std::size_t Capacity>
struct BoundedString
{
    std::array<char, Capacity + 1U> data{};
    std::uint16_t size = 0;

    constexpr bool assign(std::string_view text)
    {
        if (text.size() > Capacity)
        {
            return false;
        }
        size = static_cast<std::uint16_t>(text.size());
        for (std::uint16_t i = 0; i < size; ++i)
        {
            data[i] = text[i];
        }
        data[size] = '\0';
        return true;
    }

    constexpr const char *c_str() const
    {
        return data.data();
    }

    constexpr std::string_view view() const
    {
        return {data.data(), size};
    }
};

/**
 * @brief Fixed-capacity byte vector used by generated Remote payloads.
 */
template <std::size_t Capacity>
struct BoundedBytes
{
    std::array<std::uint8_t, Capacity> data{};
    std::uint16_t size = 0;
};

/**
 * @brief Fixed-capacity typed vector used by generated Remote payloads.
 */
template <typename T, std::size_t Capacity>
struct BoundedArray
{
    std::array<T, Capacity> data{};
    std::uint16_t size = 0;
};

/**
 * @brief Little-endian binary payload writer.
 */
class Writer
{
public:
    constexpr Writer(std::uint8_t *data, std::size_t capacity) : data_(data), capacity_(capacity) {}

    constexpr std::size_t size() const
    {
        return size_;
    }

    constexpr bool write_bytes(const std::uint8_t *bytes, std::size_t count)
    {
        if (bytes == nullptr && count != 0U)
        {
            return false;
        }
        if (size_ + count > capacity_)
        {
            return false;
        }
        for (std::size_t i = 0; i < count; ++i)
        {
            data_[size_ + i] = bytes[i];
        }
        size_ += count;
        return true;
    }

    template <typename T>
    constexpr bool write_scalar(T value)
    {
        static_assert(std::is_integral_v<T> || std::is_enum_v<T> || std::is_floating_point_v<T>);
        if constexpr (std::is_enum_v<T>)
        {
            return write_scalar(static_cast<std::underlying_type_t<T>>(value));
        }
        else if constexpr (std::is_same_v<T, bool>)
        {
            const std::uint8_t raw = value ? 1U : 0U;
            return write_bytes(&raw, 1U);
        }
        else if constexpr (std::is_floating_point_v<T>)
        {
            std::array<std::uint8_t, sizeof(T)> bytes{};
            std::memcpy(bytes.data(), &value, sizeof(T));
            return write_bytes(bytes.data(), bytes.size());
        }
        else
        {
            using Unsigned = std::make_unsigned_t<T>;
            Unsigned unsigned_value = static_cast<Unsigned>(value);
            std::array<std::uint8_t, sizeof(T)> bytes{};
            for (std::size_t i = 0; i < sizeof(T); ++i)
            {
                bytes[i] = static_cast<std::uint8_t>((unsigned_value >> (i * 8U)) & 0xFFU);
            }
            return write_bytes(bytes.data(), bytes.size());
        }
    }

    template <std::size_t Capacity>
    constexpr bool write_string(BoundedString<Capacity> const &text)
    {
        return write_scalar<std::uint16_t>(text.size) &&
               write_bytes(reinterpret_cast<const std::uint8_t *>(text.data.data()), text.size);
    }

    constexpr bool write_string(std::string_view text)
    {
        if (text.size() > 0xFFFFU)
        {
            return false;
        }
        return write_scalar<std::uint16_t>(static_cast<std::uint16_t>(text.size())) &&
               write_bytes(reinterpret_cast<const std::uint8_t *>(text.data()), text.size());
    }

private:
    std::uint8_t *data_ = nullptr;
    std::size_t capacity_ = 0;
    std::size_t size_ = 0;
};

/**
 * @brief Little-endian binary payload reader.
 */
class Reader
{
public:
    constexpr Reader(const std::uint8_t *data, std::size_t size) : data_(data), size_(size) {}

    constexpr std::size_t remaining() const
    {
        return size_ - offset_;
    }

    constexpr bool read_bytes(std::uint8_t *out, std::size_t count)
    {
        if (out == nullptr && count != 0U)
        {
            return false;
        }
        if (offset_ + count > size_)
        {
            return false;
        }
        for (std::size_t i = 0; i < count; ++i)
        {
            out[i] = data_[offset_ + i];
        }
        offset_ += count;
        return true;
    }

    template <typename T>
    constexpr bool read_scalar(T &value)
    {
        static_assert(std::is_integral_v<T> || std::is_enum_v<T> || std::is_floating_point_v<T>);
        if constexpr (std::is_enum_v<T>)
        {
            std::underlying_type_t<T> raw{};
            if (!read_scalar(raw))
            {
                return false;
            }
            value = static_cast<T>(raw);
            return true;
        }
        else if (offset_ + sizeof(T) > size_)
        {
            return false;
        }
        else if constexpr (std::is_floating_point_v<T>)
        {
            std::memcpy(&value, data_ + offset_, sizeof(T));
            offset_ += sizeof(T);
            return true;
        }
        else if constexpr (std::is_same_v<T, bool>)
        {
            value = data_[offset_] != 0U;
            offset_ += 1U;
            return true;
        }
        else
        {
            using Unsigned = std::make_unsigned_t<T>;
            Unsigned raw = 0;
            for (std::size_t i = 0; i < sizeof(T); ++i)
            {
                raw |= static_cast<Unsigned>(data_[offset_ + i]) << (i * 8U);
            }
            offset_ += sizeof(T);
            value = static_cast<T>(raw);
            return true;
        }
    }

    template <std::size_t Capacity>
    constexpr bool read_string(BoundedString<Capacity> &text)
    {
        std::uint16_t count = 0;
        if (!read_scalar(count) || count > Capacity || offset_ + count > size_)
        {
            return false;
        }
        text.size = count;
        for (std::uint16_t i = 0; i < count; ++i)
        {
            text.data[i] = static_cast<char>(data_[offset_ + i]);
        }
        text.data[count] = '\0';
        offset_ += count;
        return true;
    }

private:
    const std::uint8_t *data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t offset_ = 0;
};

template <typename T>
struct Codec
{
    static bool encode(Writer &writer, T const &value)
    {
        return T::encode(writer, value);
    }

    static bool decode(Reader &reader, T &value)
    {
        return T::decode(reader, value);
    }
};

template <typename T>
bool encode(T const &value, std::uint8_t *out, std::size_t capacity, std::size_t &size)
{
    Writer writer{out, capacity};
    if (!Codec<T>::encode(writer, value))
    {
        size = 0;
        return false;
    }
    size = writer.size();
    return true;
}

template <typename T>
bool decode(const std::uint8_t *data, std::size_t size, T &value)
{
    Reader reader{data, size};
    return Codec<T>::decode(reader, value);
}

} // namespace solar::remote
