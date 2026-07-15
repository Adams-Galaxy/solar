#pragma once

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "solar/core/status.hpp"
#include "solar/kernel/config.hpp"
#include "solar/kernel/time.hpp"

namespace solar::kernel
{

template <typename NameT>
class WorkItem
{
public:
    using Name = NameT;
    using Callback = void (*)(WorkItem &, void *);

    explicit WorkItem(Callback callback = nullptr, void *user = nullptr)
        : callback_(callback), user_(user)
    {
        k_work_init(&work_, &WorkItem::handler);
    }

    Status submit()
    {
        return status_from_native(k_work_submit(&work_));
    }

    Status submit_to(k_work_q *queue)
    {
        return status_from_native(k_work_submit_to_queue(queue, &work_));
    }

    Status cancel()
    {
        return status_from_native(k_work_cancel(&work_));
    }

    Status cancel_sync(k_work_sync &sync)
    {
        (void)k_work_cancel_sync(&work_, &sync);
        return Status::Ok;
    }

    bool pending() const
    {
        return k_work_is_pending(&work_);
    }

    k_work *native_handle()
    {
        return &work_;
    }

private:
    static void handler(k_work *work)
    {
        auto *self = CONTAINER_OF(work, WorkItem, work_);
        if (self->callback_ != nullptr)
        {
            self->callback_(*self, self->user_);
        }
    }

    k_work work_{};
    Callback callback_ = nullptr;
    void *user_ = nullptr;
};

template <typename NameT>
class DelayableWork
{
public:
    using Name = NameT;
    using Callback = void (*)(DelayableWork &, void *);

    explicit DelayableWork(Callback callback = nullptr, void *user = nullptr)
        : callback_(callback), user_(user)
    {
        k_work_init_delayable(&work_, &DelayableWork::handler);
    }

    Status schedule_after(Timeout delay)
    {
        return status_from_native(k_work_schedule(&work_, delay.native()));
    }

    Status schedule_after(Tick delay_ticks)
    {
        return schedule_after(Timeout::after_ticks(delay_ticks));
    }

    Status reschedule_after(Timeout delay)
    {
        return status_from_native(k_work_reschedule(&work_, delay.native()));
    }

    Status reschedule_after(Tick delay_ticks)
    {
        return reschedule_after(Timeout::after_ticks(delay_ticks));
    }

    Status schedule_to(k_work_q *queue, Timeout delay)
    {
        return status_from_native(k_work_schedule_for_queue(queue, &work_, delay.native()));
    }

    Status cancel()
    {
        return status_from_native(k_work_cancel_delayable(&work_));
    }

    Status cancel_sync(k_work_sync &sync)
    {
        (void)k_work_cancel_delayable_sync(&work_, &sync);
        return Status::Ok;
    }

    bool pending() const
    {
        return k_work_delayable_is_pending(&work_);
    }

    Tick remaining_ticks() const
    {
        return static_cast<Tick>(k_work_delayable_remaining_get(&work_));
    }

    k_work_delayable *native_handle()
    {
        return &work_;
    }

private:
    static void handler(k_work *work)
    {
        auto *delayable = k_work_delayable_from_work(work);
        auto *self = CONTAINER_OF(delayable, DelayableWork, work_);
        if (self->callback_ != nullptr)
        {
            self->callback_(*self, self->user_);
        }
    }

    k_work_delayable work_{};
    Callback callback_ = nullptr;
    void *user_ = nullptr;
};

} // namespace solar::kernel
