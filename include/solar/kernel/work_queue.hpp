#pragma once

#include <cstddef>

#include <zephyr/kernel.h>

#include "solar/core/status.hpp"
#include "solar/kernel/config.hpp"
#include "solar/kernel/priority.hpp"
#include "solar/kernel/thread.hpp"
#include "solar/kernel/time.hpp"

namespace solar::kernel
{

template <typename NameT, std::size_t StackBytes, Priority PriorityValue = Priority::Normal>
class WorkQueue
{
public:
    using Name = NameT;

    static_assert(StackBytes > 0, "Solar work queues require a non-zero stack");

    WorkQueue()
    {
        k_work_queue_init(&queue_);
    }

    Status start(bool no_yield = false)
    {
        if (started_)
        {
            return Status::Invalid;
        }

        k_work_queue_config config{};
        config.name = NameT::c_str();
        config.no_yield = no_yield;
        k_work_queue_start(&queue_, storage_.stack, storage_.size(), to_native_priority(PriorityValue), &config);
        started_ = true;
        return Status::Ok;
    }

    Status drain(bool plug = false)
    {
        return status_from_native(k_work_queue_drain(&queue_, plug));
    }

    Status unplug()
    {
        return status_from_native(k_work_queue_unplug(&queue_));
    }

    Status stop(Timeout timeout = Timeout::forever())
    {
        const Status status = status_from_native(k_work_queue_stop(&queue_, timeout.native()));
        if (status == Status::Ok)
        {
            started_ = false;
        }
        return status;
    }

    bool started() const
    {
        return started_;
    }

    k_work_q *native_handle()
    {
        return &queue_;
    }

    k_tid_t thread_id()
    {
        return k_work_queue_thread_get(&queue_);
    }

private:
    k_work_q queue_{};
    ThreadStorage<StackBytes> storage_{};
    bool started_ = false;
};

} // namespace solar::kernel
