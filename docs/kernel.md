# Kernel

`solar::kernel` is the stable public API for threading and synchronization inside
Solar. It wraps Zephyr kernel primitives so services, tasks, timers, queues, and
future libraries can use Solar vocabulary without depending on raw Zephyr calls
at every call site.

Public wrappers include:

- `Thread`
- `ThreadStorage<N>` where `N` is stack bytes
- `StopSource`
- `StopToken`
- `Queue<T, N>`
- `Semaphore`
- `BinarySemaphore`
- `Mutex`
- `RecursiveMutex`
- `Event`
- `EventFlags`
- `Timer`
- `Deadline`
- `Timeout`
- `TimePoint`
- `ThisThread`
- `WorkItem`
- `DelayableWork`
- `WorkQueue`
- `PollSet`
- diagnostics helpers

## Priority

Solar exposes a semantic priority ladder:

```cpp
solar::kernel::Priority::Idle
solar::kernel::Priority::Low
solar::kernel::Priority::Normal
solar::kernel::Priority::High
solar::kernel::Priority::Realtime
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

## Stop Semantics

Threaded services are stopped cooperatively. Solar requests stop, the service
observes `StopToken`, and the owning thread is joined with a bounded timeout.
`Thread::abort()` exists as an explicit policy operation, but it is not the
normal lifecycle path.
