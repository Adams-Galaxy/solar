#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <zephyr/kernel.h>

#include "solar/core/status.hpp"
#include "solar/kernel/deadline.hpp"
#include "solar/kernel/interrupt.hpp"

namespace solar::kernel
{

/** Non-owning byte-stream access to an initialized native Zephyr pipe. */
class PipeRef
{
  public:
    explicit constexpr PipeRef(k_pipe& pipe) noexcept : pipe_(&pipe) {}

    [[nodiscard]] Result<std::size_t> write(std::span<const std::byte> data,
                                            Timeout timeout = Timeout::forever()) const noexcept
    {
        if (in_isr()) {
            return fail<solar::Error>({.status = solar::Status::Invalid});
        }
        const int result = k_pipe_write(pipe_, reinterpret_cast<const std::uint8_t*>(data.data()),
                                        data.size(), timeout.native_handle());
        return transfer_result(result, timeout);
    }

    [[nodiscard]] Result<std::size_t> write(std::span<const std::byte> data,
                                            const Deadline& deadline) const noexcept
    {
        return write(data, deadline.remaining());
    }

    [[nodiscard]] Result<std::size_t> try_write(std::span<const std::byte> data) const noexcept
    {
        return write(data, Timeout::no_wait());
    }

    [[nodiscard]] Result<std::size_t> read(std::span<std::byte> destination,
                                           Timeout timeout = Timeout::forever()) const noexcept
    {
        if (in_isr()) {
            return fail<solar::Error>({.status = solar::Status::Invalid});
        }
        const int result = k_pipe_read(pipe_, reinterpret_cast<std::uint8_t*>(destination.data()),
                                       destination.size(), timeout.native_handle());
        return transfer_result(result, timeout);
    }

    [[nodiscard]] Result<std::size_t> read(std::span<std::byte> destination,
                                           const Deadline& deadline) const noexcept
    {
        return read(destination, deadline.remaining());
    }

    [[nodiscard]] Result<std::size_t> try_read(std::span<std::byte> destination) const noexcept
    {
        return read(destination, Timeout::no_wait());
    }

    void reset() const noexcept
    {
        k_pipe_reset(pipe_);
    }
    void close() const noexcept
    {
        k_pipe_close(pipe_);
    }

    [[nodiscard]] constexpr k_pipe* native_pipe() const noexcept
    {
        return pipe_;
    }

  private:
    [[nodiscard]] static Result<std::size_t> transfer_result(int result, Timeout timeout) noexcept
    {
        if (result >= 0) {
            return static_cast<std::size_t>(result);
        }
        if (result == -EAGAIN) {
            return fail<Error>(
                {.status = timeout.is_no_wait() ? Status::WouldBlock : Status::Timeout,
                 .native = result});
        }
        if (result == -ECANCELED) {
            return fail<solar::Error>({.status = solar::Status::Cancelled, .native = result});
        }
        return fail<Error>(error_from_errno(result));
    }

    k_pipe* pipe_;
};

template <std::size_t Capacity> class Pipe
{
    static_assert(Capacity > 0,
                  "SOLAR_DIAGNOSTIC_PIPE_ZERO_CAPACITY: pipe capacity must be non-zero");

  public:
    static constexpr std::size_t capacity = Capacity;

    Pipe() noexcept
    {
        k_pipe_init(&pipe_, storage_.data(), storage_.size());
    }

    Pipe(const Pipe&) = delete;
    Pipe& operator=(const Pipe&) = delete;
    Pipe(Pipe&&) = delete;
    Pipe& operator=(Pipe&&) = delete;

    [[nodiscard]] Result<std::size_t> write(std::span<const std::byte> data,
                                            Timeout timeout = Timeout::forever()) noexcept
    {
        return ref().write(data, timeout);
    }

    [[nodiscard]] Result<std::size_t> write(std::span<const std::byte> data,
                                            const Deadline& deadline) noexcept
    {
        return write(data, deadline.remaining());
    }

    [[nodiscard]] Result<std::size_t> try_write(std::span<const std::byte> data) noexcept
    {
        return write(data, Timeout::no_wait());
    }

    [[nodiscard]] Result<std::size_t> read(std::span<std::byte> destination,
                                           Timeout timeout = Timeout::forever()) noexcept
    {
        return ref().read(destination, timeout);
    }

    [[nodiscard]] Result<std::size_t> read(std::span<std::byte> destination,
                                           const Deadline& deadline) noexcept
    {
        return read(destination, deadline.remaining());
    }

    [[nodiscard]] Result<std::size_t> try_read(std::span<std::byte> destination) noexcept
    {
        return read(destination, Timeout::no_wait());
    }

    void reset() noexcept
    {
        ref().reset();
    }

    void close() noexcept
    {
        ref().close();
    }

    [[nodiscard]] PipeRef ref() noexcept
    {
        return PipeRef{pipe_};
    }

  private:
    std::array<std::uint8_t, Capacity> storage_{};
    k_pipe pipe_{};
};

} // namespace solar::kernel
