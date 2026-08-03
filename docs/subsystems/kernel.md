# Kernel

`solar::kernel` provides typed, allocation-free C++23 wrappers over Zephyr
kernel primitives. It does not require a bound Solar System, register a
component, or add lifecycle ownership.

## Choose a primitive

| Need | Primitive |
| --- | --- |
| Mutual exclusion | `Mutex`, `RecursiveMutex`, `SpinLock`, `InterruptLock` |
| Counted signalling | `Semaphore` |
| Bit-set signalling | `EventFlags` |
| Fixed typed messages | `MessageQueue<T, N>` |
| Byte streams | `Pipe<N>` |
| Fixed-block allocation | `MemorySlab<BlockBytes, Count>` |
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
Positive durations round up to the next tick, and conversions that exceed the
native tick range saturate instead of overflowing. Clock-dependent APIs are
only present when Zephyr provides `CONFIG_SYS_CLOCK_EXISTS`.

Blocking operations return `Result<T>`. A no-wait miss is normally
`Status::WouldBlock`, `Empty`, or `NoSpace`; a finite wait expiry is
`Status::Timeout`. Check each primitive's typed error where it carries richer
native detail.

`Semaphore` is the ordinary binary-capacity primitive. Use
`CountingSemaphore<Limit, InitialCount>` when more than one permit is needed;
both values are checked against Zephyr at compile time, so invalid dynamic
constructor arguments cannot disappear with release-build assertions.

`PollSignal::raise()` returns `PollSignalOutcome`. Zephyr can latch the signal
and its value while returning `-EAGAIN` because a waiting poll timeout is
already expiring; Solar reports that as the successful
`LatchedAfterTimeout` outcome and preserves `-EAGAIN` instead of pretending the
signal was lost.

Some native return values are intentionally not given more precision than
Zephyr provides. A blocking semaphore take reports native `-EAGAIN` for either
timeout expiry or a concurrent reset, and a message-queue `-ENOMSG` can mean an
immediate capacity miss or a concurrent purge. Solar preserves the native code
and reports the operation-level outcome; it does not invent a cause that the
kernel did not supply. Event waits return a zero bitmask for an unsatisfied
wait, so there is no native errno to retain.

Zephyr 4.4 reacquires a condition-variable mutex only when the wait succeeds.
After a timeout or no-wait miss, Solar therefore marks the accompanying
`UniqueLock` as not owning the mutex; call `lock()` again before accessing the
protected state.

`EventFlags::set_masked()` performs Zephyr's atomic masked replacement.
`MessageQueue::attributes()` reports the configured message size, capacity,
used count, and free count. `MemorySlab::statistics()` reports current and
maximum allocation when the corresponding Zephyr tracing option is enabled;
maximum usage can then be reset explicitly. An allocated `MemorySlabBlock`
must be released before the slab it refers to is destroyed.

Starting a `Timer` with `Timeout::forever()` preserves Zephyr's documented
no-op behavior. It is a successful call that does not schedule an expiry.

## Interrupt context

Methods that Zephyr permits unconditionally from ISR keep their ordinary name,
such as `Semaphore::give()`, `EventFlags::post()`, `Timer::stop()`, and
`Work::submit()`. Wait-capable methods are always thread-only, including their
`try_` forms. Where Zephyr permits the same operation from ISR only with
`K_NO_WAIT`, Solar exposes a structurally non-blocking `try_*_isr()` method.
Synchronous cancel, join, poll, condition waits, pipe transfers, mutex locking,
and timer start belong in thread context and return `Status::Invalid` from ISR
before calling Zephyr.

The `this_thread` operations that can inspect or reschedule the current thread
return `Result`, including priority access, priority changes, sleep, and yield.
This prevents interrupt context from being mistaken for an ordinary current
thread. `busy_wait_for()` remains directly ISR-safe, matching Zephyr.

Owned threads expose `ThreadLifecycleState`, which records only wrapper facts:
empty storage, prepared, start issued, normal/native-observed finish, or an
owner-issued abort. Suspend and resume deliberately do not manufacture a
scheduler state. Use `thread_diagnostics()` for a best-effort native
observation. `ThreadConfiguration` accepts `ThreadOptions`, not raw native
bits; this milestone supports supervisor threads and deliberately excludes
user-mode, inherited-permission, and essential-thread creation.

`ThreadRef` is the common surface for owned, current, and native Zephyr
threads. It provides priority changes, wakeup, suspend/resume, external abort,
join, and wake timing. `this_thread::suspend()` and `this_thread::abort()` cover
the corresponding self operations. Scheduler deadline APIs use
`CycleDuration` and `CycleTimePoint`, retaining Zephyr's hardware-cycle units
and signed half-range constraint. Global time slices use milliseconds and an
exact `Priority`; per-thread slices use `TickDuration` when
`CONFIG_TIMESLICE_PER_THREAD` is enabled.

## Native interoperation

Owning wrappers use stable native storage and do not expose mutable handles when
a native call could invalidate their lifecycle or callback state. Use
`owner.ref()` to borrow transparent primitives such as semaphores, recursive
mutexes, condition variables, events, message queues, pipes, memory slabs,
timers, spinlocks, poll signals, and initialized threads.
The matching `FooRef` can also borrow a Zephyr-owned object without taking over
its initialization or lifetime. Typed native queues use
`MessageQueueRef<T>::borrow(queue)`, which validates Zephyr's configured item
size and returns `Status::Invalid` instead of relying on an assertion.

Custom workqueues expose `target()`, a narrow `WorkQueueTarget` capability that
allows submission without allowing queue reinitialization or lifecycle changes.
A queue is stopped only through Zephyr's supported drain-and-plug followed by
`stop()` sequence; Solar does not expose thread abort as a queue lifecycle
operation. `WorkQueueLifecycle` reports only initialization, a successful
start, and a successful stop.

`Work` and `DelayableWork` preserve Zephyr's three submission outcomes:
already queued, newly queued, and requeued after the current handler. Triggered
work uses `arm()` for its initial event set and `replace()` for Zephyr's
explicit resubmission behavior. While armed, Solar rejects mutation and direct
waiting on that `PollSet`; its event storage must still outlive execution or
cancellation. Replacement is subject to Zephyr's documented race with a
handler that has begun and reports the native `-EINVAL` as busy.

`PollState` is a native-compatible bitmask rather than a single synthetic
state. Use `has_state()` when inspecting potentially combined states. Signals,
semaphores, typed message queues, and pipes are currently supported poll
sources; cancellation results retain inspectable event state.

A reference or target must never outlive its native object. Direct Zephyr calls
remain the supported path for kernel facilities that Solar has not wrapped.

See {doc}`../reference/api/kernel`, {doc}`../concepts/concurrency-and-context`,
and {doc}`../how-to/use-from-isr`.
