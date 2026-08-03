# Kernel

`solar::kernel` provides typed, allocation-free C++23 wrappers over Zephyr
kernel primitives. It does not require a bound Solar System, register a
component, or add lifecycle ownership.

## Choose a primitive

| Need | Primitive |
| --- | --- |
| Mutual exclusion | `Mutex`, `RecursiveMutex`, `Spinlock`, `CriticalSection` |
| Counted signalling | `Semaphore` |
| Bit-set signalling | `EventFlags` |
| Fixed typed messages | `MessageQueue<T, N>` |
| Byte streams | `Pipe<N>` |
| Fixed-block allocation | `MemorySlab<T, N>` |
| Dedicated execution | `Thread<StackBytes>` |
| Deferred callbacks | `Work`, `DelayableWork`, `TriggeredWork` |
| Timed notification | `Timer` |
| Multiple wait sources | `PollSet<N>` |

Prefer the narrowest primitive that expresses the ownership transfer. Use
{doc}`execution` when work must become part of the System graph, lifecycle, or
focused execution records.

## Time and waits

`kernel::Timeout` represents no-wait, finite durations, and forever.
`kernel::Deadline` converts one absolute budget into the remaining timeout for
several operations, preventing each wait from receiving the full budget.

Blocking operations return `Result<T>`. A no-wait miss is normally
`Status::WouldBlock`, `Empty`, or `NoSpace`; a finite wait expiry is
`Status::Timeout`. Check each primitive's typed error where it carries richer
native detail.

Zephyr 4.4 reacquires a condition-variable mutex only when the wait succeeds.
After a timeout or no-wait miss, Solar therefore marks the accompanying
`UniqueLock` as not owning the mutex; call `lock()` again before accessing the
protected state.

## Interrupt context

Only methods explicitly named for ISR use, or documented as no-wait ISR-safe,
may be called from interrupt context. They never block. Synchronous cancel,
join, flush, mutex locking, and operations that can reschedule a thread belong
in thread context.

## Native interoperation

Owning wrappers use stable native storage and do not expose mutable handles when
a native call could invalidate their lifecycle or callback state. Use
`owner.ref()` to borrow transparent primitives such as semaphores, recursive
mutexes, condition variables, events, message queues, pipes, memory slabs,
timers, spinlocks, poll signals, and initialized threads.
The matching `FooRef` can also borrow a Zephyr-owned object without taking over
its initialization or lifetime.

Custom workqueues expose `target()`, a narrow `WorkQueueTarget` capability that
allows submission without allowing queue reinitialization or lifecycle changes.
A reference or target must never outlive its native object. Direct Zephyr calls
remain the supported path for kernel facilities that Solar has not wrapped.

See {doc}`../reference/api/kernel`, {doc}`../concepts/concurrency-and-context`,
and {doc}`../how-to/use-from-isr`.
