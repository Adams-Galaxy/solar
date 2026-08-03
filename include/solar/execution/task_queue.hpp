#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "solar/core/spin_mutex.hpp"
#include "solar/core/status.hpp"

namespace solar::execution
{

struct TaskToken
{
    std::uint32_t value{};
    constexpr bool operator==(const TaskToken&) const = default;
};

enum class ShutdownPolicy : std::uint8_t
{
    Drain,
    Cancel,
};

/** Bounded, explicitly pumped task queue independent of System lifecycle. */
template <std::size_t Capacity> class TaskQueue
{
    static_assert(Capacity > 0);

  public:
    using Function = void (*)(void*) noexcept;
    [[nodiscard]] Result<void> initialize() noexcept
    {
        SpinGuard lock{mutex_};
        slots_ = {};
        next_ = 1;
        accepting_ = true;
        return {};
    }
    [[nodiscard]] Result<TaskToken> enqueue(Function function, void* context = nullptr) noexcept
    {
        SpinGuard lock{mutex_};
        if (!accepting_) {
            return fail<solar::Error>({.status = Status::NotReady});
        }
        if (function == nullptr)
            return fail<solar::Error>({.status = Status::Invalid});
        for (auto& slot : slots_)
            if (!slot.active) {
                slot = {.function = function, .context = context, .token = next_++, .active = true};
                return TaskToken{slot.token};
            }
        return fail<solar::Error>({.status = Status::NoSpace});
    }
    [[nodiscard]] Result<void> cancel(TaskToken token) noexcept
    {
        SpinGuard lock{mutex_};
        for (auto& slot : slots_)
            if (slot.active && slot.token == token.value) {
                slot.active = false;
                return {};
            }
        return fail<solar::Error>({.status = Status::NotFound});
    }
    [[nodiscard]] Result<std::size_t> drain() noexcept
    {
        std::array<Slot, Capacity> ready{};
        {
            SpinGuard lock{mutex_};
            ready = slots_;
            slots_ = {};
        }
        std::size_t count{};
        for (auto& slot : ready)
            if (slot.active) {
                const auto function = slot.function;
                auto* context = slot.context;
                function(context);
                ++count;
            }
        return count;
    }
    [[nodiscard]] Result<std::size_t> stop(ShutdownPolicy policy = ShutdownPolicy::Drain) noexcept
    {
        {
            SpinGuard lock{mutex_};
            accepting_ = false;
            if (policy == ShutdownPolicy::Cancel) {
                std::size_t cancelled{};
                for (auto& slot : slots_) {
                    if (slot.active) {
                        ++cancelled;
                    }
                }
                slots_ = {};
                return cancelled;
            }
        }
        return drain();
    }

  private:
    struct Slot
    {
        Function function{};
        void* context{};
        std::uint32_t token{};
        bool active{};
    };
    std::array<Slot, Capacity> slots_{};
    std::uint32_t next_{1};
    SpinMutex mutex_{};
    bool accepting_{};
};

template <typename Application, std::size_t Capacity> struct StaticTaskQueue
{
    inline static TaskQueue<Capacity> storage{};
    [[nodiscard]] static Result<void> initialize() noexcept
    {
        return storage.initialize();
    }
    [[nodiscard]] static Result<TaskToken> enqueue(typename TaskQueue<Capacity>::Function function,
                                                   void* context = nullptr) noexcept
    {
        return storage.enqueue(function, context);
    }
    [[nodiscard]] static Result<void> cancel(TaskToken token) noexcept
    {
        return storage.cancel(token);
    }
    [[nodiscard]] static Result<std::size_t> drain() noexcept
    {
        return storage.drain();
    }
    [[nodiscard]] static Result<void> stop() noexcept
    {
        auto stopped = storage.stop();
        return stopped ? Result<void>{} : fail<solar::Error>(stopped.error());
    }
};

} // namespace solar::execution
