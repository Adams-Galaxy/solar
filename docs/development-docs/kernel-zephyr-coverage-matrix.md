# Solar Kernel Baseline And Zephyr Coverage Matrix

Date: 2026-08-03

Status: Phase 0 baseline recorded; maintained throughout the kernel fidelity work

Implementation plan:
[`kernel-zephyr-fidelity-implementation-plan.md`](kernel-zephyr-fidelity-implementation-plan.md)

## 1. Baseline

The source baseline is Solar commit `936e0b1` on `developmental_modulatiry` and
robot commit `46d27f2` on `developmental`. The dependency is the project's
manifest-managed Zephyr 4.4 fork at `aa04d575`.

Before the first API change:

- the LLVM host build completed 68 tests in 16.88 seconds with no failures;
- native-simulation kernel core and execution completed 17 cases across two
  configurations with no failures;
- the native test configuration provided 16 cooperative priorities, 15
  preemptive priorities, and no Meta-IRQ priorities;
- the robot selected semantic `High`, which Solar collapsed into preemptive
  priority 3 on that configuration;
- the system workqueue used native priority -1 and the Zephyr main thread used
  priority 0; and
- the existing optimized Teensy artifact reported 201,280 bytes of text,
  99,524 bytes of data, and 131,749 bytes of BSS. Its binary was 296 KiB.

The Teensy measurement is a comparison point, not a size budget. It comes from
`firmware/build/teensy40-release/zephyr/zephyr.elf` produced before the priority
migration; subsequent gates must rebuild rather than reuse that artifact.

The existing size assertions establish these ownership costs on the native
test ABI:

- `RecursiveMutex`, `Semaphore`, and `EventFlags` are the size of their native
  object;
- `Mutex` adds an atomic owner to `k_mutex`;
- `MessageQueue<T, N>` contains `k_msgq` and its complete message buffer;
- `PollSet<N>` contains at least its `N` native events;
- `Timer` contains `k_timer` and two callback pointers; and
- `Thread<N>` and `WorkQueue<N>` own at least the requested kernel stack.

## 2. Current Wrapper Interoperation

This matrix records the Phase 2 boundary. A raw handle appears only on a value
type or transparent borrowed reference whose invariants survive native calls.

| Solar surface | Solar-only state | Borrowing/interoperation | Owner raw handle |
| --- | --- | --- | ---: |
| `Priority`, `Timeout`, `Deadline` | validated typed value | exact native value conversion | value |
| `Thread<N>` | entry, argument, ID, lifecycle state | `ThreadRef`; observable native ID only | no |
| `Mutex` | non-recursive owner tracking | deliberately none; condition wait is a private capability | no |
| `RecursiveMutex` | none | `RecursiveMutexRef`, including native handle | no |
| `ConditionVariable` | none | `ConditionVariableRef`, including native handle | no |
| `Semaphore` | none | `SemaphoreRef`, including native handle | no |
| `EventFlags` | none | `EventFlagsRef`, including native handle | no |
| `MessageQueue<T, N>` | owned typed buffer and capacity | `MessageQueueRef<T>` typed operations; no reinitialization handle | no |
| `Pipe<N>` | owned byte buffer and capacity | `PipeRef` operations; no reinitialization handle | no |
| `MemorySlab<B, N>` | owned aligned storage | `MemorySlabRef<B>` and native-backed RAII block | no |
| `Timer` | callback pointers and user data | `TimerRef` operations; no reinitialization handle | no |
| `SpinLock` | none | `SpinLockRef`, including native handle | no |
| `InterruptLock`, `SchedulerLock` | acquisition ownership | focused RAII capability | n/a |
| `PollSignal` | none | `PollSignalRef`, including native handle | no |
| `PollSet<N>` | event storage, count, and tags | private triggered-work capability only | no |
| `Work`, `DelayableWork` | handler and running handler ID | submission APIs only | no |
| `TriggeredWork` | handler, event lifetime, armed state | submission APIs only | no |
| `WorkQueue<N>` | stack and lifecycle state | `WorkQueueTarget` submission capability | no |
| system workqueue | Zephyr-owned | `system_work_queue` submission capability | n/a |
| `StopSource`, `StopToken` | companion stop generation | no native claim | n/a |
| diagnostics/fatal | normalized snapshots/latched fatal state | observable thread identity and public Zephyr APIs | n/a |

Borrowed references never initialize or own the native object. `MemorySlabBlock`
stores the native slab identity rather than a Solar owner pointer, so the same
RAII block type works for Solar-owned and Zephyr-owned slabs. All references
retain the ordinary requirement that the native object outlive the reference
and any outstanding wait or allocation.

## 3. Zephyr Kernel Coverage

### Context contract inventory

| Context class | Solar operations |
| --- | --- |
| Unconditionally ISR-safe | semaphore `give/reset/count`; event `post/set/clear/test`; queue `try_send_front/peek/peek_at/purge` and size queries; slab release/statistics; timer `stop` and time queries; work submit/cancel/state queries; triggered-work submit/cancel-trigger/state queries; workqueue `unplug`; spin/interrupt locks; busy wait; poll-signal raise/reset/check |
| Explicit no-wait ISR | semaphore `try_take_isr`; queue `try_send_isr/try_receive_isr`; event `try_wait_any_isr/try_take_any_isr`; slab `try_allocate_isr` |
| Thread-only with pre-call rejection | mutex and condition-variable operations; ordinary wait-capable semaphore, queue, event, and slab calls; all pipe transfers; poll wait; timer start/sync; thread creation and join; current-thread sleep/yield/priority access; scheduler lock; stop-token waits and source mutation; work/workqueue synchronous cancellation, flush, drain, start, stop, and abort |

An ordinary `try_` method remains thread-only when it is the no-wait form of a
wait-capable method. Only the explicit `_isr` spelling crosses that boundary.
Operations in the first row keep one ordinary name because Zephyr permits them
from either context without changing their contract.

| Zephyr 4.4 family | Baseline Solar coverage | Planned disposition |
| --- | --- | --- |
| Priority values | coop/preemptive factories; semantic collapsing above them | Phase 1 exact native, cooperative, preemptive, and optional Meta-IRQ values |
| Thread create/start/join/abort | static stack owner with partial shadow lifecycle | Phases 2-4 owner/reference redesign and scheduler controls |
| Sleep, yield, busy wait | wrapped with chrono | Phase 5 overflow, no-clock, and context audit |
| Scheduler lock | RAII wrapper | Phase 4 reschedule/preemptibility/time slicing/deadlines |
| Mutex and condition variable | wrapped | Phases 2, 3, and 5 fidelity repair |
| Semaphore and events | wrapped | Phases 2, 3, and 5; add masked event set |
| Message queue and pipe | typed fixed-storage wrappers | Phases 2, 3, and 5 attributes, references, and context repair |
| Memory slab | typed fixed-storage wrapper | Phases 2, 3, 5, and 9 lifetime/statistics work |
| Timer | callback owner | Phases 2, 3, and 5 lifecycle/time repair |
| Work and delayable work | callback owners and queue submission | Phase 6 native transition repair |
| Triggered work | restricted single-arm wrapper | Phase 6 explicit arm and replacement model |
| Workqueue | static owner with unsafe abort and shadow state | Phase 6 supported lifecycle plus submission target |
| Poll and signals | selected signal/semaphore/message-queue sources | Phase 6 exact state bits and broader sources |
| Spin/IRQ locks | RAII wrappers | Phases 2, 3, and 5 context/native-access audit |
| Fatal handling | optional observer bridge | Phase 7 real fatal-path verification |
| Thread diagnostics | optional snapshots using some private layout | Phase 7 public API repair |
| Intrusive queue/FIFO/LIFO | absent | Phase 8 typed zero-copy wrappers |
| Kernel word stack | absent | Phase 8 constrained typed wrapper |
| Heap | absent | Phase 9 fixed-storage owner/reference and optional PMR adapter |
| Mailbox | absent | Phase 10 typed rendezvous wrapper |
| Userspace/object permissions/memory domains | absent | deferred; direct Zephyr API |
| Dynamic thread stacks | absent | deferred; direct Zephyr API |
| SMP affinity, CPU masks, and IPI work | absent | deferred; direct Zephyr API |
| Futex | absent | deferred; direct Zephyr API |
| Floating-point thread context | absent | deferred; direct Zephyr API |
| CPU idle/atomic idle | absent | deferred; direct Zephyr API |
| Object core/global runtime statistics | partial diagnostics only | deferred beyond selected diagnostics |

## 4. Required Configuration Fixtures

The final matrix must contain executable or compile-only fixtures for:

- ordinary mixed cooperative/preemptive scheduling;
- cooperative-only and preemptive-only builds;
- reduced and non-default priority counts;
- Meta-IRQ priorities enabled and disabled;
- poll and events enabled and disabled;
- system clock and supported no-clock builds;
- supported no-multithreading compilation;
- ARM QEMU behavior and ISR offload;
- an SMP-capable target for lock/reference safety; and
- optimized Teensy application integration.

At baseline, only native mixed-priority, native optional-feature-disabled, and
Teensy build coverage exist. Missing rows remain Phase 12 work and must not be
treated as verified merely because the default native configuration passes.

## 5. Evidence Log

### 2026-08-03 — baseline

```text
Solar commit: 936e0b1
Robot commit: 46d27f2
Zephyr commit: aa04d575
Host: ctest --test-dir build/host-llvm --output-on-failure
Host result: 68/68 passed, 16.88 s
Native: west twister -T kernel_core -T kernel_execution -p native_sim/native/64
Native result: 2/2 configurations and 17/17 cases passed
Teensy reference size: text 201280, data 99524, bss 131749, binary 296 KiB
Known limitations: all non-default configuration and architecture rows above
```

### 2026-08-03 — exact priorities

```text
Solar worktree after baseline: Phase 1 implementation
Host: 68/68 passed
Native: 7/7 configurations passed or built; 18/18 runtime cases passed
Priority matrix: mixed, cooperative-only, preemptive-only, reduced mixed,
                 and Meta-IRQ configurations built
Compile-fail: invalid preemptive, cooperative, native-low, native-high,
              Meta-IRQ, and Application policy values produced focused markers
Teensy: optimized/LTO application built; text 201276, data 99524, bss 131749
Documentation audit: 18 aggregates, 9 subsystems, 67 Kconfig symbols passed
```

### 2026-08-03 — ownership/reference boundary

```text
Solar worktree after Phase 1: Phase 2 implementation
Host: 68/68 passed
Native: kernel core, kernel execution, and Remote protocol all passed;
        21/21 runtime cases
Borrowing: native semaphore, recursive mutex, message queue, event, timer,
           poll signal, condition variable, pipe, memory slab, spinlock, and
           current thread exercised through typed references
Compile-fail: mutable owner handles rejected for every shadow-state owner,
              including Mutex, Thread, WorkQueue, Timer, ConditionVariable,
              Pipe, MemorySlab, and SpinLock
Condition wait: failed Zephyr 4.4 waits correctly disown UniqueLock because the
                native mutex is not reacquired on -EAGAIN
Teensy: optimized/LTO application built; FLASH 301492 B, RAM 198040 B
Documentation audit: 18 aggregates, 9 subsystems, 67 Kconfig symbols passed
Known limitations: context rejection and errno preservation are assigned to
                   Phase 3
```

### 2026-08-03 — context vocabulary foundation

```text
Solar worktree after Phase 2: Phase 3 first implementation block
Native: kernel core and kernel execution passed; 19/19 runtime cases
ISR table: ordinary wait-capable semaphore, message queue, event, memory slab,
           pipe, poll, mutex, and timer-start calls reject before Zephyr;
           explicit no-wait ISR variants succeed where Zephyr permits them
API cleanup: unconditional ISR-safe operations use their ordinary names;
             redundant work, timer, semaphore, and event aliases removed
Errors: semaphore, message queue, memory slab, pipe, workqueue, and triggered
        work mappings preserve native errno for mapped failures
Teensy: optimized/LTO application built; FLASH 301068 B, RAM 198040 B
Known limitations: the complete per-operation context inventory, richer poll
                   outcomes, constructor audit, and race tests remain Phase 3
```

### 2026-08-03 — context and error contracts

```text
Solar worktree after context foundation: Phase 3 completion
Native default: kernel core, execution, and Remote protocol passed; 21/21 cases
Native assertions-disabled: kernel core and execution exercised with
                            CONFIG_ASSERT=n
Race coverage: semaphore reset -> -EAGAIN, queue purge -> -ENOMSG,
               pipe reset -> -ECANCELED, pipe close -> -EPIPE
Construction: native initializers execute independently of assertions;
              semaphore configuration is compile-time and typed queue borrowing
              validates native item size at runtime
Partial success: poll-signal timeout race is a successful LatchedAfterTimeout
                 outcome retaining native -EAGAIN
Teensy: optimized/LTO application built; FLASH 301068 B, RAM 198040 B
Known limitations: exact thread and scheduler control expansion begins Phase 4
```

### 2026-08-03 — thread and scheduler controls

```text
Solar worktree after Phase 3: Phase 4 implementation
Lifecycle: owner records Empty, Prepared, Started, Finished, or owner-issued
           Aborted facts; suspend/resume are no longer claimed as scheduler state
ThreadRef: native/current/owned priority, wake, suspend/resume, abort, join,
           exit, wake deadline, and wake remaining operations
Self operations: suspend and abort exercised through this_thread
Scheduling: reschedule, preemptibility, global and per-thread time slicing,
            relative and absolute hardware-cycle deadlines
Options: raw flags removed; supervisor-safe ThreadOptions required; raw K_USER
         and direct option construction rejected by compile-fail cases 26/27
Native: default kernel core/execution and deadline/per-thread-slice variants pass
Known limitations: user mode, essential threads, CPU affinity, and callable
                   adapters remain explicitly deferred
```

### 2026-08-03 — existing primitive repairs

```text
Events: atomic masked set added
Message queues: native size/capacity/used/free attributes exposed
Memory slabs: current/max usage and maximum reset exposed under Zephyr tracing;
              outstanding block lifetime requirement documented
Timers: K_FOREVER start preserved as Zephyr's successful no-op
Time: positive sub-tick durations round up; integral overflow saturates on
      both 64-bit native simulation and 32-bit Cortex-M toolchains
Native: default core/execution 22/22; slab-statistics variant 14/14
ARM QEMU: core 8/8 and execution 14/14 passed
Fixture repair: ARM exposed insufficient Ztest stack for locally owned thread
                stacks; execution fixture now provisions 16 KiB
Known limitation: native_sim forces a timer and cannot represent a no-clock
                  runtime; no-clock compile coverage remains a Phase 12 matrix gate
```

### 2026-08-03 — work, workqueues, triggered work, and poll

```text
Work: 0/1/2 submission outcomes remain AlreadyQueued/Queued/RequeuedAfterCurrent;
      submission and triggered-work error maps are operation-specific
Workqueues: unsupported backing-thread abort removed; drain, plug, unplug, stop,
            and the Zephyr 4.4 stopped-queue restart transition pass
Triggered work: explicit arm/replace API; same-queue replacement, timeout,
                cancellation, cross-queue -EADDRINUSE, and event-array lifetime
                locking covered
Poll: native state bits preserved as a bitmask; pipe source added alongside
      signal, semaphore, and message queue; direct wait/mutation rejected while
      a triggered work item owns the set
Native: core/execution 22/22 passed; focused execution 15/15 passed
ARM QEMU: core 8/8 and execution 15/15 passed
Teensy optimized/LTO: FLASH 301396 B, RAM 198040 B
Deferred dependency: queue/FIFO poll sources and live -EINTR cancellation test
                     land with the Phase 8 intrusive queue owners
```

### 2026-08-03 — diagnostics, fatal handling, and stop generations

```text
Diagnostics: no Solar header or source accesses k_thread private layout;
             borrowed stack totals remain optional unless supplied by caller
Zephyr 4.4 compatibility: runtime stack-threshold implementation-name mismatch
                          isolated in one exact-version translation unit
Configuration: enabled execution diagnostics and disabled diagnostics fixtures pass
Fatal bridge: controlled worker k_oops reaches the real Zephyr fatal handler;
              observer validates KernelOops, native reason, requested status,
              and the already-latched fatal snapshot before worker termination
Stop tokens: reset advances a 32-bit lock-free generation, wakes old waiters,
             keeps every old token stopped, and leaves new tokens unstopped
Native: execution 15/15 and disabled diagnostics 1/1 passed
ARM QEMU: execution 15/15 passed; 32-bit target rejected a non-lock-free
          64-bit generation during development, so the final counter is uint32_t
```

### 2026-08-03 — intrusive queues and kernel word stack

```text
Node layout: explicit first-word Zephyr link, standard-layout assertion, typed
             value, atomic queue identity, and linked-destruction assertion
Queue: append/prepend/insert/remove/unique append/atomic list append, peek,
       blocking/no-wait/ISR get, cancellation, borrowing, and node reuse
FIFO/LIFO: first-in-first-out and last-in-first-out typed surfaces verified
Poll: queue/FIFO/LIFO source support added; live k_queue_cancel_wait produces
      successful interrupted PollResult with inspectable Cancelled state
Stack: fixed owner/reference over k_stack; unsigned integer, enum, and pointer
       round-trip constraint; blocking/no-wait/ISR pop and ISR-safe push
Native: core/execution 25/25 passed
ARM QEMU: core/execution 25/25 passed
Compile-fail: signed stack value and zero-capacity cases 28/29 rejected
Teensy optimized/LTO: FLASH 301500 B, RAM 198040 B
```

### 2026-08-03 — bounded heap and PMR

```text
Heap: fixed aligned owner and native HeapRef; allocate/aligned/calloc/realloc,
      free, timeout/deadline/no-wait, and explicit ISR no-wait surfaces
Behavior: alignment, zeroing, realloc preservation, exhaustion, finite timeout,
          borrowed native heap, and ISR context rejection/success covered
PMR: vector backing storage proven to reside in the selected heap; failure
     policy mandatory at construction; exception-disabled exhaustion reaches
     the expected Zephyr panic path in a dedicated fatal-hook test
Storage statistics: message-queue capacity/used/free and memory-slab
                    current/maximum/reset coverage retained from Phase 5
Native: kernel core 11/11 passed
ARM QEMU: kernel core 11/11 passed
Compile-fail: heap size/alignment, owner native access, and omitted PMR policy
              cases 30-33 rejected
```
