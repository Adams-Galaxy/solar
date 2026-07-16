#pragma once

#include <zephyr/irq.h>

namespace solar::kernel
{

class InterruptLock
{
  public:
    InterruptLock() noexcept : key_(irq_lock()) {}

    ~InterruptLock()
    {
        irq_unlock(key_);
    }

    InterruptLock(const InterruptLock&) = delete;
    InterruptLock& operator=(const InterruptLock&) = delete;
    InterruptLock(InterruptLock&&) = delete;
    InterruptLock& operator=(InterruptLock&&) = delete;

  private:
    unsigned int key_{};
};

} // namespace solar::kernel
