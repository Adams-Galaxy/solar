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

This is the initial matrix. “Mutable handle” describes the API before the
corresponding ownership phase is completed.

| Solar surface | Owns native storage | Solar-only state | Mutable handle | Final direction |
| --- | ---: | --- | ---: | --- |
| `Priority` | no | validated signed value | n/a | exact value type |
| `Timeout`, `Deadline` | no | typed time value | n/a | exact value types |
| `Thread<N>` | yes | entry, argument, ID, lifecycle state | yes | owner plus `ThreadRef`; no mutating owner handle |
| `Mutex` | yes | non-recursive owner tracking | yes | retain owner invariant; no mutable handle |
| `RecursiveMutex` | yes | none | yes | transparent owner plus `RecursiveMutexRef` |
| `ConditionVariable` | yes | none | yes | transparent owner/reference where useful |
| `Semaphore` | yes | none | yes | transparent owner plus `SemaphoreRef` |
| `EventFlags` | yes | none | yes | transparent owner plus `EventFlagsRef` |
| `MessageQueue<T, N>` | yes | owned byte buffer | yes | owner plus typed `MessageQueueRef<T>` |
| `Pipe<N>` | yes | owned byte buffer | yes | owner plus `PipeRef` |
| `MemorySlab<B, N>` | yes | owned aligned blocks | yes | owner plus slab reference and explicit block lifetime |
| `Timer` | yes | callback pointers | yes | owner/reference split without callback mutation holes |
| `SpinLock` | yes | none | yes | transparent exact owner/reference |
| `InterruptLock` | no | saved architecture key | n/a | companion RAII capability |
| `SchedulerLock` | no | acquisition ownership | n/a | companion RAII capability |
| `PollSignal` | yes | none | yes | transparent owner plus reference |
| `PollSet<N>` | yes | count and tag metadata | event array | retain controlled event storage; broaden sources |
| `Work` | yes | handler and running handler ID | yes | owner; focused queue submission capability |
| `DelayableWork` | yes | handler and running handler ID | yes | owner; focused queue submission capability |
| `TriggeredWork` | yes | handler, event pointer/count, armed state | yes | redesign arm/replacement ownership |
| `WorkQueue<N>` | yes | stack and `started_` | yes | owner plus `WorkQueueTarget`; remove abort |
| `SystemWorkQueue` | no | none | yes | non-owning `WorkQueueTarget` |
| `StopSource`, `StopToken` | companion | shared stop generation | n/a | one-shot generations; not called native Zephyr |
| diagnostics/fatal | no | normalized snapshots/latched fatal state | thread IDs | public Zephyr API boundary only |

No new mutable native handle is permitted until its row has a native mutation
test showing that Solar lifecycle and destruction remain correct.

## 3. Zephyr Kernel Coverage

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
