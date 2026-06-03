#pragma once

#include <cstddef>

#include "solar/core.hpp"
#include "solar/kernel/kernel.hpp"

namespace solar
{

/**
 * @brief Static thread policy for a graph-owned task.
 *
 * Tasks are lower-level than services: they bind directly to a free function
 * entry point and do not receive a Solar context or stop token.
 */
template <typename NameT, std::size_t StackBytes, kernel::Priority PriorityValue = kernel::Priority::Normal>
struct TaskSpec
{
    using Name = NameT;

    static_assert(StackBytes > 0, "Solar task stacks require a non-zero byte count");

    static constexpr std::size_t stack_bytes = StackBytes;
    static constexpr auto priority = PriorityValue;
    static constexpr kernel::Milliseconds stop_timeout = kernel::Milliseconds{100};
};

/**
 * @brief Graph entry that owns a Kernel thread and static task storage.
 */
template <typename SpecT, kernel::Thread::Entry Entry, typename DependencyList = solar::Dependencies<>>
class Task
{
public:
    using Name = typename SpecT::Name;
    using Dependencies = DependencyList;

    static constexpr std::size_t stack_bytes = SpecT::stack_bytes;
    static constexpr kernel::Priority priority = SpecT::priority;

    template <typename ContextT>
    Status start(ContextT &)
    {
        return thread_.start(storage_);
    }

    template <typename ContextT>
    Status stop(ContextT &)
    {
        thread_.request_stop();
        return thread_.join(kernel::Timeout::after(SpecT::stop_timeout));
    }

    kernel::Thread &thread()
    {
        return thread_;
    }

    kernel::Thread const &thread() const
    {
        return thread_;
    }

private:
    kernel::Thread thread_{Name::c_str(), priority, static_cast<std::uint32_t>(stack_bytes), Entry};
    kernel::ThreadStorage<stack_bytes> storage_{};
};

} // namespace solar
