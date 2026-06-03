#pragma once

#include <zephyr/irq.h>

namespace solar::kernel
{

class InterruptLock
{
public:
    InterruptLock() : key_(irq_lock()) {}
    ~InterruptLock() { irq_unlock(key_); }

    InterruptLock(const InterruptLock &) = delete;
    InterruptLock &operator=(const InterruptLock &) = delete;

private:
    unsigned int key_{};
};

using CriticalSection = InterruptLock;

} // namespace solar::kernel
