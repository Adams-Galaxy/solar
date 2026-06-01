#pragma once

#include <cstddef>
#include <cstring>

#include "solar/events/record.hpp"

namespace solar::events
{

namespace detail
{

class BufferWriter
{
public:
    BufferWriter(char *out, std::size_t capacity) : out_(out), capacity_(capacity) {}

    void append(char ch)
    {
        if (offset_ + 1U >= capacity_)
        {
            return;
        }
        out_[offset_++] = ch;
    }

    void append(const char *text)
    {
        if (text == nullptr)
        {
            return;
        }

        const std::size_t len = std::strlen(text);
        if (offset_ + len >= capacity_)
        {
            return;
        }

        std::memcpy(out_ + offset_, text, len);
        offset_ += len;
    }

    void append_u64(std::uint64_t value)
    {
        char digits[24]{};
        std::size_t index = sizeof(digits);
        digits[--index] = '\0';
        do
        {
            digits[--index] = static_cast<char>('0' + (value % 10U));
            value /= 10U;
        } while (value != 0 && index > 0);
        append(digits + index);
    }

    void append_i32(std::int32_t value)
    {
        if (value < 0)
        {
            append('-');
            append_u64(static_cast<std::uint64_t>(-(value + 1)) + 1U);
            return;
        }
        append_u64(static_cast<std::uint32_t>(value));
    }

    std::size_t finish()
    {
        if (capacity_ == 0)
        {
            return 0;
        }
        if (offset_ >= capacity_)
        {
            offset_ = capacity_ - 1U;
        }
        out_[offset_] = '\0';
        return offset_;
    }

private:
    char *out_ = nullptr;
    std::size_t capacity_ = 0;
    std::size_t offset_ = 0;
};

inline void append_json_string(BufferWriter &writer, const char *text)
{
    writer.append('"');
    if (text != nullptr)
    {
        for (const char *cursor = text; *cursor != '\0'; ++cursor)
        {
            switch (*cursor)
            {
            case '"':
                writer.append("\\\"");
                break;
            case '\\':
                writer.append("\\\\");
                break;
            case '\n':
                writer.append("\\n");
                break;
            case '\r':
                writer.append("\\r");
                break;
            case '\t':
                writer.append("\\t");
                break;
            default:
                writer.append(*cursor);
                break;
            }
        }
    }
    writer.append('"');
}

} // namespace detail

struct CompactFormat
{
    static std::size_t format(Record const &record, char *out, std::size_t capacity)
    {
        detail::BufferWriter writer{out, capacity};
        writer.append('[');
        writer.append(to_string(record.severity));
        writer.append("] ");
        writer.append(record.source != nullptr ? record.source : "?");
        writer.append('/');
        writer.append(record.name != nullptr ? record.name : "?");
        writer.append(" value=");
        writer.append_i32(record.value);
        writer.append(" detail=");
        writer.append_u64(record.detail);
        writer.append('\n');
        return writer.finish();
    }
};

struct JsonLinesFormat
{
    static std::size_t format(Record const &record, char *out, std::size_t capacity)
    {
        detail::BufferWriter writer{out, capacity};
        writer.append("{\"ts\":");
        writer.append_u64(record.timestamp_us);
        writer.append(",\"seq\":");
        writer.append_u64(record.sequence);
        writer.append(",\"id\":");
        writer.append_u64(record.id);
        writer.append(",\"severity\":");
        detail::append_json_string(writer, to_string(record.severity));
        writer.append(",\"source\":");
        detail::append_json_string(writer, record.source != nullptr ? record.source : "?");
        writer.append(",\"name\":");
        detail::append_json_string(writer, record.name != nullptr ? record.name : "?");
        writer.append(",\"value\":");
        writer.append_i32(record.value);
        writer.append(",\"detail\":");
        writer.append_u64(record.detail);
        writer.append("}\n");
        return writer.finish();
    }
};

} // namespace solar::events
