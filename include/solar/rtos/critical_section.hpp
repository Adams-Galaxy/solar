#pragma once

#include <zephyr/irq.h>

namespace solar::rtos
{

class CriticalSection
{
public:
    CriticalSection() : key_(irq_lock()) {}
    ~CriticalSection() { irq_unlock(key_); }

    CriticalSection(const CriticalSection &) = delete;
    CriticalSection &operator=(const CriticalSection &) = delete;

private:
    unsigned int key_{};
};

} // namespace solar::rtos
