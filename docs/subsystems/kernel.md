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

## Interrupt context

Only methods explicitly named for ISR use, or documented as no-wait ISR-safe,
may be called from interrupt context. They never block. Synchronous cancel,
join, flush, mutex locking, and operations that can reschedule a thread belong
in thread context.

## Native interoperation

Wrappers expose `native_handle()` or a focused native accessor. The returned
Zephyr object remains owned by the wrapper; do not reinitialize it, move it, or
retain it beyond the wrapper's lifetime. Direct Zephyr calls remain valid when
Solar has no useful typed addition.

See {doc}`../reference/api/kernel`, {doc}`../concepts/concurrency-and-context`,
and {doc}`../how-to/use-from-isr`.
