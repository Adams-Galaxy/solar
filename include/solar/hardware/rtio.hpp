#pragma once

#include <cstdint>

#include <zephyr/rtio/rtio.h>

#include "solar/hardware/error.hpp"

namespace solar::hardware::rtio
{

class Completion
{
  public:
    Completion() = delete;
    Completion(const Completion&) = delete;
    Completion& operator=(const Completion&) = delete;

    Completion(Completion&& other) noexcept : context_(other.context_), value_(other.value_)
    {
        other.context_ = nullptr;
        other.value_ = nullptr;
    }

    Completion& operator=(Completion&& other) noexcept
    {
        if (this == &other) {
            return *this;
        }
        if (value_ != nullptr) {
            rtio_cqe_release(context_, value_);
        }
        context_ = other.context_;
        value_ = other.value_;
        other.context_ = nullptr;
        other.value_ = nullptr;
        return *this;
    }

    ~Completion()
    {
        if (value_ != nullptr) {
            rtio_cqe_release(context_, value_);
        }
    }

    [[nodiscard]] int result() const noexcept
    {
        return value_->result;
    }
    [[nodiscard]] void* user_data() const noexcept
    {
        return value_->userdata;
    }
    [[nodiscard]] ::rtio_cqe* native_handle() const noexcept
    {
        return value_;
    }

  private:
    Completion(::rtio* context, ::rtio_cqe* value) noexcept : context_(context), value_(value) {}

    ::rtio* context_{};
    ::rtio_cqe* value_{};

    friend class Context;
};

class Context
{
  public:
    explicit constexpr Context(::rtio& native) noexcept : native_(&native) {}

    [[nodiscard]] Result<::rtio_sqe*, Error> acquire() noexcept
    {
        if (auto* value = rtio_sqe_acquire(native_); value != nullptr) {
            return value;
        }
        return fail(Error{.status = Status::NoSpace,
                          .reason = Reason::ResourceExhausted,
                          .operation = Operation::Submit,
                          .native = -ENOMEM});
    }

    [[nodiscard]] Result<void, Error> submit(std::uint32_t wait_count = 0) noexcept
    {
        return hardware::detail::native_result(rtio_submit(native_, wait_count), Operation::Submit);
    }

    [[nodiscard]] Result<Completion, Error> consume(bool block = false) noexcept
    {
        auto* value = block ? rtio_cqe_consume_block(native_) : rtio_cqe_consume(native_);
        if (value == nullptr) {
            return fail(Error{.status = Status::WouldBlock,
                              .reason = Reason::Busy,
                              .operation = Operation::Complete,
                              .native = -EAGAIN});
        }
        return Completion{native_, value};
    }

    [[nodiscard]] Result<void, Error> cancel(::rtio_sqe& submission) noexcept
    {
        return hardware::detail::native_result(rtio_sqe_cancel(&submission),
                                               Operation::Cancel);
    }

    [[nodiscard]] constexpr ::rtio* native_handle() const noexcept
    {
        return native_;
    }

  private:
    ::rtio* native_{};
};

} // namespace solar::hardware::rtio
