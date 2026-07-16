#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>
#include <variant>

#include "solar/inspection/collections.hpp"

namespace solar::inspection
{

class CborWriter
{
  public:
    explicit constexpr CborWriter(std::span<std::byte> destination) noexcept
        : destination_(destination)
    {}

    [[nodiscard]] constexpr bool unsigned_integer(std::uint64_t value) noexcept
    {
        return head(0, value);
    }

    [[nodiscard]] constexpr bool signed_integer(std::int64_t value) noexcept
    {
        return value >= 0 ? head(0, static_cast<std::uint64_t>(value))
                          : head(1, static_cast<std::uint64_t>(-1 - value));
    }

    [[nodiscard]] constexpr bool boolean(bool value) noexcept
    {
        return byte(static_cast<std::uint8_t>(value ? 0xF5U : 0xF4U));
    }

    [[nodiscard]] constexpr bool floating(double value) noexcept
    {
        if (!byte(0xFBU)) {
            return false;
        }
        const auto bits = std::bit_cast<std::uint64_t>(value);
        for (int shift = 56; shift >= 0; shift -= 8) {
            if (!byte(static_cast<std::uint8_t>(bits >> shift))) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] constexpr bool text(std::string_view value) noexcept
    {
        if (!head(3, value.size()) || remaining() < value.size()) {
            failed_ = true;
            return false;
        }
        for (const char character : value) {
            destination_[written_++] = static_cast<std::byte>(character);
        }
        return true;
    }

    [[nodiscard]] constexpr bool array(std::size_t size) noexcept
    {
        return head(4, size);
    }

    [[nodiscard]] constexpr bool map(std::size_t size) noexcept
    {
        return head(5, size);
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept
    {
        return written_;
    }

    [[nodiscard]] constexpr bool good() const noexcept
    {
        return !failed_;
    }

  private:
    [[nodiscard]] constexpr std::size_t remaining() const noexcept
    {
        return destination_.size() - written_;
    }

    [[nodiscard]] constexpr bool byte(std::uint8_t value) noexcept
    {
        if (remaining() == 0) {
            failed_ = true;
            return false;
        }
        destination_[written_++] = static_cast<std::byte>(value);
        return true;
    }

    [[nodiscard]] constexpr bool head(std::uint8_t major, std::uint64_t value) noexcept
    {
        if (value < 24) {
            return byte(static_cast<std::uint8_t>((major << 5U) | value));
        }
        std::size_t bytes{};
        std::uint8_t additional{};
        if (value <= UINT8_MAX) {
            bytes = 1;
            additional = 24;
        } else if (value <= UINT16_MAX) {
            bytes = 2;
            additional = 25;
        } else if (value <= UINT32_MAX) {
            bytes = 4;
            additional = 26;
        } else {
            bytes = 8;
            additional = 27;
        }
        if (!byte(static_cast<std::uint8_t>((major << 5U) | additional))) {
            return false;
        }
        for (std::size_t index{}; index < bytes; ++index) {
            const auto shift = (bytes - index - 1U) * 8U;
            if (!byte(static_cast<std::uint8_t>(value >> shift))) {
                return false;
            }
        }
        return true;
    }

    std::span<std::byte> destination_;
    std::size_t written_{};
    bool failed_{};
};

template <typename Record> struct CborEncoder
{};

template <> struct CborEncoder<component::DescriptorView>
{
    static bool encode(const component::DescriptorView& value, CborWriter& writer) noexcept
    {
        return writer.map(5) && writer.unsigned_integer(0) &&
               writer.unsigned_integer(value.local_id.value) && writer.unsigned_integer(1) &&
               writer.text(value.descriptor.name) && writer.unsigned_integer(2) &&
               writer.unsigned_integer(static_cast<std::uint8_t>(value.owner.kind)) &&
               writer.unsigned_integer(3) && writer.unsigned_integer(value.owner.component.value) &&
               writer.unsigned_integer(4) &&
               writer.unsigned_integer(static_cast<std::uint8_t>(value.origin));
    }
};

template <> struct CborEncoder<lifecycle::ComponentRecord>
{
    static bool encode(const lifecycle::ComponentRecord& value, CborWriter& writer) noexcept
    {
        return writer.map(7) && writer.unsigned_integer(0) &&
               writer.unsigned_integer(value.local_id.value) && writer.unsigned_integer(1) &&
               writer.unsigned_integer(static_cast<std::uint8_t>(value.state)) &&
               writer.unsigned_integer(2) &&
               writer.unsigned_integer(static_cast<std::uint8_t>(value.category)) &&
               writer.unsigned_integer(3) && writer.signed_integer(to_errno(value.last_status)) &&
               writer.unsigned_integer(4) &&
               writer.unsigned_integer(static_cast<std::uint8_t>(value.last_operation)) &&
               writer.unsigned_integer(5) && writer.unsigned_integer(value.transitions) &&
               writer.unsigned_integer(6) && writer.unsigned_integer(value.attempts);
    }
};

#if defined(CONFIG_SOLAR_EXECUTION)
template <> struct CborEncoder<execution::RegistrationRecord>
{
    static bool encode(const execution::RegistrationRecord& value, CborWriter& writer) noexcept
    {
        return writer.map(9) && writer.unsigned_integer(0) &&
               writer.unsigned_integer(value.local_id.value) && writer.unsigned_integer(1) &&
               writer.text(value.descriptor.descriptor.name) && writer.unsigned_integer(2) &&
               writer.unsigned_integer(static_cast<std::uint8_t>(value.kind)) &&
               writer.unsigned_integer(3) &&
               writer.unsigned_integer(static_cast<std::uint8_t>(value.availability)) &&
               writer.unsigned_integer(4) && writer.signed_integer(to_errno(value.last_status)) &&
               writer.unsigned_integer(5) && writer.unsigned_integer(value.submissions) &&
               writer.unsigned_integer(6) && writer.unsigned_integer(value.completed) &&
               writer.unsigned_integer(7) && writer.unsigned_integer(value.failed) &&
               writer.unsigned_integer(8) && writer.unsigned_integer(value.pending_count);
    }
};
#endif

#if defined(CONFIG_SOLAR_METRICS)
template <> struct CborEncoder<metrics::MetricViewRecord>
{
    static bool scalar(const metrics::ScalarValue& value, CborWriter& writer) noexcept
    {
        return std::visit(
            [&](const auto& current) {
                using T = std::remove_cvref_t<decltype(current)>;
                if constexpr (std::same_as<T, std::int64_t>) {
                    return writer.signed_integer(current);
                } else if constexpr (std::same_as<T, std::uint64_t>) {
                    return writer.unsigned_integer(current);
                } else if constexpr (std::same_as<T, double>) {
                    return writer.floating(current);
                } else {
                    return writer.boolean(current);
                }
            },
            value);
    }

    static bool encode(const metrics::MetricViewRecord& value, CborWriter& writer) noexcept
    {
        return writer.map(8) && writer.unsigned_integer(0) &&
               writer.unsigned_integer(value.metric.value) && writer.unsigned_integer(1) &&
               writer.unsigned_integer(static_cast<std::uint8_t>(value.view)) &&
               writer.unsigned_integer(2) && writer.unsigned_integer(value.view_index) &&
               writer.unsigned_integer(3) && scalar(value.value, writer) &&
               writer.unsigned_integer(4) && writer.unsigned_integer(value.revision) &&
               writer.unsigned_integer(5) && writer.signed_integer(value.updated_at) &&
               writer.unsigned_integer(6) && writer.boolean(value.initialized) &&
               writer.unsigned_integer(7) && writer.unsigned_integer(value.owner.value);
    }
};
#endif

#if defined(CONFIG_SOLAR_REMOTE)
template <> struct CborEncoder<remote::LinkRecord>
{
    static bool encode(const remote::LinkRecord& value, CborWriter& writer) noexcept
    {
        return writer.map(7) && writer.unsigned_integer(0) &&
               writer.unsigned_integer(value.id.value) && writer.unsigned_integer(1) &&
               writer.unsigned_integer(static_cast<std::uint8_t>(value.session)) &&
               writer.unsigned_integer(2) && writer.unsigned_integer(value.received_frames) &&
               writer.unsigned_integer(3) && writer.unsigned_integer(value.transmitted_frames) &&
               writer.unsigned_integer(4) && writer.unsigned_integer(value.protocol_errors) &&
               writer.unsigned_integer(5) && writer.unsigned_integer(value.subscriptions) &&
               writer.unsigned_integer(6) && writer.boolean(value.connected);
    }
};
#endif

template <typename Record>
concept CborEncodable = requires(const Record& value, CborWriter& writer) {
    { CborEncoder<Record>::encode(value, writer) } -> std::same_as<bool>;
};

template <typename Record>
[[nodiscard]] Result<std::size_t, Error> encode_cbor(const Record& record,
                                                     std::span<std::byte> destination) noexcept
{
#if defined(CONFIG_SOLAR_INSPECTION_CBOR)
    if constexpr (CborEncodable<Record>) {
        CborWriter writer{destination};
        if (!CborEncoder<Record>::encode(record, writer) || !writer.good()) {
            return fail(Error{.status = Status::NoSpace,
                              .reason = Reason::NoSpace,
                              .operation = Operation::EncodeCbor});
        }
        return writer.size();
    } else {
        return fail(Error{.status = Status::NotSupported,
                          .reason = Reason::Unsupported,
                          .operation = Operation::EncodeCbor});
    }
#else
    (void)record;
    (void)destination;
    return fail(Error{.status = Status::NotSupported,
                      .reason = Reason::Disabled,
                      .operation = Operation::EncodeCbor});
#endif
}

} // namespace solar::inspection
