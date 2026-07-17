#pragma once

#include <algorithm>
#include <span>
#include <string_view>

#include "solar/inspection/api.hpp"

namespace solar::inspection
{

class TextWriter
{
  public:
    explicit constexpr TextWriter(std::span<char> destination) noexcept : destination_(destination)
    {}

    constexpr void write(std::string_view text) noexcept
    {
        required_ += text.size();
        const auto count = (std::min)(text.size(), destination_.size() - written_);
        std::copy_n(text.begin(), count, destination_.begin() + written_);
        written_ += count;
    }

    [[nodiscard]] constexpr FormatResult result() const noexcept
    {
        return {.written = written_, .required = required_, .truncated = written_ != required_};
    }

  private:
    std::span<char> destination_;
    std::size_t written_{};
    std::size_t required_{};
};

template <> struct TextFormatter<DescriptorView>
{
    static void format(const DescriptorView& value, TextWriter& writer) noexcept
    {
        writer.write(value.descriptor.name);
    }
};

template <typename Record>
[[nodiscard]] Result<FormatResult, Error> format_text(const Record& record,
                                                      std::span<char> destination) noexcept
{
#if defined(CONFIG_SOLAR_INSPECTION_TEXT_FORMATTING)
    if constexpr (requires(TextWriter& writer) { TextFormatter<Record>::format(record, writer); }) {
        TextWriter writer{destination};
        TextFormatter<Record>::format(record, writer);
        return writer.result();
    } else {
        return fail<Error>({.status = solar::Status::NotSupported,
                            .reason = Reason::Unsupported,
                            .operation = Operation::FormatText});
    }
#else
    (void)record;
    (void)destination;
    return fail<Error>({.status = solar::Status::NotSupported,
                        .reason = Reason::Disabled,
                        .operation = Operation::FormatText});
#endif
}

} // namespace solar::inspection
