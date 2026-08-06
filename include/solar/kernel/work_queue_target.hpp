#pragma once

#include <zephyr/kernel.h>

namespace solar::kernel
{

class Work;
class DelayableWork;
class TriggeredWork;

/** A non-owning capability that permits work submission to one native queue. */
class WorkQueueTarget
{
  public:
    explicit constexpr WorkQueueTarget(k_work_q& queue) : queue_(&queue) {}

  private:
    [[nodiscard]] constexpr k_work_q* native_queue() const
    {
        return queue_;
    }

    friend class Work;
    friend class DelayableWork;
    friend class TriggeredWork;

    k_work_q* queue_;
};

inline constexpr WorkQueueTarget system_work_queue{k_sys_work_q};

} // namespace solar::kernel
