#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <zephyr/kernel.h>

#include "solar/core/status.hpp"
#include "solar/kernel/deadline.hpp"
#include "solar/kernel/interrupt.hpp"

namespace solar::kernel
{

namespace detail
{

template <typename T, bool = std::is_enum_v<T>> struct UnsignedEnum : std::false_type
{};

template <typename T>
struct UnsignedEnum<T, true> : std::bool_constant<std::is_unsigned_v<std::underlying_type_t<T>> &&
                                                  sizeof(T) <= sizeof(stack_data_t)>
{};

} // namespace detail

template <typename T>
concept StackValue = (std::unsigned_integral<T> && sizeof(T) <= sizeof(stack_data_t)) ||
                     std::is_pointer_v<T> || detail::UnsignedEnum<T>::value;

namespace detail
{

template <StackValue T> [[nodiscard]] constexpr stack_data_t stack_pack(T value) noexcept
{
    if constexpr (std::is_pointer_v<T>) {
        return reinterpret_cast<stack_data_t>(value);
    } else {
        return static_cast<stack_data_t>(value);
    }
}

template <StackValue T> [[nodiscard]] constexpr T stack_unpack(stack_data_t value) noexcept
{
    if constexpr (std::is_pointer_v<T>) {
        return reinterpret_cast<T>(value);
    } else {
        return static_cast<T>(value);
    }
}

} // namespace detail

template <StackValue T> class StackRef
{
  public:
    explicit constexpr StackRef(k_stack& stack) noexcept : stack_(&stack) {}

    [[nodiscard]] Result<void> push(T value) const noexcept
    {
        const int result = k_stack_push(stack_, detail::stack_pack(value));
        if (result == 0) {
            return {};
        }
        return fail<Error>(
            {.status = result == -ENOMEM ? Status::NoSpace : status_from_errno(result),
             .native = result});
    }

    [[nodiscard]] Result<T> pop(Timeout timeout = Timeout::forever()) const noexcept
    {
        if (in_isr()) {
            return fail<Error>({.status = Status::Invalid});
        }
        return pop_native(timeout);
    }

    [[nodiscard]] Result<T> pop(const Deadline& deadline) const noexcept
    {
        return pop(deadline.remaining());
    }

    [[nodiscard]] Result<T> try_pop() const noexcept
    {
        return pop(Timeout::no_wait());
    }

    [[nodiscard]] Result<T> try_pop_isr() const noexcept
    {
        return pop_native(Timeout::no_wait());
    }

    [[nodiscard]] constexpr k_stack* native_handle() const noexcept
    {
        return stack_;
    }

  private:
    [[nodiscard]] Result<T> pop_native(Timeout timeout) const noexcept
    {
        stack_data_t value{};
        const int result = k_stack_pop(stack_, &value, timeout.native_handle());
        if (result == 0) {
            return detail::stack_unpack<T>(value);
        }
        if (result == -EBUSY) {
            return fail<Error>({.status = Status::WouldBlock, .native = result});
        }
        if (result == -EAGAIN) {
            return fail<Error>({.status = Status::Timeout, .native = result});
        }
        return fail<Error>(error_from_errno(result));
    }

    k_stack* stack_;
};

template <StackValue T, std::size_t Capacity> class Stack
{
    static_assert(Capacity > 0,
                  "SOLAR_DIAGNOSTIC_STACK_ZERO_CAPACITY: kernel stack capacity must be non-zero");

  public:
    Stack() noexcept
    {
        k_stack_init(&stack_, storage_.data(), static_cast<std::uint32_t>(storage_.size()));
    }

    Stack(const Stack&) = delete;
    Stack& operator=(const Stack&) = delete;
    Stack(Stack&&) = delete;
    Stack& operator=(Stack&&) = delete;

    [[nodiscard]] Result<void> push(T value) noexcept
    {
        return ref().push(value);
    }
    [[nodiscard]] Result<T> pop(Timeout timeout = Timeout::forever()) noexcept
    {
        return ref().pop(timeout);
    }
    [[nodiscard]] Result<T> pop(const Deadline& deadline) noexcept
    {
        return ref().pop(deadline);
    }
    [[nodiscard]] Result<T> try_pop() noexcept
    {
        return ref().try_pop();
    }
    [[nodiscard]] Result<T> try_pop_isr() noexcept
    {
        return ref().try_pop_isr();
    }
    [[nodiscard]] StackRef<T> ref() noexcept
    {
        return StackRef<T>{stack_};
    }

  private:
    k_stack stack_{};
    std::array<stack_data_t, Capacity> storage_{};
};

} // namespace solar::kernel
