#pragma once

#include <cstddef>
#include <cstdint>

#include "solar/core.hpp"
#include "solar/rtos/rtos.hpp"

namespace solar
{

/**
 * @brief Thread policy for an active Solar service.
 *
 * Services are active runtime actors. Each service declares a
 * `using Thread = solar::ServiceSpec<...>` alias and implements
 * `run(ctx, stop_token)`. The run loop is owned by the service; Solar only
 * starts the thread and provides cooperative shutdown.
 *
 * @tparam NameT Stable service name.
 * @tparam StackWords Statically allocated stack size in RTOS words.
 * @tparam PriorityValue Portable Solar priority mapped by the low-level RTOS.
 */
template <typename NameT,
          std::size_t StackWords,
          rtos::Priority PriorityValue = rtos::Priority::Normal>
struct ServiceSpec
{
    using Name = NameT;

    static_assert(StackWords > 0, "Solar service threads require a non-zero stack");

    static constexpr std::size_t stack_words = StackWords;
    static constexpr rtos::Priority priority = PriorityValue;
};

/**
 * @brief Cooperative stop handle passed to threaded service run loops.
 *
 * Service loops should check this token regularly and return when stop is
 * requested. Solar deliberately avoids killing behavior as a normal lifecycle
 * mechanism; the low-level backend may still terminate as a last resort.
 */
class StopToken
{
public:
    constexpr StopToken() = default;
    explicit constexpr StopToken(rtos::Thread *thread) : thread_(thread) {}

    bool stop_requested() const
    {
        return thread_ != nullptr && thread_->stop_requested();
    }

private:
    rtos::Thread *thread_ = nullptr;
};

} // namespace solar
