# Stage 05: Kernel Execution Foundation

Status: landed

Landed date: 2026-07-16

Implementation repository/branch: `/workspaces/solar`, `static_reform`

Baseline: Stage 04 landed worktree based on `bed432f pre-solar-implementation`

## 1. Objective

Complete the direct Zephyr kernel foundation required by static lifecycle,
service execution, executor integration, deferred subsystem processing, and
later supervision. The result must prepare owned threads and work without
executing user behavior before an explicit release, remain usable without a
Blueprint or bound System, and introduce no hidden heap, worker, queue, or
runtime registry.

## 2. Specification Coverage

| Specification | Coverage | Notes |
| --- | --- | --- |
| `00-design-conventions.md` | static ownership, explicit context, bounded state, focused results | complete for Stage 05 |
| `00a-modern-cpp-result-and-status.md` | `Result<T, E>`, chrono values, move-only ownership handles | complete |
| `03-lifecycle-kernel-and-configuration.md` | activation barrier prerequisites, thread containment, diagnostics, Kconfig ownership | complete for the Stage 06 prerequisite surface |
| `09-tasks-and-executors.md` | direct work/workqueue semantics and Kernel/Execution boundary | complete for Kernel; registration remains Stage 07 |
| `12-health-and-supervision.md` | direct thread evidence, stack/runtime facts, fatal boundary, practical missing primitives | complete for the Stage 05 roadmap scope |
| `14-integrated-architecture.md` | Zephyr-native and System-independent Kernel ownership | preserved |
| Stage 05 of `04-implementation-roadmap.md` | implementation and verification scope | complete |

The broader optional Kernel families accepted by Spec 12 are not all critical
path prerequisites for Stage 06. Task-watchdog/provider behavior remains with
Supervisor Stage 19. Optional userspace/futex, SMP affinity, intrusive
FIFO/LIFO/mailbox, and dynamic heap/block-pool conveniences remain for their
need-driven subsystem work or the Stage 20 Kernel coverage audit. They are not
represented by placeholders or hidden fallback behavior.

## 3. Public Surface Landed

The aggregate remains:

```cpp
#include <solar/kernel.hpp>
```

### Threads and cooperative stop

- `kernel::Thread<StackBytes>` owns one native control block and one statically
  sized kernel stack.
- `prepare()` creates a dormant thread; `start()` is the explicit release.
- `launch()` supports immediate and native delayed launch.
- `suspend()`, `resume()`, bounded `join()`, no-wait `exited()`, and explicit
  `abort()` preserve native containment operations.
- `ThreadConfiguration` carries native priority, optional name, and options.
- `StopSource` and copyable non-owning `StopToken` provide one-way cooperative
  cancellation with query and bounded wait.
- `solar::StopToken` is the common callback vocabulary alias; no object is
  passed implicitly by Solar.

```cpp
solar::kernel::Thread<2048> worker;
solar::kernel::StopSource stop;

worker.prepare(&run_worker, &state,
               {.priority = solar::kernel::Priority::preemptive<2>(),
                .name = "control"});

// Lifecycle can prepare every owner first, then release only after commit.
worker.start();
stop.request_stop();
worker.join(solar::kernel::Timeout::after(100ms));
```

### Work and workqueues

- `Work` wraps ordinary Zephyr work.
- `DelayableWork` preserves schedule versus reschedule semantics.
- `TriggeredWork` wraps poll-triggered work when `CONFIG_POLL` is enabled.
- `WorkSubmission` preserves Zephyr's exact successful outcomes:
  `AlreadyQueued`, `Queued`, and `RequeuedAfterCurrent`.
- `WorkError` retains focused reason, Solar status, and native errno.
- asynchronous cancel, synchronous cancel, flush, state, pending, current
  handler detection, and explicit ISR submission forms are available.
- `SystemWorkQueue` is a stateless non-owning adapter over `k_sys_work_q`.
- `WorkQueue<StackBytes>` owns one native queue and static stack and exposes
  start, drain/plug, unplug, and bounded stop.

Triggered work additionally tracks the native pre-queue polling phase. An
armed item is live even before ordinary work flags become busy, so destruction,
flush, cancellation, and duplicate submission cannot mistake it for idle.

### Additional bounded primitives

- `ConditionVariable` works with `UniqueLock<Mutex>` while preserving Solar's
  non-recursive owner checks across the native release/reacquire operation.
- `SpinLock` provides a move-only lexical guard over `k_spinlock`.
- `MemorySlab<BlockBytes, BlockCount, Alignment>` owns aligned inline storage
  and returns move-only RAII blocks.
- `Pipe<Capacity>` owns a bounded byte buffer and exposes read, write,
  non-blocking, reset, close, deadline, and native-handle operations.

### Diagnostics and fatal boundary

Focused facts replace broad snapshots:

```cpp
solar::kernel::thread_diagnostics(thread);
solar::kernel::stack_usage(thread);
solar::kernel::runtime_stats(thread);
solar::kernel::thread_exited(thread);
solar::kernel::check_stack_safety(thread, true);
```

Optional availability facts expose thread names, stack diagnostics, runtime
statistics, runtime stack safety, and global enumeration independently.
Locked and unlocked global thread iteration are explicitly distinct.

`FatalReason`, `FatalError`, `panic()`, `fatal_halt()`, an atomic fatal latch,
and one optional panic-safe observer form the fatal boundary. The strong Zephyr
fatal handler is linked only when `CONFIG_SOLAR_FATAL_BRIDGE=y`.

## 4. Runtime Ownership

| Owner | Storage/resource | Capacity | Synchronization | Lifetime |
| --- | --- | --- | --- | --- |
| `Thread<N>` | one `k_thread`, one native kernel stack, callback/argument, atomic ID/state | one owned thread | Zephyr scheduler plus atomic wrapper state | object lifetime; termination must be proven before destruction |
| `StopSource` | atomic request bit, one `k_mutex`, one `k_condvar` | one irreversible request | acquire/release atomic plus native mutex/condition | source must outlive tokens and waiters |
| `Work` | one `k_work`, callback, atomic current-handler ID | one coalesced work item | Zephyr work lock plus atomic handler fact | object must be idle at destruction |
| `DelayableWork` | one `k_work_delayable`, callback, atomic handler ID | one delay and work item | Zephyr timeout/work state | object must be idle at destruction |
| `TriggeredWork` | one `k_work_poll`, callback, handler ID, atomic claim | one poll registration/work item | Zephyr poll/work locks plus atomic ownership claim | events and item live through execution or cancellation |
| `SystemWorkQueue` | non-owning reference to `k_sys_work_q` | native system queue | Zephyr-owned | system lifetime |
| `WorkQueue<N>` | one `k_work_q`, one native kernel stack, started bit | one owned queue thread | Zephyr workqueue plus atomic started fact | explicit drain/plug/stop before destruction |
| `MemorySlab<B, N>` | one `k_mem_slab` and `N` aligned inline blocks | exactly `N` | native slab lock | slab outlives all blocks; zero outstanding blocks at destruction |
| `Pipe<N>` | one `k_pipe` and `N` inline bytes | exactly `N` bytes | native pipe synchronization | object lifetime |
| `ConditionVariable` | one `k_condvar` | native waiters | native condition and paired mutex | object lifetime |
| `SpinLock` | one `k_spinlock` | one lexical owner per acquisition | native spinlock | object lifetime |
| fatal bridge | three lock-free atomics | one observer and one terminal reason | lock-free atomics only | firmware lifetime when enabled |

There is no dynamic allocation, hidden thread, hidden workqueue, constructor
registration, static runtime registry, System slot, or implicit execution
target. All owning objects are non-copyable and non-movable. Work callbacks,
thread callbacks, stop tokens, and native handles are non-owning.

Native 64-bit simulation measurements from the final debug ELF were:

| Type | Wrapper size | Native/static basis |
| --- | ---: | --- |
| `Thread<2048>` | 2352 B | `k_thread` 272 B plus 2048 B native stack and wrapper facts |
| `Work` | 48 B | `k_work` 32 B plus callback and handler ID |
| `DelayableWork` | 88 B | `k_work_delayable` 72 B plus callback and handler ID |
| `TriggeredWork` | 136 B | `k_work_poll` 112 B plus callback, handler ID, and claim |
| `WorkQueue<2048>` | 2392 B | `k_work_q` 336 B plus 2048 B native stack and started fact |
| `MemorySlab<16, 2>` | 96 B | `k_mem_slab` 64 B plus 32 B block storage |
| `Pipe<8>` | 104 B | `k_pipe` 96 B plus 8 B byte storage |
| `StopSource` | 56 B | request bit, native mutex, and native condition |
| `ConditionVariable` | 16 B | one `k_condvar` |
| `SpinLock` | 8 B | one `k_spinlock` |

Sizes are target and configuration dependent. Static lower-bound, ownership,
and move/copy assertions remain in the runtime suite.

## 5. Compile-Time Behavior

- Thread and workqueue stacks must be non-zero.
- Memory-slab block size/count must be non-zero, count must fit Zephyr's
  native width, and alignment must be a power of two.
- Pipe capacity must be non-zero.
- Triggered-work construction is a focused compile failure when polling is
  disabled.
- Optional diagnostic headers remain includable when capabilities are off;
  calls return `NotSupported` and availability facts are false.
- The fatal bridge contributes a compiled source and strong handler only when
  its Kconfig is enabled.
- Twenty-eight public Kernel headers pass isolated compilation using the real
  generated Zephyr compile database and optional capabilities disabled.
- No binding, catalog, generated application code, or System type is required.

Stable focused compile diagnostics added in this stage are:

- `SOLAR_DIAGNOSTIC_THREAD_ZERO_STACK`;
- `SOLAR_DIAGNOSTIC_WORK_QUEUE_ZERO_STACK`;
- `SOLAR_DIAGNOSTIC_MEMORY_SLAB_ZERO_BLOCK`;
- `SOLAR_DIAGNOSTIC_MEMORY_SLAB_ZERO_CAPACITY`;
- `SOLAR_DIAGNOSTIC_MEMORY_SLAB_INVALID_ALIGNMENT`;
- `SOLAR_DIAGNOSTIC_PIPE_ZERO_CAPACITY`;
- `SOLAR_DIAGNOSTIC_TRIGGERED_WORK_DISABLED`;
- `SOLAR_DIAGNOSTIC_FATAL_OBSERVER_NOT_LOCK_FREE`.

## 6. Error And Availability Behavior

| Condition | Result |
| --- | --- |
| prepare/start/launch on an active owner | `Already` |
| start before prepare, join prepared thread, or operation before start | `NotReady` |
| finite thread/stop wait expiry | `Timeout` |
| no-wait join while alive | `WouldBlock` or `false` predicate |
| self suspend/abort or blocking ISR use | `Invalid` |
| synchronous work operation from its own handler | `Deadlock` |
| workqueue stopped, invalid, busy, or owned by another queue | structured `WorkError` with native errno |
| triggered work armed but not queued | `Triggered` state; flush/duplicate submit returns `Busy` |
| triggered work receives an empty poll set | `InvalidEvents` |
| immediate slab exhaustion | `NoMemory` |
| finite slab allocation expiry | `Timeout` |
| immediate pipe transfer cannot proceed | `WouldBlock` |
| pipe wait cancelled/reset | `Cancelled` |
| disabled diagnostic family | `NotSupported` |
| fatal bridge disabled | install/query returns `NotSupported` |
| no fatal reason latched yet | `NotReady` |

No result silently collapses Zephyr work submission disposition 0, 1, or 2.

## 7. Zephyr Integration

The implementation directly uses Zephyr 4.4 public storage and operations:

- `k_thread_create/start/suspend/resume/join/abort`, native kernel stack macros,
  thread names, priorities, and stack/runtime queries;
- `k_mutex`, `k_condvar`, `k_spinlock`, `k_mem_slab`, and `k_pipe`;
- `k_work`, `k_work_delayable`, `k_work_poll`, `k_work_q`, and
  `k_sys_work_q`;
- `k_thread_foreach` and `k_thread_foreach_unlocked`;
- runtime stack-safety checks and fatal APIs.

Owned workqueue stop remains faithful to Zephyr: the caller must first drain
and plug the queue. Solar does not silently alter queue state inside `stop()`.
The system queue adapter intentionally exposes no global drain, plug, stop, or
abort ownership.

Zephyr 4.4 declares runtime-stack threshold syscall names that do not match the
implementation symbols in `kernel/thread.c`. Solar confines the pinned-version
compatibility workaround to `diagnostics.hpp`, validating and updating the same
guarded `stack_info.usage.unused_threshold` field used by Zephyr. A later
Zephyr baseline can replace only those focused lines when the native API is
corrected.

## 8. Files Changed

### Added

- `include/solar/kernel/condition_variable.hpp`
- `include/solar/kernel/fatal.hpp`
- `include/solar/kernel/memory_slab.hpp`
- `include/solar/kernel/pipe.hpp`
- `include/solar/kernel/spinlock.hpp`
- `include/solar/kernel/stop.hpp`
- `include/solar/kernel/triggered_work.hpp`
- `src/kernel/fatal.cpp`
- `tests/zephyr/kernel_execution/`
- `tests/zephyr/kernel_diagnostics_disabled/`
- `tests/zephyr/kernel_fatal_bridge/`
- `tests/zephyr/check_kernel_headers.py`

### Reshaped

- `include/solar/kernel/thread.hpp`
- `include/solar/kernel/work.hpp`
- `include/solar/kernel/work_queue.hpp`
- `include/solar/kernel/diagnostics.hpp`
- `include/solar/kernel/mutex.hpp`
- `include/solar/kernel/priority.hpp`
- `include/solar/kernel/native.hpp`
- `include/solar/kernel/kernel.hpp`
- `CMakeLists.txt`
- `zephyr/Kconfig`
- `tests/zephyr/kernel_compile_fail/src/main.cpp`

### Removed

None beyond the superseded Kernel files already removed in Stages 00 and 04.

## 9. Tests And Evidence

| Command | Platform/configuration | Result | What it proves |
| --- | --- | --- | --- |
| `west twister -T tests/zephyr/kernel_execution -T tests/zephyr/kernel_diagnostics_disabled -T tests/zephyr/kernel_fatal_bridge ... --warnings-as-errors -j 1` | native 64, diagnostics enabled/disabled, fatal bridge enabled/disabled | 3/3 configurations, 12/12 cases, no warnings | focused Stage 05 behavior and exclusion |
| `west twister -T tests/zephyr ... --warnings-as-errors -j 1` | all Stage 00-05 native suites | 13/13 configurations, 34/34 cases, no warnings | complete native regression gate |
| six `expect_failure.py` wrapped `west build` invocations for cases 6-11 | native 64 | 6/6 expected failures observed | static stack, slab, alignment, and pipe misuse diagnostics |
| `check_kernel_headers.py` using disabled-diagnostics `compile_commands.json` | real Zephyr generated configuration | 28/28 headers pass with `-Werror` | standalone headers and optional-off include safety |
| `cmake -S . -B build/host ...`, build, and `ctest` | host GCC 13, strict/relaxed/LTO | 47/47 pass | Stage 00-03 host and compile-fail regressions |
| GDB `sizeof` queries against final native debug ELF | native 64 | measured | bounded object/storage inventory recorded above |

The runtime suite specifically covers dormant preparation, explicit release,
native delayed launch, suspend/resume, finite join, explicit abort, stop-token
wakeup, condition-variable predicate wait, all work submission dispositions,
self-flush deadlock detection, schedule versus reschedule, cancellation,
owned queue drain/plug/unplug/stop, triggered poll lifetime, slab exhaustion,
pipe transfer, spin locking, direct diagnostics, global enumeration, and fatal
vocabulary.

## 10. Specification Refinements

None. The Zephyr 4.4 stack-threshold symbol mismatch is a pinned-baseline
implementation compatibility decision, not a change to the accepted API or
architecture.

### Local decision: controlled immediate thread launch

Problem: Zephyr can schedule an immediate thread before its optional name and
all application activation barriers are established.

Constraints: preserve native static ownership and guarantee dormant prepare.

Options considered: native immediate create; always create dormant and start;
separate private semaphore gate.

Decision: prepare and no-delay launch create with `K_FOREVER`; launch then calls
`k_thread_start()` after configuration. Native non-zero delayed creation remains
native.

Why: no extra semaphore or hidden behavior is needed, and naming/activation
ordering is deterministic.

Physical implementation: `include/solar/kernel/thread.hpp`.

Tests/evidence: prepare/release and delayed-launch runtime tests.

Reversal path: replace the private `create()` controlled-start branch without
changing the public API.

### Local decision: explicit containment at destruction

Problem: the trampoline writes `Exited` immediately before returning, while
Zephyr still has final thread-exit work to perform.

Constraints: destruction must neither block nor abort and stack storage must
remain valid through native termination.

Options considered: trust wrapper state; keep a separate contained bit; perform
a native no-wait join assertion.

Decision: destruction performs only a no-wait join assertion when a native
thread was created.

Why: it proves actual native termination without waiting or changing policy.

Physical implementation: `include/solar/kernel/thread.hpp`.

Tests/evidence: all joined, exited, and aborted owners satisfy destruction in
the runtime suite.

Reversal path: a future native containment token can replace the destructor
check internally.

### Local decision: triggered-work ownership claim

Problem: Zephyr ordinary work flags are idle while triggered work is registered
with the poller, so `k_work_is_pending()` alone is not a lifetime proof.

Constraints: no registry, lock, allocation, or unsafe event lifetime.

Options considered: inspect private poller state; trust native work flags;
track one atomic wrapper claim.

Decision: maintain an atomic claim from accepted submission through handler
completion or synchronized cancellation and conservatively reject overlapping
wrapper submissions.

Why: destruction and cancellation remain truthful without reading unstable
poller internals.

Physical implementation: `triggered_work.hpp`, `work.hpp`.

Tests/evidence: armed pending/state, duplicate rejection, armed flush rejection,
cancel, and empty-event tests.

Reversal path: remove the claim if Zephyr later exposes a stable complete
triggered-work ownership query.

### Local decision: opt-in fatal bridge

Problem: Zephyr permits one application strong fatal handler, so unconditional
Solar ownership would conflict with applications and Ztest.

Constraints: panic-safe, no ordinary locks, no logging recursion, no duplicate
handler.

Options considered: always override; weak chaining; explicit Kconfig link.

Decision: compile and link Solar's strong bridge only with
`CONFIG_SOLAR_FATAL_BRIDGE=y`.

Why: handler ownership is explicit and conflict-free.

Physical implementation: `fatal.hpp`, `fatal.cpp`, root `CMakeLists.txt`, and
`zephyr/Kconfig`.

Tests/evidence: disabled API test and enabled console link fixture.

Reversal path: applications can leave the option disabled and own Zephyr's
handler directly; a future frontend API can consume the same atomic latch.

## 11. Firmware And Host Impact

Firmware is intentionally unchanged in Stage 05. The first hard firmware
migration is the Stage 06 foundation integration gate. Host System/catalog
behavior remains unchanged and all strict, relaxed, multi-translation-unit,
LTO, and compile-fail tests remain green.

## 12. Known Limits And Deferred Work

- `StopToken` is non-owning; its source must outlive tokens and waiters.
- Triggered work intentionally rejects overlapping wrapper submissions; direct
  native use remains available when externally synchronized.
- Work and workqueue destructors assert idleness/stopped state and never flush,
  drain, abort, or wait implicitly.
- Runtime stack scans have Zephyr's normal timing and target restrictions.
- The optional broad Kernel families listed in Section 2 are not represented
  by partial wrappers. Task watchdog returns in Stage 19 with its provider
  boundary; other optional families are audited as concrete subsystem needs
  arise and at Stage 20 closure.

## 13. Documentation Handoff

The later public documentation pass should explain:

- dormant thread preparation and explicit release;
- stop-token lifetime and one-way semantics;
- exact work submission outcomes and schedule/reschedule differences;
- owned versus system workqueue authority;
- queue drain/plug requirement before owned stop;
- triggered-work event lifetime and conservative overlap behavior;
- explicit synchronous containment before destruction;
- diagnostic Kconfig costs and partial records;
- fatal bridge ownership and panic-safe observer restrictions;
- native escape-hatch responsibilities.

`tests/zephyr/kernel_execution/src/main.cpp` is the executable source for these
examples.

## 14. Closure Statement

Stage 05 is complete because Kernel can statically prepare threads and work,
hold them dormant through an activation barrier, release and contain them
explicitly, expose focused direct diagnostics, and provide the bounded native
facilities and fatal boundary required by lifecycle and execution. All
stage-owned, optional-off, compile-fail, standalone-header, host, and complete
native regression gates pass. Stage 06 lifecycle/System implementation is now
unblocked.
