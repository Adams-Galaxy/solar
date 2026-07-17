# Stage 04: Kernel Core Primitives

Status: landed

Landed date: 2026-07-16

Implementation repository/branch: `/workspaces/solar`, `static_reform`

Baseline: Stage 03 landed worktree based on `bed432f pre-solar-implementation`

## 1. Objective

Provide a compact, typed C++23 surface over Zephyr's core timing,
synchronization, queueing, polling, timer, interrupt, and scheduler primitives.
The surface must remain usable by an ordinary Zephyr application without a
Solar Blueprint, System, binding, lifecycle, executor, or hidden runtime owner.

## 2. Specification Coverage

| Specification | Coverage | Notes |
| --- | --- | --- |
| `00-design-conventions.md` | static ownership, no hidden allocation, typed errors, explicit context | complete for Stage 04 |
| `00a-modern-cpp-result-and-status.md` | `Result<T>`, focused `Status`, C++23 value returns | complete |
| `03-lifecycle-kernel-and-configuration.md` | kernel/lifecycle separation, core primitive surface, ISR spelling, native escape hatches, Kconfig capability ownership | complete for Stage 04 primitives |
| `09-tasks-and-executors.md` | kernel versus execution ownership boundary | preserved; work and executor interpretation remain later stages |
| `12-health-and-supervision.md` | direct Zephyr primitive foundation and partial availability | complete for Stage 04; broad thread diagnostics remain Stage 05 |
| `14-integrated-architecture.md` | Zephyr-native kernel layer with no System integration | complete for Stage 04 |
| Stage 04 of `04-implementation-roadmap.md` | declared implementation and verification scope | complete |

Owned threads, stop tokens, work items, workqueues, memory slabs, broad thread
records, and fatal-hook integration remain Stage 05. Lifecycle and execution
semantics remain Stages 06 and 07.

## 3. Public Surface Landed

The Zephyr-only aggregate is `#include <solar/kernel.hpp>`. Focused headers are
also self-contained and may be included individually.

### Time and scheduling

- `kernel::SteadyClock`, `TickDuration`, `TimePoint`, `now()`, and
  `now_ticks()` use Zephyr uptime ticks.
- `kernel::Timeout` preserves native `K_NO_WAIT`, `K_FOREVER`, and bounded tick
  timeout semantics.
- `kernel::Deadline` preserves Zephyr timepoint calculation and remaining-time
  behavior.
- `kernel::Priority` supplies compile-time checked and runtime checked
  cooperative/preemptive priorities.
- `kernel::this_thread` supplies current ID/priority, priority changes, sleep,
  yield, and bounded busy-wait operations.
- `kernel::SchedulerLock` and `kernel::can_yield()` expose focused scheduler
  behavior.

### Synchronization and communication

- `kernel::Mutex` is intentionally non-recursive even though Zephyr's native
  mutex supports recursion.
- `kernel::RecursiveMutex` exposes Zephyr recursion explicitly.
- `kernel::LockGuard`, `kernel::UniqueLock`, `lock_guard()`, and
  `unique_lock()` preserve fallible lock acquisition.
- `kernel::Semaphore` and `kernel::BinarySemaphore` expose ordinary,
  non-blocking, deadline, and explicit ISR-safe forms.
- `kernel::MessageQueue<T, Capacity>` owns a statically bounded queue for
  trivially-copyable messages and returns received values through
  `Result<T>`.
- `kernel::EventFlags` provides post/set/clear/test plus any/all, consuming,
  deadline, reset-before-wait, and explicit ISR forms when `CONFIG_EVENTS` is
  enabled.
- `kernel::PollSignal` and `kernel::PollSet<Capacity>` provide typed bounded
  polling for signals, semaphores, and message queues when `CONFIG_POLL` is
  enabled.

### Interrupts and timers

- `kernel::in_isr()` reports actual Zephyr interrupt context.
- `kernel::InterruptLock` is a scoped, non-copyable, non-movable IRQ lock.
- `kernel::Timer` exposes one-shot and periodic timers, stop, status,
  synchronization, remaining time, expiry time, and native callback context.
- `kernel::native_handle()` members and `Native*` aliases provide direct escape
  hatches without hiding the underlying Zephyr object.

Representative use:

```cpp
#include <solar/kernel.hpp>

using Samples = solar::kernel::MessageQueue<ImuSample, 8>;

solar::kernel::Mutex state_mutex;
Samples samples;

solar::Status publish(const ImuSample& sample)
{
    return samples.try_send(sample);
}

solar::Result<ImuSample> await_sample()
{
    return samples.receive(solar::kernel::Timeout::after(20ms));
}
```

## 4. Runtime Ownership

| Owner | Storage/resource | Capacity | Synchronization | Lifetime |
| --- | --- | --- | --- | --- |
| `Mutex` | one `k_mutex` plus one atomic owner ID | one owner | native mutex plus atomic owner check | object lifetime |
| `RecursiveMutex` | one `k_mutex` | native recursion depth | native mutex | object lifetime |
| `Semaphore` | one `k_sem` | constructor-selected fixed limit | native semaphore | object lifetime |
| `MessageQueue<T, N>` | one `k_msgq` and `sizeof(T) * N` inline bytes | exactly `N` | native message queue | object lifetime |
| `EventFlags` | one `k_event` | native 32-bit event word | native event object | object lifetime |
| `PollSignal` | one `k_poll_signal` | one current signal result | native poll signal | object lifetime |
| `PollSet<N>` | `N` inline `k_poll_event` objects plus current count | exactly `N` registrations | caller serialization required | object lifetime |
| `Timer` | one `k_timer` and two non-owning callback pointers | one expiry and one stop callback | native timer | object lifetime |
| scoped locks | native key or non-owning wrapper pointer | one held operation | corresponding native primitive | lexical scope |

There is no heap use, dynamic registration, hidden thread, stack, workqueue,
System slot, lifecycle state, or constructor-time global registration. All
owning wrappers are non-copyable and non-movable. `Timeout`, `Deadline`,
`Priority`, native aliases, and result records are values.

The native 64-bit simulation measurement was:

| Type | Wrapper | Native/base storage | Explicit difference |
| --- | ---: | ---: | --- |
| `RecursiveMutex` | 32 B | `k_mutex`: 32 B | none |
| `Mutex` | 40 B | `k_mutex`: 32 B | 8 B atomic owner ID |
| `Semaphore` | 40 B | `k_sem`: 40 B | none |
| `MessageQueue<uint32_t, 2>` | 112 B | `k_msgq`: 104 B | 8 B payload buffer |
| `EventFlags` | 32 B | `k_event`: 32 B | none |
| `PollSet<3>` | 128 B | three 40 B events | 8 B count |
| `Timer` | 104 B | `k_timer`: 88 B | two 8 B callbacks |

These sizes are target-dependent. Portable lower-bound and exact-storage
assertions remain in the native suite.

## 5. Compile-Time Behavior

- Queue messages must be trivially copyable.
- Queue and poll capacities must be non-zero and within native limits.
- Compile-time priority constructors reject levels outside configured Zephyr
  ranges; runtime priority constructors return `Status::Invalid`.
- `EventFlags` and polling headers remain includable when their Zephyr
  capability is disabled. Intentional construction then fails with a focused
  `SOLAR_DIAGNOSTIC_*` token.
- `event_flags_available` and `poll_available` expose direct compile-time
  availability facts.
- Every public Stage 04 header passes standalone compilation with both optional
  capabilities disabled.
- Owning primitive lifetime and storage properties are compile-time asserted.

Stage 04 introduces no binding, catalog, generated code, or System-dependent
compile-time behavior.

## 6. Error And Availability Behavior

Ordinary Zephyr return values are normalized into focused Solar statuses:

| Condition | Result |
| --- | --- |
| immediate lock/semaphore operation cannot proceed | `WouldBlock` |
| immediate queue send when full | `Full` |
| immediate queue receive/peek when empty | `Empty` |
| finite wait expires | `Timeout` |
| blocked message queue operation is cancelled by purge | `Cancelled` |
| non-recursive mutex is reacquired by its owner | `Deadlock` |
| mutex unlock by a non-owner | `PermissionDenied` |
| waiting mutex operation attempted in ISR context | `Invalid` |
| scheduler lock attempted in ISR | `Invalid` |
| scheduler/yield unavailable in current context | `NotReady` |
| invalid mask, duration, priority, index, or operation | focused `Invalid` or `NotFound` |

Poll interruption is not discarded as an error: Zephyr `-EINTR` may accompany
valid cancelled event state, so `PollResult` reports both the ready count and
`interrupted = true`.

Disabled optional capabilities remain compile-time unavailable. Solar does not
select `CONFIG_EVENTS` or `CONFIG_POLL` merely because their headers are
included.

## 7. Zephyr Integration

The implementation uses public Zephyr 4.4 APIs and their native storage:

- `k_uptime_ticks`, timeout macros, and `sys_timepoint_*`;
- `k_thread_*`, `k_sleep`, `k_yield`, and `k_busy_wait` for current-thread
  operations;
- `k_sched_lock`/`k_sched_unlock`;
- `k_mutex`, `k_sem`, `k_msgq`, `k_event`, `k_poll`, `k_poll_signal`, and
  `k_timer`;
- `k_is_in_isr`, `irq_lock`, and `irq_unlock`.

No Solar Kconfig option was added. Capability truth comes directly from
Zephyr's generated `CONFIG_EVENTS`, `CONFIG_POLL`, priority ranges, and clock
configuration.

Explicit `_isr` operations are non-waiting and are tested through
`irq_offload()`, not from an ordinary thread pretending to be an ISR. Timer
expiry callbacks run in the system clock interrupt context; timer stop
callbacks run in the context that calls stop. Poll-signal raise is not claimed
as an ISR operation because the public Zephyr contract used for this baseline
does not mark it as such.

Native handles are intentionally available for unsupported advanced use. The
caller then owns native Zephyr context and lifetime rules.

## 8. Files Changed

### Added

- `include/solar/kernel.hpp`
- `include/solar/kernel/error.hpp`
- `include/solar/kernel/message_queue.hpp`
- `tests/zephyr/kernel_core/`
- `tests/zephyr/kernel_availability/`
- `tests/zephyr/kernel_compile_fail/`

### Reshaped

- `include/solar/kernel/critical_section.hpp`
- `include/solar/kernel/deadline.hpp`
- `include/solar/kernel/event_flags.hpp`
- `include/solar/kernel/interrupt.hpp`
- `include/solar/kernel/kernel.hpp`
- `include/solar/kernel/mutex.hpp`
- `include/solar/kernel/native.hpp`
- `include/solar/kernel/poll.hpp`
- `include/solar/kernel/priority.hpp`
- `include/solar/kernel/scheduler.hpp`
- `include/solar/kernel/semaphore.hpp`
- `include/solar/kernel/this_thread.hpp`
- `include/solar/kernel/time.hpp`
- `include/solar/kernel/timer.hpp`

### Removed

- `include/solar/kernel/config.hpp`
- `include/solar/kernel/queue.hpp`
- stale pre-reform `thread.hpp`, `work.hpp`, `work_queue.hpp`, and
  `diagnostics.hpp`; their accepted replacements belong to Stage 05

## 9. Tests And Evidence

| Command | Platform/configuration | Result | What it proves |
| --- | --- | --- | --- |
| `west twister -T tests/zephyr/kernel_core -T tests/zephyr/kernel_availability --inline-logs --outdir build/twister-stage04-kernel --clobber-output --warnings-as-errors` | Zephyr 4.4, native 64, optional capabilities enabled and disabled | 2/2 configurations, 8/8 cases, no warnings | time, deadline, priority, scheduler, mutex contention/recursion, guards, semaphores, queues, events, poll, timers, and optional exclusion |
| five `expect_failure.py` wrapped `west build` invocations over `kernel_compile_fail` | native 64 | 5/5 expected failures observed | queue payload/capacity, priority range, and disabled capability diagnostics |
| standalone syntax loop over 17 public kernel headers using the disabled-capability compile command | native Zephyr generated configuration | 17/17 pass | self-contained headers and no include-order dependency |
| `west twister -T tests/zephyr --inline-logs --outdir build/twister-stage04-full-serial --clobber-output --warnings-as-errors -j 1` | all Stage 00-04 native suites | 10/10 configurations, 22/22 cases, no warnings | complete native regression gate |
| final `west twister -T tests/zephyr/kernel_core ... -j 1` after ownership assertions | native 64 | 1/1 configuration, 7/7 cases, no warnings | final PollSet ownership fix and permanent storage/lifetime assertions |
| `cmake -S . -B build/host -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release && cmake --build build/host && ctest --test-dir build/host --output-on-failure` | host GCC 13 | 47/47 pass | Stage 00-03 host and compile-fail regressions |
| temporary compile-only size symbols inspected with `nm -S --size-sort` | native 64 generated configuration | inspected | exact bounded wrapper and native object sizes recorded above |

The first parallel full-matrix attempt used Twister's default eight jobs and
four devicetree generators failed with host `EMFILE` (`Too many open files`).
The same matrix passed serially. This is a container file-descriptor limit, not
a code or test failure; serial Twister is the reliable closure command in this
workspace.

## 10. Specification Refinements

None. Implementation stayed within the accepted ownership, Zephyr-native, and
context-explicit contracts.

## 11. Firmware And Host Impact

No firmware migration is required at this roadmap stage. The kernel aggregate
is deliberately separate from host-safe `solar/solar.hpp` because Zephyr
kernel headers require generated configuration and platform context. Ordinary
Zephyr consumers can use `solar/kernel.hpp` without a System.

The host-only architecture tests remain independent of Zephyr, and all passed
unchanged.

## 12. Known Limits And Deferred Work

- Owned threads, stack storage, stop primitives, work, workqueues, memory
  slabs, diagnostics, and fatal hooks are Stage 05.
- Cancellation-aware service waits and execution records are Stages 05-07.
- `PollSet` covers signals, semaphores, and message queues in this core pass;
  additional stable public Zephyr poll object types may be added with their
  owning primitive stage.
- Advanced native operations remain available through explicit handles and are
  not implied to be Solar-safe.
- Target-specific firmware compilation is required at later integration gates,
  not Stage 04.

## 13. Documentation Handoff

The public documentation pass should explain:

- choosing `Timeout`, `Deadline`, or non-blocking operations;
- why `Mutex` and `RecursiveMutex` are distinct;
- fallible guard construction and `UniqueLock` ownership;
- queue trivial-copy and bounded-storage requirements;
- event and poll Kconfig availability;
- explicit ISR spellings and timer callback contexts;
- native handle ownership transfer and non-transfer rules;
- the fact that kernel primitives need neither Blueprint nor lifecycle.

`tests/zephyr/kernel_core/src/main.cpp` is the executable example source to
adapt.

## 14. Local Implementation Decisions

### Zephyr-native timeout and timepoint representation

Problem: flattening Zephyr timeouts into milliseconds would lose forever,
no-wait, tick rounding, and absolute-timepoint behavior.

Constraints: wrappers must preserve native semantics and remain cheap values.

Options considered: fixed millisecond integers; `std::chrono` values converted
at every call; opaque wrappers around native timeout/timepoint values.

Decision: `Timeout` owns `k_timeout_t`, `Deadline` owns `k_timepoint_t`, and
chrono durations are rounded upward to ticks only at construction.

Why: this preserves Zephyr's public semantics while giving callers typed C++23
duration input.

Physical implementation: `kernel/time.hpp`, `deadline.hpp`, and timeout-taking
primitive overloads.

Tests/evidence: no-wait, forever, sub-tick rounding, expiry, remaining-time,
and finite timeout tests pass.

Reversal path: conversion internals can change behind the same value types if a
future Zephyr baseline changes timeout representation.

### Strictly non-recursive Mutex

Problem: Zephyr `k_mutex` is recursive, but the accepted Solar surface requires
distinct ordinary and recursive mutex types.

Constraints: use public Zephyr storage, preserve priority inheritance, reject
self-acquisition deterministically, and avoid a registry.

Options considered: expose native recursion on both names; implement a mutex
from semaphore primitives; retain `k_mutex` and track the current owner.

Decision: `Mutex` stores one atomic owner ID beside `k_mutex`; `RecursiveMutex`
is the exact native wrapper.

Why: native locking and priority inheritance remain intact while ordinary
self-acquisition reports `Deadlock` instead of silently increasing recursion.

Physical implementation: `kernel/mutex.hpp`.

Tests/evidence: self-lock, non-owner unlock, contention timeout, recursive
depth, and measured 8-byte owner-state overhead pass.

Reversal path: replace only `Mutex` private storage if Zephyr later exposes a
stable non-recursive primitive or owner query.

### Value-returning typed message receive

Problem: a C-style out parameter would leak native queue ergonomics into every
caller and complicate `Result` composition.

Constraints: payloads are bounded, have no heap or constructor execution in
queue storage, and must be safe to copy as bytes.

Options considered: output references; optional values plus a separate error;
`Result<T>` reconstructed from copied representation bytes.

Decision: require trivially-copyable payloads, receive into local byte storage,
and return `Result<T>` via `std::bit_cast`.

Why: ordinary use is compact, composable, and does not create a live `T` inside
Zephyr's raw queue storage.

Physical implementation: `kernel/message_queue.hpp`.

Tests/evidence: ordered send/receive, front insertion, peek, full, empty,
timeout, purge, ISR transfer, and non-trivial compile failure pass.

Reversal path: a separately named object-lifetime-aware queue can be added
later without weakening `MessageQueue`'s byte-copy contract.

### Optional Zephyr capabilities remain optional

Problem: unconditional wrapper definitions either force-enable Zephyr features
or fail every translation unit that includes the aggregate when a feature is
off.

Constraints: Kconfig is canonical, unused wrappers must cost nothing, and
intentional use of disabled functionality needs a focused error.

Options considered: Solar selects all kernel features; omit all names when
disabled; expose availability facts and delayed diagnostics on construction.

Decision: headers expose `poll_available` and `event_flags_available`; disabled
types diagnose only when intentionally instantiated.

Why: aggregate headers remain ergonomic while Kconfig keeps authority and
link-time cost remains opt-in.

Physical implementation: `kernel/event_flags.hpp`, `poll.hpp`, and the disabled
test application.

Tests/evidence: aggregate and every focused header compile with both options
off; intentional uses emit their stable diagnostic tokens.

Reversal path: the disabled stubs can become constrained factory functions if
future diagnostics warrant it without changing enabled operation names.

### Preserve interrupted poll facts

Problem: Zephyr may return `-EINTR` while poll events contain meaningful
cancelled state.

Constraints: callers must not lose event facts, but interruption must remain
visible.

Options considered: return only `Status::Interrupted`; treat interruption as
ordinary success; return one result containing readiness and interruption.

Decision: successful and interrupted waits return `PollResult` with ready count
and an `interrupted` flag.

Why: this preserves all native information without forcing callers to inspect
raw event arrays.

Physical implementation: `kernel/poll.hpp`.

Tests/evidence: ready-state decoding and timeout behavior pass; cancellation
representation follows the inspected Zephyr 4.4 poll implementation.

Reversal path: `PollResult` can gain more focused interruption metadata without
changing the wait operation's result ownership.

### Timer callback boundary stays native and explicit

Problem: hiding timer callbacks behind deferred work would introduce an
executor and change Zephyr callback context.

Constraints: Stage 04 cannot own work or a thread, and context truth must remain
visible.

Options considered: always defer callbacks; expose only polling; store two
non-owning typed callback pointers and preserve native execution context.

Decision: `Timer` invokes typed callbacks directly from Zephyr's expiry and
stop shims.

Why: the wrapper is exact, bounded, and honest. Applications that need deferral
can submit Stage 05 work explicitly.

Physical implementation: `kernel/timer.hpp`.

Tests/evidence: expiry is observed in actual ISR context; stop callback is
observed in caller thread context; one-shot, periodic, stop, and sync pass.

Reversal path: add a separately named work-dispatch adapter after Stage 05;
do not change `Timer`'s direct callback contract.

### PollSet is a static owner

Problem: `k_poll_event` is a plain copyable struct, so the first `PollSet`
implementation accidentally inherited copy and move operations.

Constraints: a copied set would duplicate registration state and non-owning
object pointers while implying independent ownership.

Options considered: document copying as shallow; rebuild pointers on copy;
delete copy and move like the other owning kernel wrappers.

Decision: `PollSet` is explicitly non-copyable and non-movable.

Why: its address-stable bounded event array is one runtime owner, consistent
with every other Stage 04 primitive.

Physical implementation: `kernel/poll.hpp` and permanent type assertions in
`tests/zephyr/kernel_core/src/main.cpp`.

Tests/evidence: the ownership assertions initially caught the issue; the final
native suite passes after the fix.

Reversal path: a separate non-owning poll descriptor view can be introduced if
copyable poll configuration becomes valuable.

## 15. Closure Statement

Stage 04 is complete because the accepted kernel primitives work independently
of Solar System composition, own only bounded native storage, preserve optional
Kconfig capability truth, pass actual ISR and concurrency behavior, emit
focused misuse diagnostics, compile as standalone headers, and leave all
Stage 00-03 regressions green. Stage 05 thread, work, stop, diagnostic, and
fatal-boundary implementation is now unblocked.
