#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <concepts>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>

#include "solar/log/types.hpp"

namespace solar::log
{

namespace detail
{

#if defined(CONFIG_SOLAR_LOG)
inline constexpr std::size_t max_copied_string_bytes = CONFIG_SOLAR_LOG_MAX_STRING_BYTES;
inline constexpr std::size_t max_hexdump_bytes = CONFIG_SOLAR_LOG_MAX_HEXDUMP_BYTES;
#else
inline constexpr std::size_t max_copied_string_bytes = 1;
inline constexpr std::size_t max_hexdump_bytes = 1;
#endif

enum class ArgumentKind : std::uint8_t
{
    Signed,
    Unsigned,
    Floating,
    Boolean,
    Character,
    String,
};

struct NativePayloadHeader
{
    const char* format{};
    std::uint16_t format_size{};
    std::uint8_t argument_count{};
};

struct ArgumentHeader
{
    ArgumentKind kind{};
    std::uint16_t size{};
};

struct HexdumpPayloadHeader
{
    std::uint16_t label_size{};
    std::uint16_t data_size{};
};

template <typename T> struct is_character_array : std::false_type
{};

template <typename Character, std::size_t Size>
struct is_character_array<Character[Size]>
    : std::bool_constant<std::is_same_v<std::remove_cv_t<Character>, char>>
{};

template <typename T>
inline constexpr bool is_character_array_v = is_character_array<std::remove_reference_t<T>>::value;

template <typename T>
inline constexpr bool supported_argument_v =
    std::integral<std::remove_cvref_t<T>> || std::floating_point<std::remove_cvref_t<T>> ||
    std::is_enum_v<std::remove_cvref_t<T>> || is_character_array_v<T> ||
    std::is_same_v<std::remove_cvref_t<T>, std::string_view>;

template <typename... Arguments> [[nodiscard]] consteval bool valid_format(std::string_view format)
{
    std::size_t fields{};
    for (std::size_t index{}; index < format.size(); ++index) {
        if (format[index] == '{') {
            if (index + 1 < format.size() && format[index + 1] == '{') {
                ++index;
                continue;
            }
            const auto close = format.find('}', index + 1);
            if (close == std::string_view::npos) {
                return false;
            }
            const auto field = format.substr(index + 1, close - index - 1);
            if (!field.empty() && field.front() != ':') {
                return false;
            }
            ++fields;
            index = close;
        } else if (format[index] == '}') {
            if (index + 1 >= format.size() || format[index + 1] != '}') {
                return false;
            }
            ++index;
        }
    }
    return fields == sizeof...(Arguments) && (supported_argument_v<Arguments> && ...);
}

[[noreturn]] consteval void SOLAR_DIAGNOSTIC_LOG_FORMAT()
{
    __builtin_abort();
}

class PayloadWriter
{
  public:
    explicit PayloadWriter(std::span<std::byte> output) noexcept : output_(output) {}

    template <typename T> [[nodiscard]] bool append(const T& value) noexcept
    {
        return append_bytes(std::as_bytes(std::span{&value, std::size_t{1}}));
    }

    [[nodiscard]] bool append_bytes(std::span<const std::byte> bytes) noexcept
    {
        if (offset_ + bytes.size() > output_.size()) {
            return false;
        }
        std::memcpy(output_.data() + offset_, bytes.data(), bytes.size());
        offset_ += bytes.size();
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return offset_;
    }

  private:
    std::span<std::byte> output_{};
    std::size_t offset_{};
};

template <typename T> [[nodiscard]] bool encode_argument(PayloadWriter& writer, T&& argument)
{
    using Value = std::remove_cvref_t<T>;
    if constexpr (is_character_array_v<T>) {
        constexpr auto extent = std::extent_v<std::remove_reference_t<T>>;
        const auto size = static_cast<std::uint16_t>(extent == 0 ? 0 : extent - 1);
        return writer.append(ArgumentHeader{.kind = ArgumentKind::String, .size = size}) &&
               writer.append_bytes(
                   std::as_bytes(std::span<const char>{argument, static_cast<std::size_t>(size)}));
    } else if constexpr (std::is_same_v<Value, std::string_view>) {
        const auto copied = std::min<std::size_t>(argument.size(), max_copied_string_bytes);
        const auto size = static_cast<std::uint16_t>(copied);
        return writer.append(ArgumentHeader{.kind = ArgumentKind::String, .size = size}) &&
               writer.append_bytes(std::as_bytes(std::span{argument.data(), copied}));
    } else if constexpr (std::is_same_v<Value, bool>) {
        const std::uint8_t encoded = argument ? 1U : 0U;
        return writer.append(
                   ArgumentHeader{.kind = ArgumentKind::Boolean, .size = sizeof(encoded)}) &&
               writer.append(encoded);
    } else if constexpr (std::is_same_v<Value, char>) {
        return writer.append(
                   ArgumentHeader{.kind = ArgumentKind::Character, .size = sizeof(char)}) &&
               writer.append(argument);
    } else if constexpr (std::is_enum_v<Value>) {
        using Underlying = std::underlying_type_t<Value>;
        return encode_argument(writer, static_cast<Underlying>(argument));
    } else if constexpr (std::signed_integral<Value>) {
        const auto encoded = static_cast<std::int64_t>(argument);
        return writer.append(
                   ArgumentHeader{.kind = ArgumentKind::Signed, .size = sizeof(encoded)}) &&
               writer.append(encoded);
    } else if constexpr (std::unsigned_integral<Value>) {
        const auto encoded = static_cast<std::uint64_t>(argument);
        return writer.append(
                   ArgumentHeader{.kind = ArgumentKind::Unsigned, .size = sizeof(encoded)}) &&
               writer.append(encoded);
    } else if constexpr (std::floating_point<Value>) {
        const auto encoded = static_cast<double>(argument);
        return writer.append(
                   ArgumentHeader{.kind = ArgumentKind::Floating, .size = sizeof(encoded)}) &&
               writer.append(encoded);
    } else {
        return false;
    }
}

template <typename... Arguments>
[[nodiscard]] Result<void, Error> encode_native(CaptureRequest& request, const char* format,
                                                std::size_t format_size,
                                                Arguments&&... arguments) noexcept
{
    PayloadWriter writer{request.payload};
    const NativePayloadHeader header{
        .format = format,
        .format_size = static_cast<std::uint16_t>(format_size),
        .argument_count = static_cast<std::uint8_t>(sizeof...(Arguments)),
    };
    if (!writer.append(header) ||
        !(encode_argument(writer, std::forward<Arguments>(arguments)) && ...)) {
        return fail<Error>({.status = solar::Status::MessageTooLarge,
                            .reason = Reason::ArgumentEncoding,
                            .operation = Operation::Capture,
                            .level = request.level});
    }
    request.payload_size = static_cast<std::uint16_t>(writer.size());
    return {};
}

class TextWriter
{
  public:
    explicit TextWriter(std::span<char> output) noexcept : output_(output) {}

    void append(char value) noexcept
    {
        if (size_ < output_.size()) {
            output_[size_++] = value;
        } else {
            truncated_ = true;
        }
    }

    void append(std::string_view value) noexcept
    {
        const auto copied =
            std::min(value.size(), output_.size() - std::min(size_, output_.size()));
        if (copied != 0) {
            std::memcpy(output_.data() + size_, value.data(), copied);
            size_ += copied;
        }
        truncated_ = truncated_ || copied != value.size();
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return size_;
    }

    [[nodiscard]] bool truncated() const noexcept
    {
        return truncated_;
    }

  private:
    std::span<char> output_{};
    std::size_t size_{};
    bool truncated_{};
};

struct ArgumentView
{
    ArgumentHeader header{};
    std::span<const std::byte> bytes{};
};

[[nodiscard]] inline std::optional<ArgumentView> next_argument(std::span<const std::byte>& bytes)
{
    if (bytes.size() < sizeof(ArgumentHeader)) {
        return std::nullopt;
    }
    ArgumentHeader header{};
    std::memcpy(&header, bytes.data(), sizeof(header));
    bytes = bytes.subspan(sizeof(header));
    if (bytes.size() < header.size) {
        return std::nullopt;
    }
    auto value = bytes.first(header.size);
    bytes = bytes.subspan(header.size);
    return ArgumentView{.header = header, .bytes = value};
}

template <typename T> [[nodiscard]] T read_scalar(std::span<const std::byte> bytes) noexcept
{
    T value{};
    if (bytes.size() == sizeof(T)) {
        std::memcpy(&value, bytes.data(), sizeof(T));
    }
    return value;
}

inline void render_argument(TextWriter& output, const ArgumentView& argument,
                            std::string_view specification) noexcept
{
    std::array<char, 64> buffer{};
    const bool hexadecimal = specification.find('x') != std::string_view::npos ||
                             specification.find('X') != std::string_view::npos;
    const bool prefix = specification.find('#') != std::string_view::npos;
    switch (argument.header.kind) {
    case ArgumentKind::Signed: {
        const auto value = read_scalar<std::int64_t>(argument.bytes);
        if (hexadecimal) {
            if (prefix) {
                output.append("0x");
            }
            const auto converted = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                                                 static_cast<std::uint64_t>(value), 16);
            output.append({buffer.data(), static_cast<std::size_t>(converted.ptr - buffer.data())});
        } else {
            const auto converted =
                std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
            output.append({buffer.data(), static_cast<std::size_t>(converted.ptr - buffer.data())});
        }
        break;
    }
    case ArgumentKind::Unsigned: {
        const auto value = read_scalar<std::uint64_t>(argument.bytes);
        if (hexadecimal && prefix) {
            output.append("0x");
        }
        const auto converted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                                             hexadecimal ? 16 : 10);
        output.append({buffer.data(), static_cast<std::size_t>(converted.ptr - buffer.data())});
        break;
    }
    case ArgumentKind::Floating: {
        const auto value = read_scalar<double>(argument.bytes);
        int precision = 6;
        if (const auto dot = specification.find('.');
            dot != std::string_view::npos && dot + 1 < specification.size()) {
            precision = std::clamp<int>(specification[dot + 1] - '0', 0, 9);
        }
        const auto length = std::snprintf(buffer.data(), buffer.size(), "%.*f", precision, value);
        if (length > 0) {
            output.append({buffer.data(), std::min<std::size_t>(length, buffer.size() - 1)});
        }
        break;
    }
    case ArgumentKind::Boolean:
        output.append(read_scalar<std::uint8_t>(argument.bytes) != 0 ? "true" : "false");
        break;
    case ArgumentKind::Character:
        output.append(read_scalar<char>(argument.bytes));
        break;
    case ArgumentKind::String:
        output.append(
            {reinterpret_cast<const char*>(argument.bytes.data()), argument.bytes.size()});
        break;
    }
}

[[nodiscard]] inline Result<std::size_t, Error> render_native(RecordView record,
                                                              std::span<char> output) noexcept
{
    if (record.payload.size() < sizeof(NativePayloadHeader)) {
        return fail<Error>({.status = solar::Status::ProtocolError,
                            .reason = Reason::InternalInvariant,
                            .operation = Operation::Render,
                            .source = record.header.source,
                            .level = record.header.level});
    }
    NativePayloadHeader header{};
    std::memcpy(&header, record.payload.data(), sizeof(header));
    if (header.format == nullptr) {
        return fail<Error>({.status = solar::Status::ProtocolError,
                            .reason = Reason::InternalInvariant,
                            .operation = Operation::Render});
    }
    auto arguments = record.payload.subspan(sizeof(header));
    TextWriter writer{output};
    const std::string_view format{header.format, header.format_size};
    std::uint8_t rendered{};
    for (std::size_t index{}; index < format.size(); ++index) {
        if (format[index] == '{' && index + 1 < format.size() && format[index + 1] == '{') {
            writer.append('{');
            ++index;
        } else if (format[index] == '}' && index + 1 < format.size() && format[index + 1] == '}') {
            writer.append('}');
            ++index;
        } else if (format[index] == '{') {
            const auto close = format.find('}', index + 1);
            auto argument = next_argument(arguments);
            if (close == std::string_view::npos || !argument) {
                return fail<Error>({.status = solar::Status::ProtocolError,
                                    .reason = Reason::InternalInvariant,
                                    .operation = Operation::Render});
            }
            auto specification = format.substr(index + 1, close - index - 1);
            if (!specification.empty() && specification.front() == ':') {
                specification.remove_prefix(1);
            }
            render_argument(writer, *argument, specification);
            ++rendered;
            index = close;
        } else {
            writer.append(format[index]);
        }
    }
    if (rendered != header.argument_count || writer.truncated()) {
        return fail<Error>(
            {.status = writer.truncated() ? Status::MessageTooLarge : Status::ProtocolError,
             .reason = writer.truncated() ? Reason::RecordTooLarge : Reason::InternalInvariant,
             .operation = Operation::Render});
    }
    return writer.size();
}

} // namespace detail

template <typename... Arguments> class FormatString
{
  public:
    template <std::size_t Size>
    consteval FormatString(const char (&format)[Size]) : format_(format), size_(Size - 1)
    {
        if (!detail::valid_format<Arguments...>(std::string_view{format, Size - 1})) {
            detail::SOLAR_DIAGNOSTIC_LOG_FORMAT();
        }
    }

    [[nodiscard]] constexpr const char* data() const noexcept
    {
        return format_;
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept
    {
        return size_;
    }

  private:
    const char* format_{};
    std::size_t size_{};
};

} // namespace solar::log
