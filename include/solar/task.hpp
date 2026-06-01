#pragma once

#include <cstddef>

#include "solar/core.hpp"
#include "solar/rtos/rtos.hpp"

namespace solar
{

/**
 * @brief Static thread policy for a graph-owned task.
 *
 * Tasks are lower-level than services: they bind directly to a free function
 * entry point and do not receive a Solar context or stop token.
 */
template <typename NameT, std::size_t StackWords, rtos::Priority PriorityValue = rtos::Priority::Normal>
struct TaskSpec
{
    using Name = NameT;

    static_assert(StackWords > 0, "Solar task stacks require a non-zero word count");

    static constexpr std::size_t stack_words = StackWords;
    static constexpr auto priority = PriorityValue;
};

/**
 * @brief Graph entry that owns an RTOS thread and static task storage.
 */
template <typename SpecT, rtos::Thread::Entry Entry, typename DependencyList = solar::Dependencies<>>
class Task
{
public:
    using Name = typename SpecT::Name;
    using Dependencies = DependencyList;

    static constexpr std::size_t stack_words = SpecT::stack_words;
    static constexpr rtos::Priority priority = SpecT::priority;

    template <typename ContextT>
    Status start(ContextT &)
    {
        return thread_.start(storage_);
    }

    template <typename ContextT>
    Status stop(ContextT &)
    {
        thread_.request_stop();
        return thread_.terminate();
    }

    rtos::Thread &thread()
    {
        return thread_;
    }

    rtos::Thread const &thread() const
    {
        return thread_;
    }

private:
    rtos::Thread thread_{Name::c_str(), priority, static_cast<std::uint32_t>(stack_words), Entry};
    rtos::ThreadStorage<stack_words> storage_{};
};

} // namespace solar
