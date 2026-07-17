#pragma once

#include <atomic>
#include <cstdint>

#include "solar/hardware/error.hpp"

namespace solar::hardware::async
{

struct Token
{
    std::uint32_t generation{};
    constexpr bool operator==(const Token&) const = default;
};

class Gate
{
  public:
    [[nodiscard]] Result<Token, Error> begin() noexcept
    {
        bool expected{};
        if (!active_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            return fail<Error>({.status = solar::Status::Busy,
                                .reason = Reason::Busy,
                                .operation = Operation::Submit,
                                .native = -EBUSY});
        }
        const auto generation = generation_.fetch_add(1, std::memory_order_acq_rel) + 1U;
        return Token{generation};
    }

    [[nodiscard]] bool active(Token token) const noexcept
    {
        return active_.load(std::memory_order_acquire) &&
               generation_.load(std::memory_order_acquire) == token.generation;
    }

    [[nodiscard]] Result<void, Error> complete(Token token) noexcept
    {
        return finish(token, Operation::Complete, Reason::StaleCompletion);
    }

    [[nodiscard]] Result<void, Error> cancel(Token token) noexcept
    {
        return finish(token, Operation::Cancel, Reason::Cancelled);
    }

  private:
    [[nodiscard]] Result<void, Error> finish(Token token, Operation operation,
                                             Reason stale_reason) noexcept
    {
        if (generation_.load(std::memory_order_acquire) != token.generation) {
            return fail<Error>({.status = solar::Status::Invalid,
                                .reason = stale_reason,
                                .operation = operation,
                                .native = -EINVAL});
        }
        bool expected{true};
        if (!active_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
            return fail<Error>({.status = solar::Status::Already,
                                .reason = stale_reason,
                                .operation = operation,
                                .native = -EALREADY});
        }
        return {};
    }

    std::atomic<std::uint32_t> generation_{};
    std::atomic<bool> active_{};
};

} // namespace solar::hardware::async
