#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "solar/core/status.hpp"

namespace solar::parameters
{

/** Explicit adapter joining a parameter facade to a versioned byte store. */
template <typename Parameters, typename Storage, typename Codec, std::size_t MaximumBytes>
class PersistenceAdapter
{
  public:
    PersistenceAdapter(Storage& storage, std::uint16_t format_version = 1)
        : storage_(storage), format_version_(format_version)
    {}

    template <typename Parameter> [[nodiscard]] Result<void> save()
    {
        auto value = Parameters::template get<Parameter>();
        if (!value)
            return fail<solar::Error>(value.error());
        std::array<std::byte, MaximumBytes> payload{};
        auto encoded = Codec::template encode<Parameter>(*value, payload);
        if (!encoded)
            return fail<solar::Error>(encoded.error());
        constexpr std::size_t header_size = 12;
        if (*encoded + header_size > bytes_.size())
            return fail<solar::Error>({.status = Status::MessageTooLarge});
        put32(0, magic);
        put16(4, format_version_);
        put16(6, static_cast<std::uint16_t>(*encoded));
        std::copy_n(payload.begin(), *encoded, bytes_.begin() + header_size);
        put32(8, checksum(std::span{bytes_}.subspan(header_size, *encoded)));
        return storage_.write(Parameter::id, std::span{bytes_}.first(header_size + *encoded));
    }

    template <typename Parameter> [[nodiscard]] Result<void> load()
    {
        auto read = storage_.read(Parameter::id, bytes_);
        if (!read)
            return fail<solar::Error>(read.error());
        constexpr std::size_t header_size = 12;
        if (*read < header_size || get32(0) != magic || get16(4) != format_version_)
            return fail<solar::Error>({.status = Status::ProtocolError});
        const auto size = get16(6);
        if (header_size + size != *read ||
            checksum(std::span{bytes_}.subspan(header_size, size)) != get32(8))
            return fail<solar::Error>({.status = Status::ProtocolError});
        auto decoded = Codec::template decode<Parameter>(
            std::span<const std::byte>{bytes_}.subspan(header_size, size));
        if (!decoded)
            return fail<solar::Error>(decoded.error());
        auto updated = Parameters::template set<Parameter>(*decoded);
        return updated ? Result<void>{} : fail<solar::Error>(updated.error());
    }

  private:
    static constexpr std::uint32_t magic = 0x534F4C50U;
    [[nodiscard]] static std::uint32_t checksum(std::span<const std::byte> bytes)
    {
        std::uint32_t value = 2166136261U;
        for (auto byte : bytes) {
            value ^= std::to_integer<std::uint8_t>(byte);
            value *= 16777619U;
        }
        return value;
    }
    void put16(std::size_t at, std::uint16_t value)
    {
        bytes_[at] = std::byte(value);
        bytes_[at + 1] = std::byte(value >> 8);
    }
    void put32(std::size_t at, std::uint32_t value)
    {
        for (unsigned offset{}; offset < 4; ++offset)
            bytes_[at + offset] = std::byte(value >> (offset * 8));
    }
    [[nodiscard]] std::uint16_t get16(std::size_t at) const
    {
        return std::to_integer<std::uint16_t>(bytes_[at]) |
               (std::to_integer<std::uint16_t>(bytes_[at + 1]) << 8);
    }
    [[nodiscard]] std::uint32_t get32(std::size_t at) const
    {
        std::uint32_t value{};
        for (unsigned offset{}; offset < 4; ++offset)
            value |= std::to_integer<std::uint32_t>(bytes_[at + offset]) << (offset * 8);
        return value;
    }

    Storage& storage_;
    std::uint16_t format_version_;
    std::array<std::byte, MaximumBytes + 12> bytes_{};
};

} // namespace solar::parameters
