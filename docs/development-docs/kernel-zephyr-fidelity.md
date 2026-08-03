# Solar Kernel And Zephyr Fidelity

Date: 2026-08-03

Status: accepted; implementation governed by
[`kernel-zephyr-fidelity-implementation-plan.md`](kernel-zephyr-fidelity-implementation-plan.md)

## 1. Purpose

This document records the fidelity audit of `solar::kernel` against the pinned
Zephyr 4.4 kernel API. It is the anti-drift surface for finishing the package.

The kernel package does not replace Zephyr or establish an independent runtime.
Its job is to expose Zephyr through modern C++ ownership, types, RAII, chrono,
spans, compile-time validation, and explicit error values while preserving the
native scheduler, context, object, and lifecycle semantics.

The current package is a useful foundation, but it is not yet a complete or
fully transparent Zephyr facade. Several wrappers maintain state that can
diverge from Zephyr, some operations intentionally or accidentally narrow the
native behavior, and major kernel object families remain uncovered.

## 2. Accepted Direction

The following decisions are already accepted:

1. Zephyr remains the source of truth for kernel state and behavior.
2. Solar may improve expression and safety, but must not silently change a
   Zephyr state machine.
3. Exact cooperative and preemptive priorities are first-class.
4. The five-level `Background`/`Low`/`Normal`/`High`/`Realtime` priority ladder
   will be removed. It collapses target configuration and is not Zephyr's
   scheduling model.
5. Application service policies must be able to select exact cooperative or
   preemptive priorities. A raw native integer escape hatch may remain when it
   is clearly identified and validated.
6. Native interoperation must be truthful. A native handle cannot be advertised
   as mutable when native calls can invalidate Solar-maintained state.
7. Unsupported Zephyr facilities must be documented as intentionally native
   only or added to Solar. They must not disappear accidentally.

No final spelling for priority policies or borrowed/native views is locked by
this document.

## 3. Zephyr Scheduling Model

Zephyr uses one signed static-priority space:

- cooperative priorities occupy `-CONFIG_NUM_COOP_PRIORITIES` through `-1`;
- preemptive priorities occupy `0` through
  `CONFIG_NUM_PREEMPT_PRIORITIES - 1`;
- a numerically lower value has higher scheduling priority;
- the idle priority is not an application-thread priority;
- configured Meta-IRQ priorities occupy the highest part of the cooperative
  range;
- deadline scheduling orders ready threads only within the same static
  priority; and
- time slicing is an independent policy affecting eligible equal-priority
  preemptive threads.

The existing low-level factories `Priority::cooperative<N>()`,
`Priority::preemptive<N>()`, and `Priority::from_native()` preserve the basic
signed Zephyr priority representation. The semantic five-level mapper is the
part that collapses it.

The high-level Application API currently accepts a non-negative integer as a
preemptive priority or one of the semantic levels. Consequently, a service
cannot use a cooperative priority through the canonical ergonomic API.

Additional priority problems are:

- default-constructed `kernel::Priority` silently selects native priority `0`,
  which is the highest preemptive priority;
- `ThreadConfiguration` and `WorkQueueConfiguration` inherit that surprising
  default;
- `SOLAR_APPLICATION_DEFAULT_SERVICE_PRIORITY` has a fixed `0..15` Kconfig
  range unrelated to the target's configured priority count;
- Meta-IRQ priorities are representable as native cooperative values but are
  not identified or guarded; and
- deadline and time-slice controls are not exposed.

## 4. Fidelity Defects

### 4.1 Shadow state conflicts with native interoperation

Solar documents that direct Zephyr calls through native handles remain valid,
but several wrappers keep independent state:

- `Mutex` tracks `owner_`; a native lock or unlock bypasses it.
- `Thread` tracks `ThreadExecutionState`; native start, suspend, resume, abort,
  and wake operations bypass it.
- `WorkQueue` tracks `started_`; native queue lifecycle calls bypass it.
- `TriggeredWork` tracks `claimed_`; native triggered-work calls bypass it.
- work wrappers track the handler thread separately from native busy state.

The native object and Solar state can therefore disagree. This breaks both the
wrapper and the documented escape hatch. State should be derived from Zephyr,
or native mutation must be prevented by the type and explicitly unsupported.

### 4.2 Work queue abort is not a Zephyr lifecycle operation

`WorkQueue::abort()` aborts the queue's backing thread and clears only Solar's
`started_` flag. It does not clear Zephyr's internal
`K_WORK_QUEUE_STARTED` state, drain pending work, or perform Zephyr's supported
stop transition. A subsequent start may assert, while the Solar destructor can
incorrectly accept the object as stopped.

This operation is unsafe and must be removed or replaced by a complete native
transition proven against Zephyr internals. Normal shutdown must use drain,
plug, and `k_work_queue_stop()`.

### 4.3 Solar thread state is not scheduler state

`ThreadExecutionState` is a wrapper lifecycle estimate. It cannot represent
Zephyr's ready, pending, sleeping, delayed, suspended-plus-pending, dead, or
currently executing conditions. It also becomes stale after native operations.

Solar currently rejects self-suspend and self-abort although Zephyr supports
both. Raw `ThreadConfiguration::options` appears to permit all Zephyr flags even
though the owning stack and wrapper lifecycle do not support every option, most
notably user-mode and essential-thread behavior.

The wrapper must distinguish owned lifecycle from observed native scheduling
state and must not imply that its local enum is a complete kernel state.

### 4.4 ISR contracts are inconsistent

Solar uses useful `_isr` and `try_` spellings, but ordinary blocking methods do
not consistently reject invalid ISR timeouts before calling Zephyr. This affects
semaphore takes, message-queue operations, event waits, polling, and related
paths. Other wrappers do perform an explicit context check.

The result is a convention rather than an enforced contract. Invalid calls can
reach a Zephyr assertion instead of returning a bounded Solar error.

### 4.5 Triggered work narrows native behavior

Zephyr permits an armed triggered-work item to be submitted again, replacing
its watched event set subject to the documented race constraints. Solar's
`claimed_` guard rejects every second submission as busy.

Lifetime protection is desirable, but replacement must either remain available
through an explicit safe operation or be called out as an intentional subset.
It must not silently appear equivalent to Zephyr submission.

`WorkErrorReason::InvalidQueue` also over-interprets `-EINVAL`; triggered work
uses that code for a different state error.

### 4.6 Diagnostics accesses private kernel layout

`set_stack_warning_margin()` directly modifies
`k_thread::stack_info.usage.unused_threshold`. This is not a public Zephyr API,
is sensitive to configuration and version changes, and can bypass kernel
synchronization.

The wrapper must use public APIs or carry a narrow, version-gated compatibility
shim that calls the public implementation symbol without accessing structure
internals.

### 4.7 Native outcomes are sometimes collapsed

Several useful distinctions are missing or inconsistently preserved:

- semaphore timeout and reset interruption both surface from Zephyr as
  `-EAGAIN` and are currently reported as timeout for a blocking take;
- `PollSignal::raise()` can report `-EAGAIN` even though the signal remains
  latched, but `Result<void>` communicates only failure;
- event wait failures omit the native result;
- message-queue purge and immediate capacity failures can share native codes;
- work errors interpret the same errno without operation-specific context; and
- `PollState` converts a native bitfield into a single enum value.

Where Zephyr itself cannot distinguish causes, Solar must document that fact.
Where Zephyr provides a meaningful partial-success outcome, Solar should retain
it in a typed result.

### 4.8 Construction and destruction rely on assertions

Several constructors call a fallible native initializer and verify it only with
`__ASSERT_NO_MSG`. Invalid semaphore counts are the clearest runtime example.
When assertions are disabled, invalid construction may not produce a usable
error.

Destructors for threads, work, slabs, and workqueues also rely on assertions to
enforce lifetime rules. Assertions are appropriate for programmer invariants,
but public runtime configuration must be validated before construction or
represented by a fallible factory.

### 4.9 Stop tokens are a companion abstraction, not a Zephyr wrapper

`StopSource` and `StopToken` are useful, but tokens contain a raw pointer to the
source-owned state and do not keep it alive. `reset()` also changes what all
existing tokens observe, unlike the one-shot semantics associated with C++ stop
tokens.

This facility should remain possible, but its lifetime and reset contract must
be explicit. It should not be mistaken for a direct Zephyr primitive or a full
`std::stop_token` equivalent.

## 5. Incomplete Zephyr Surface

Solar currently covers the common paths for time, mutexes, condition variables,
semaphores, events, typed message queues, pipes, memory slabs, timers, poll,
threads, work, workqueues, spinlocks, scheduler locks, diagnostics, and fatal
handling.

The following public Zephyr areas are absent or only partly covered:

- intrusive queues, FIFO, and LIFO;
- kernel stack objects;
- mailboxes;
- synchronized heaps, aligned allocation, and heap statistics;
- futexes;
- message-queue attributes and dynamic storage;
- memory-slab maximum-use statistics and reset;
- event masked-set operations;
- poll sources for queues/FIFOs and pipes;
- thread wakeup and sleep-time inspection;
- deadline scheduling and scheduler rescheduling;
- global and per-thread time slicing;
- CPU affinity, pinning, and SMP iteration;
- IPI work;
- dynamic thread stacks;
- userspace threads, permissions, resource pools, and memory domains;
- thread custom data;
- floating-point context control; and
- CPU idle operations.

Completeness does not require wrapping every native call immediately. Each area
must be classified as one of:

1. canonical Solar wrapper;
2. deliberately exposed through a focused native view;
3. intentionally unsupported, with rationale; or
4. planned work with a test gate.

## 6. Configuration And Portability Gaps

The current test matrix primarily exercises `native_sim` with ordinary
multithreading and system-clock configuration. It does not establish behavior
for:

- cooperative-only or preemptive-only builds;
- small and large configured priority ranges;
- Meta-IRQ priorities;
- deadline scheduling and time slicing;
- ARM interrupt behavior;
- SMP and CPU affinity;
- userspace;
- `CONFIG_MULTITHREADING=n`;
- `CONFIG_SYS_CLOCK_EXISTS=n`;
- native mutation through escape hatches; or
- cancellation, reset, and purge races.

The kernel facade needs a Zephyr configuration matrix, not just broader unit
coverage on one platform.

## 7. Strengths To Preserve

The redesign must retain the parts that already work well:

- exact native handles where their contract is truthful;
- allocation-free owned storage by default;
- non-copyable and non-movable address-stable kernel objects;
- chrono-based durations and reusable deadlines;
- spans for byte transfers;
- typed, trivially-copyable fixed-capacity message queues;
- RAII guards for mutexes, scheduler locking, interrupt locking, spinlocks,
  memory-slab blocks, and unique locks;
- explicit `Result` values instead of ignored integer error codes;
- compile-time capacity, alignment, and priority validation;
- focused Kconfig availability diagnostics; and
- standalone use without System, Remote, or application generation.

## 8. Completion Gates

The package is considered faithful only when:

1. exact cooperative, preemptive, and raw native priority choices work through
   both kernel and Application APIs;
2. no default silently selects an unexpectedly high priority;
3. every mutable native handle has a proven synchronization contract;
4. no Solar lifecycle state can contradict Zephyr's state;
5. workqueue shutdown uses a supported Zephyr transition;
6. ISR-invalid calls are rejected consistently before reaching native asserts;
7. all intentional semantic restrictions are named and documented;
8. no wrapper accesses private Zephyr object layout;
9. every major Zephyr kernel family is classified and the selected canonical
   subset is implemented;
10. API documentation exactly matches the actual type names and templates; and
11. the configuration test matrix covers native simulation, ARM, SMP where
    supported, priority variants, optional facilities, and native
    interoperation.
