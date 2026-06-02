# RTOS

`solar::rtos` is the stable public API for threading and synchronization inside
Solar. It wraps Zephyr kernel primitives so services, tasks, timers, queues, and
future libraries can use Solar vocabulary without depending on raw Zephyr calls
at every call site.

Public wrappers include:

- `Thread`
- `ThreadStorage<N>`
- `Queue<T, N>`
- `Semaphore`
- `BinarySemaphore`
- `Mutex`
- `RecursiveMutex`
- `EventFlags`
- `Timer`
- `Deadline`
- `ThisThread`
- notifications

## Priority

Solar exposes a semantic priority ladder:

```cpp
solar::rtos::Priority::Idle
solar::rtos::Priority::Low
solar::rtos::Priority::Normal
solar::rtos::Priority::High
solar::rtos::Priority::Realtime
```

Zephyr uses lower numeric values for higher preemptive priority. Solar maps its
ladder onto Zephyr's configured preemptive priority range.

## Deadlines

`Deadline` represents an absolute tick deadline with optional grace. It is useful
when one operation has a single time budget spanning multiple waits.

## Static Allocation

Solar services and tasks use static thread storage. This keeps the normal runtime
path aligned with Solar's static-after-boot policy while still relying on
Zephyr's scheduler and synchronization primitives.
