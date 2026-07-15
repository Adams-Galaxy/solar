#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <zephyr/kernel.h>

#include "solar/core.hpp"
#include "solar/kernel/kernel.hpp"

namespace solar
{

struct TaskExecutionRecord
{
    std::uint32_t submitted = 0;
    std::uint32_t started = 0;
    std::uint32_t completed = 0;
    Status last_status = Status::NotReady;
    bool accepting = false;
    bool in_flight = false;
    bool dedicated_thread = false;
};

namespace task_detail
{

template <typename ReturnT>
Status normalize(ReturnT &&value)
{
    using Raw = std::remove_cvref_t<ReturnT>;
    if constexpr (std::is_same_v<Raw, Status>) return value;
    else if constexpr (std::is_same_v<Raw, Result<void>>) return value.status();
    else if constexpr (std::is_same_v<Raw, bool>) return value ? Status::Ok : Status::Error;
    else return Status::Ok;
}

template <typename BehaviorT>
Status execute()
{
    static_assert(requires { BehaviorT::execute(); },
                  "Solar task behavior must implement static execute()");
    if constexpr (std::is_void_v<decltype(BehaviorT::execute())>)
    {
        BehaviorT::execute();
        return Status::Ok;
    }
    else
    {
        return normalize(BehaviorT::execute());
    }
}

template <typename BehaviorT>
Status execute(kernel::StopToken stop)
{
    if constexpr (requires { BehaviorT::execute(stop); })
    {
        if constexpr (std::is_void_v<decltype(BehaviorT::execute(stop))>)
        {
            BehaviorT::execute(stop);
            return Status::Ok;
        }
        else
        {
            return normalize(BehaviorT::execute(stop));
        }
    }
    else
    {
        return execute<BehaviorT>();
    }
}

} // namespace task_detail

template <typename NameT,
          std::size_t StackBytes,
          kernel::Priority PriorityValue = kernel::Priority::Normal,
          std::uint32_t StopTimeoutMs = 100>
struct TaskThreadPolicy
{
    using Name = NameT;
    static_assert(StackBytes > 0, "Solar task stacks require a non-zero byte count");
    static constexpr std::size_t stack_bytes = StackBytes;
    static constexpr kernel::Priority priority = PriorityValue;
    static constexpr kernel::Milliseconds stop_timeout{StopTimeoutMs};
};

/** A statically owned work queue that may execute many SharedTask types. */
template <typename NameT,
          std::size_t StackBytes,
          kernel::Priority PriorityValue = kernel::Priority::Normal>
class SharedExecutor
{
public:
    using Name = NameT;

    static Status init() { return Status::Ok; }
    static Status start() { return queue_.start(); }
    static Status stop()
    {
        const Status drain = queue_.drain(true);
        if (drain != Status::Ok) return drain;
        return queue_.stop();
    }
    static k_work_q *native_handle() { return queue_.native_handle(); }
    static bool running() { return queue_.started(); }

private:
    static inline kernel::WorkQueue<NameT, StackBytes, PriorityValue> queue_{};
};

/** Event-triggered task using Zephyr's system work queue. */
template <typename NameT, typename BehaviorT,
          typename DependencyList = Dependencies<>>
class EventTask
{
public:
    using Name = NameT;
    using Behavior = BehaviorT;
    using Dependencies = DependencyList;

    static Status start() { accepting_.store(true); return Status::Ok; }
    static Status trigger()
    {
        if (!accepting_.load()) return Status::NotReady;
        ++record_.submitted;
        return work_.submit();
    }
    static Status stop()
    {
        accepting_.store(false);
        return work_.cancel_sync(cancel_sync_);
    }
    static TaskExecutionRecord execution()
    {
        TaskExecutionRecord copy = record_;
        copy.accepting = accepting_.load();
        copy.in_flight = in_flight_.load();
        return copy;
    }

private:
    static void execute(kernel::WorkItem<NameT> &, void *)
    {
        in_flight_.store(true);
        ++record_.started;
        record_.last_status = task_detail::execute<BehaviorT>();
        ++record_.completed;
        in_flight_.store(false);
    }

    static inline std::atomic<bool> accepting_{false};
    static inline std::atomic<bool> in_flight_{false};
    static inline TaskExecutionRecord record_{};
    static inline kernel::WorkItem<NameT> work_{&execute};
    static inline k_work_sync cancel_sync_{};
};

/** Event-triggered task submitted to an explicitly shared executor stack. */
template <typename NameT, typename BehaviorT, typename ExecutorT,
          typename DependencyList = Dependencies<ExecutorT>>
class SharedTask
{
public:
    using Name = NameT;
    using Behavior = BehaviorT;
    using Executor = ExecutorT;
    using Dependencies = DependencyList;

    static Status start() { accepting_.store(true); return Status::Ok; }
    static Status trigger()
    {
        if (!accepting_.load() || !ExecutorT::running()) return Status::NotReady;
        ++record_.submitted;
        return work_.submit_to(ExecutorT::native_handle());
    }
    static Status stop()
    {
        accepting_.store(false);
        return work_.cancel_sync(cancel_sync_);
    }
    static TaskExecutionRecord execution()
    {
        TaskExecutionRecord copy = record_;
        copy.accepting = accepting_.load();
        copy.in_flight = in_flight_.load();
        return copy;
    }

private:
    static void execute(kernel::WorkItem<NameT> &, void *)
    {
        in_flight_.store(true);
        ++record_.started;
        record_.last_status = task_detail::execute<BehaviorT>();
        ++record_.completed;
        in_flight_.store(false);
    }

    static inline std::atomic<bool> accepting_{false};
    static inline std::atomic<bool> in_flight_{false};
    static inline TaskExecutionRecord record_{};
    static inline kernel::WorkItem<NameT> work_{&execute};
    static inline k_work_sync cancel_sync_{};
};

/** Periodic task backed by one delayable work item and no private stack. */
template <typename NameT, typename BehaviorT, std::uint32_t PeriodMs,
          typename DependencyList = Dependencies<>>
class PeriodicTask
{
public:
    using Name = NameT;
    using Behavior = BehaviorT;
    using Dependencies = DependencyList;
    static_assert(PeriodMs > 0, "Solar periodic task period must be non-zero");

    static Status start()
    {
        accepting_.store(true);
        ++record_.submitted;
        return work_.schedule_after(kernel::Timeout::after(kernel::Milliseconds{PeriodMs}));
    }
    static Status stop()
    {
        accepting_.store(false);
        return work_.cancel_sync(cancel_sync_);
    }
    static TaskExecutionRecord execution()
    {
        TaskExecutionRecord copy = record_;
        copy.accepting = accepting_.load();
        copy.in_flight = in_flight_.load();
        return copy;
    }

private:
    static void execute(kernel::DelayableWork<NameT> &, void *)
    {
        if (!accepting_.load()) return;
        in_flight_.store(true);
        ++record_.started;
        record_.last_status = task_detail::execute<BehaviorT>();
        ++record_.completed;
        in_flight_.store(false);
        if (accepting_.load())
        {
            ++record_.submitted;
            (void)work_.reschedule_after(kernel::Timeout::after(kernel::Milliseconds{PeriodMs}));
        }
    }

    static inline std::atomic<bool> accepting_{false};
    static inline std::atomic<bool> in_flight_{false};
    static inline TaskExecutionRecord record_{};
    static inline kernel::DelayableWork<NameT> work_{&execute};
    static inline k_work_sync cancel_sync_{};
};

/** Explicit dedicated-thread task policy. */
template <typename NameT, typename BehaviorT, typename ThreadPolicyT,
          typename DependencyList = Dependencies<>>
class DedicatedTask
{
public:
    using Name = NameT;
    using Behavior = BehaviorT;
    using ThreadPolicy = ThreadPolicyT;
    using Dependencies = DependencyList;

    static Status start()
    {
        record_.accepting = true;
        record_.dedicated_thread = true;
        ++record_.submitted;
        return thread_.start(storage_);
    }
    static Status stop()
    {
        record_.accepting = false;
        thread_.request_stop();
        return thread_.join(kernel::Timeout::after(ThreadPolicyT::stop_timeout));
    }
    static TaskExecutionRecord execution()
    {
        TaskExecutionRecord copy = record_;
        copy.in_flight = thread_.running();
        return copy;
    }

private:
    static void execute(void *)
    {
        ++record_.started;
        record_.last_status = task_detail::execute<BehaviorT>(thread_.stop_token());
        ++record_.completed;
    }

    static inline TaskExecutionRecord record_{};
    static inline kernel::Thread thread_{NameT::c_str(), ThreadPolicyT::priority,
                                         static_cast<std::uint32_t>(ThreadPolicyT::stack_bytes),
                                         &execute};
    static inline kernel::ThreadStorage<ThreadPolicyT::stack_bytes> storage_{};
};

} // namespace solar
