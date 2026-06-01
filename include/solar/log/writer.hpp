#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "solar/core.hpp"

namespace solar::log
{

/**
 * @brief Writer that forwards formatted log bytes to a typed serial transport.
 */
template <typename TransportT>
class SerialWriter
{
public:
    Status init()
    {
        return Status::Ok;
    }

    template <typename ContextT>
    Status init(ContextT &)
    {
        return init();
    }

    Status write(const char *data, std::size_t len)
    {
        const auto written = transport_.write(reinterpret_cast<const std::uint8_t *>(data), len);
        return written == len ? Status::Ok : Status::Error;
    }

    void flush()
    {
        transport_.flush();
    }

private:
    TransportT transport_{};
};

/**
 * @brief Fixed-size byte ring useful for tests and in-memory diagnostics.
 */
template <std::size_t Capacity>
class RingBufferSink
{
public:
    static_assert(Capacity > 0, "Solar log ring buffer requires a non-zero capacity");

    Status init()
    {
        clear();
        return Status::Ok;
    }

    template <typename ContextT>
    Status init(ContextT &)
    {
        return init();
    }

    Status write(const char *data, std::size_t len)
    {
        if (data == nullptr)
        {
            return Status::Invalid;
        }

        for (std::size_t i = 0; i < len; ++i)
        {
            buffer_[head_] = data[i];
            head_ = (head_ + 1U) % Capacity;
            if (size_ < Capacity)
            {
                ++size_;
            }
            else
            {
                tail_ = (tail_ + 1U) % Capacity;
                ++dropped_;
            }
        }
        return Status::Ok;
    }

    std::size_t read(char *out, std::size_t max_len) const
    {
        if (out == nullptr)
        {
            return 0;
        }

        const std::size_t count = size_ < max_len ? size_ : max_len;
        for (std::size_t i = 0; i < count; ++i)
        {
            out[i] = buffer_[(tail_ + i) % Capacity];
        }
        return count;
    }

    std::size_t size() const
    {
        return size_;
    }

    std::size_t dropped() const
    {
        return dropped_;
    }

    void clear()
    {
        head_ = 0;
        tail_ = 0;
        size_ = 0;
        dropped_ = 0;
    }

private:
    std::array<char, Capacity> buffer_{};
    std::size_t head_ = 0;
    std::size_t tail_ = 0;
    std::size_t size_ = 0;
    std::size_t dropped_ = 0;
};

} // namespace solar::log
