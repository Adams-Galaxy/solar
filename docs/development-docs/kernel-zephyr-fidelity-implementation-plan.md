# Solar Kernel And Zephyr Fidelity Implementation Plan

Date: 2026-08-03

Status: accepted direction; implementation not started

Companion problem statement:
[`kernel-zephyr-fidelity.md`](kernel-zephyr-fidelity.md)

Baseline and native coverage matrix:
[`kernel-zephyr-coverage-matrix.md`](kernel-zephyr-coverage-matrix.md)

## 1. Purpose And Authority

This document is the implementation script for finishing `solar::kernel` as a
modern C++ facade over the pinned Zephyr 4.4 kernel. It covers repairs to the
existing package, the useful missing kernel facilities selected for this
milestone, Application integration, migration, documentation, and verification.

When an implementation shortcut conflicts with this plan, stop and update the
design before continuing. Do not preserve a Solar behavior merely because it
already exists when that behavior contradicts Zephyr.

Source compatibility with the current developmental kernel API is not a release
gate. Existing Solar and robot call sites must be migrated in the same phase as
the corresponding public change. Wire protocols and generated application
contracts are unaffected.

## 2. Target And Non-Goals

The completed package will:

- preserve Zephyr's scheduling, context, object, and lifecycle semantics;
- add C++ ownership, borrowing, RAII, chrono, spans, typed values, compile-time
  validation, and explicit results;
- expose exact native concepts rather than replacing them with a portable
  approximation;
- remain allocation-free by default;
- work independently of Solar System, Remote, and generated applications; and
- identify every intentionally unwrapped Zephyr kernel area.

This milestone will not implement the advanced integrations listed in section
15. Their omission is explicit and documented rather than accidental.

## 3. Non-Negotiable Invariants

1. Zephyr is the source of truth for kernel state.
2. A Solar wrapper does not silently narrow or reinterpret a native state
   transition.
3. An owning wrapper owns address-stable native storage and is non-copyable and
   non-movable after initialization.
4. A borrowed reference never owns, initializes, reinitializes, or extends the
   lifetime of its native object.
5. Mutable native access is unavailable when it can violate Solar invariants.
6. A method documented as thread-only rejects ISR use before invoking Zephyr.
7. An ISR entry point cannot block.
8. Kconfig-dependent APIs either compile to their real implementation or
   produce a focused availability diagnostic. They do not silently degrade.
9. Solar retains native errno or native outcome information whenever it is
   meaningful.
10. No Solar code reads or writes private Zephyr object layout.
11. Assertions enforce programmer lifetime invariants, not ordinary fallible
    public configuration.
12. Exact Zephyr functionality remains reachable through the canonical wrapper
    or an explicitly documented focused native escape hatch.

## 4. Locked Public Direction

### 4.1 Exact priority values

`kernel::Priority` remains the runtime value passed to threads and workqueues.
It has no public default constructor and provides exact factories:

```cpp
auto control = kernel::Priority::preemptive<2>();
auto worker = kernel::Priority::cooperative<0>();
auto exact = kernel::Priority::native<-3>();

auto dynamic = kernel::Priority::try_preemptive(level);
auto native = kernel::Priority::from_native(value);
```

- `preemptive<N>()` validates `N < CONFIG_NUM_PREEMPT_PRIORITIES`.
- `cooperative<N>()` validates `N < CONFIG_NUM_COOP_PRIORITIES` and maps through
  `K_PRIO_COOP(N)`.
- `native<N>()` validates the complete signed application-thread range.
- Runtime factories return `Result<Priority>`.
- Queries expose native value, cooperative/preemptive category, and, when
  configured, whether the value occupies the Meta-IRQ subset.
- The semantic `PriorityLevel` type and `Priority::semantic()` are removed.

Meta-IRQ construction may be offered as the advanced exact factory
`Priority::meta_irq<N>()`. It must be gated by
`CONFIG_NUM_METAIRQ_PRIORITIES`, clearly document its synchronous
interrupt-like behavior, and remain unavailable as a default Application
service policy.

### 4.2 Application scheduling policies

The high-level Application API provides:

```cpp
solar::PreemptivePriority<2>
solar::CooperativePriority<0>
solar::Priority<-3> // explicitly raw native value
```

All three normalize to one `kernel::Priority`. `Run<Service, ...>` accepts at
most one of them. Diagnostics distinguish an invalid native value from an
invalid cooperative or preemptive level.

Unqualified services retain a Kconfig default using an explicit class and
level:

- `SOLAR_APPLICATION_DEFAULT_SERVICE_PRIORITY_PREEMPTIVE` or
  `SOLAR_APPLICATION_DEFAULT_SERVICE_PRIORITY_COOPERATIVE` through a Kconfig
  choice; and
- `SOLAR_APPLICATION_DEFAULT_SERVICE_PRIORITY_LEVEL`.

The default is preemptive level 2. Compilation validates it against the resolved
Zephyr priority counts. A target with no such level receives a focused error
rather than a fallback or clamp.

### 4.3 Ownership and borrowing vocabulary

- `Foo` owns its native object and storage.
- `FooRef` is a non-owning reference when a complete native-backed operation
  set can be offered without Solar shadow state.
- `Foo::ref()` produces the corresponding borrowed reference.
- Focused capabilities such as `WorkQueueTarget` are used when exposing a full
  mutable native object would violate lifecycle invariants.
- `native_handle()` lives on the exact reference or transparent owner only.
- `unsafe_native_handle()` will not be added as a general escape hatch.

The implementation must publish an interoperation matrix listing each owner,
its reference/capability type, whether mutable native access exists, and which
operations are valid through it.

`Mutex` remains deliberately non-recursive to satisfy the ordinary C++ mutex
contract while retaining Zephyr priority inheritance. Its owner tracking is a
Solar invariant, so it exposes no mutable native mutex. `RecursiveMutex` is the
exact Zephyr recursive behavior and may provide `RecursiveMutexRef`.

### 4.4 Context vocabulary

- Operations that Zephyr permits unconditionally from ISR use one ordinary
  method and are documented `ISR-safe`.
- Wait-capable methods are thread-only, even when passed `Timeout::no_wait()`.
- Explicit `try_*_isr()` methods expose native operations that are legal from
  ISR only with `K_NO_WAIT`.
- Thread-only methods return `Status::Invalid` from ISR before calling Zephyr.
- Methods such as poll, join, synchronous work cancellation, condition waits,
  and mutex operations remain thread-only.

## 5. Delivery Dashboard

| Phase | Scope | State |
| --- | --- | --- |
| 0 | Baseline and native coverage matrix | complete |
| 1 | Priority and scheduling values | complete |
| 2 | Ownership, references, and native interoperation | complete |
| 3 | Context and error contracts | complete |
| 4 | Threads and scheduler controls | complete |
| 5 | Synchronization, communication, time, and memory repairs | not started |
| 6 | Work, workqueues, triggered work, and poll | not started |
| 7 | Diagnostics, fatal handling, and stop tokens | not started |
| 8 | Intrusive queues, FIFO, LIFO, and kernel stack | not started |
| 9 | Heap, PMR, and storage statistics | not started |
| 10 | Mailbox | not started |
| 11 | Application and robot migration | not started |
| 12 | Configuration matrix, documentation, and hardening | not started |

Only one semantic foundation phase may be active at a time. Tests and
documentation for the active phase land with its implementation.

## 6. Phase 0: Baseline And Native Coverage Matrix

### Work

- Record the current Solar kernel public API, sizes, configured features,
  compile time, and test results.
- Record the current robot and simulator thread/workqueue priorities.
- Build a checked-in coverage matrix of Zephyr 4.4 kernel groups and functions.
- Classify each Zephyr facility as:
  - canonical Solar wrapper;
  - focused native reference/capability;
  - included in phases 1-10; or
  - deferred advanced integration.
- Record the existing wrappers that maintain shadow state and every public
  native handle that can mutate them.
- Add compile fixtures for cooperative-only, preemptive-only, reduced priority
  counts, optional poll/events, and ordinary ARM builds before changing APIs.

### Evidence

- Baseline document and API/interop matrix.
- Existing host and Twister suites pass unchanged.
- Optimized Teensy and native simulator builds pass.

### Exit gate

Every current wrapper and every public Zephyr kernel family is represented in
the matrix. No missing family can be discovered later without changing this
plan.

## 7. Phase 1: Priority And Scheduling Values

### Work

- Remove `PriorityLevel`, its root alias, and `Priority::semantic()`.
- Remove default construction of `kernel::Priority`.
- Implement exact compile-time and runtime native, cooperative, preemptive, and
  optional Meta-IRQ construction.
- Add explicit comparisons or rank helpers whose names make Zephyr's
  lower-number-is-higher ordering clear. Do not assign conventional numeric
  ordering a different meaning.
- Add `PreemptivePriority<N>`, `CooperativePriority<N>`, and raw `Priority<N>`
  Application policies.
- Replace the hard-coded Kconfig range with priority class plus level and
  compile-time validation against resolved Zephyr symbols.
- Require explicit priorities for directly created threads and workqueues.
- Update `ServiceRunner` to consume a normalized `kernel::Priority`, not an
  integer interpreted as preemptive.
- Migrate all examples, tests, Solar services, simulator code, and robot
  firmware.

### Evidence

- Compile-time round-trip tests for every configured valid native priority.
- Compile-fail tests for out-of-range native, cooperative, preemptive, and
  Meta-IRQ values.
- Runtime threads execute at both cooperative and preemptive priorities.
- Application expansion tests prove all three policies normalize exactly.
- Builds pass with zero cooperative priorities and with zero preemptive
  priorities where Zephyr supports those configurations.

### Exit gate

No semantic priority ladder or implicit preemptive interpretation remains.
Every application and kernel priority is exact and target-validated.

## 8. Phase 2: Ownership, References, And Native Interoperation

### Work

- Produce and approve the per-wrapper interoperation matrix before changing
  native access.
- Introduce borrowed references for transparent native objects, beginning with
  `ThreadRef`, `SemaphoreRef`, `RecursiveMutexRef`, `EventFlagsRef`,
  `MessageQueueRef<T>`, `TimerRef`, and the references required by poll.
- Introduce `WorkQueueTarget` for submission without exposing queue lifecycle.
- Make owner operations and reference operations share one implementation path.
- Remove mutable native handles from `Mutex`, owned `Thread`, `WorkQueue`, and
  `TriggeredWork` where they can invalidate Solar invariants.
- Replace raw internal cross-wrapper access with private friends or focused
  capability objects.
- Allow Solar wrappers to operate on Zephyr statically declared and
  Zephyr-owned objects through references.
- Correct the native-interoperation documentation to state exactly which
  mutations are supported.

### Evidence

- Tests operate on both Solar-owned objects and native Zephyr objects borrowed
  after `K_*_DEFINE` or native initialization.
- Compile-fail tests reject construction from null pointers and forbidden
  mutable access.
- Native operations permitted by the matrix leave owner behavior correct.
- Searches show no public mutable escape hatch on a shadow-state wrapper.

### Exit gate

Every exposed native mutation is proven compatible. Solar documentation no
longer promises unrestricted direct calls.

## 9. Phase 3: Context And Error Contracts

### Work

- Inventory every wrapped Zephyr operation's ISR contract.
- Apply the locked context vocabulary consistently.
- Add pre-call ISR rejection to every thread-only operation.
- Ensure every `_isr` path is statically or structurally fixed to no-wait.
- Preserve native errno in `Error::native` for every failed native operation.
- Replace generic errno mapping where an operation assigns different meanings
  to the same code.
- Add typed partial-success outcomes, including poll-signal delivery versus a
  latched timeout race.
- Document indistinguishable native outcomes, such as semaphore reset versus a
  native `-EAGAIN` timeout, without manufacturing precision.
- Audit constructors. Move dynamic fallible configuration to validated
  initialization/factories, and make fixed embedded configuration compile-time
  where that produces a cleaner type.
- Retain destructor assertions only for genuine ownership/lifetime violations.

### Evidence

- An ISR test table exercises every ISR-safe entry point and every rejected
  thread-only entry point.
- Error tests check both Solar status and preserved native code.
- Cancellation, reset, purge, timeout, close, and capacity races have focused
  tests.
- Release builds with assertions disabled still reject invalid public runtime
  configuration.

### Exit gate

No invalid ISR operation reaches a Zephyr assertion, and no documented native
outcome is accidentally discarded.

## 10. Phase 4: Threads And Scheduler Controls

### Work

- Reduce the owned `Thread` state to storage/lifecycle facts Solar can know
  reliably. Remove the claim that it represents complete execution state.
- Make `ThreadRef` the common native operation surface for owned, current, and
  borrowed threads.
- Add wakeup, wake deadline/remaining time, priority get/set, self-suspend,
  external suspend/resume, self/external abort, and join where Zephyr supports
  them.
- Add scheduler rescheduling and current-context preemptibility queries.
- Add global and per-thread time-slice configuration behind the appropriate
  Kconfig symbols.
- Add relative and absolute deadline scheduling behind
  `CONFIG_SCHED_DEADLINE`, using cycle-based types that preserve Zephyr units
  and ordering constraints.
- Replace raw thread option integers with typed flags and validate unsupported
  combinations.
- Explicitly exclude user-mode thread creation from this milestone.
- Preserve allocation-free static stack ownership and the raw function-pointer
  entry as the baseline. Optional callable adapters must add no hidden
  allocation.
- Ensure native operations cannot leave the owner destructor or joinability
  checks stale.

### Evidence

- Tests cover delayed start, start, wake, sleep, suspend, resume, join, abort,
  reuse, self operations, and destruction invariants.
- Cooperative, preemptive, deadline, and time-sliced scheduling tests run only
  when their native configurations are enabled.
- Borrowed current and statically declared native threads work through
  `ThreadRef`.
- Compile-fail tests reject user-mode and invalid option combinations.

### Exit gate

Solar reports only state it can prove, and the exposed thread/scheduler calls
match Zephyr behavior.

## 11. Phase 5: Existing Primitive Repairs

### Work

- Apply owner/reference and context rules to mutexes, condition variables,
  semaphores, events, message queues, pipes, memory slabs, timers, spinlocks,
  interrupt locks, scheduler locks, timeout, and deadline.
- Keep the deliberate distinction between non-recursive `Mutex` and exact
  `RecursiveMutex` explicit in names and documentation.
- Add event masked-set support.
- Add message-queue attributes and native free-count queries.
- Add memory-slab current, maximum, and resettable usage statistics.
- Clarify slab-block lifetime and prevent an owned block from silently
  outliving its slab in supported code paths.
- Preserve `K_FOREVER` timer start as Zephyr's permitted no-op outcome rather
  than rejecting it.
- Audit chrono conversion overflow, absolute timeouts, no-clock builds, and
  sub-tick rounding.
- Correct documentation names and signatures, including `SpinLock`,
  `InterruptLock`, and byte-sized `MemorySlab` templates.

### Evidence

- Native equivalence tests compare return values and state transitions for each
  repaired operation.
- ISR and thread-context tests pass on native simulation and ARM emulation.
- Time tests cover saturation, absolute deadlines, no-wait, forever, and clock
  configuration variants.
- Public documentation examples compile.

### Exit gate

Every existing basic primitive has an exact native behavior table and passes
it.

## 12. Phase 6: Work, Workqueues, Triggered Work, And Poll

### Work

- Delete `WorkQueue::abort()`.
- Model queue start, drain, plug, unplug, and stop using only supported Zephyr
  transitions.
- Remove or contain `started_` so native and Solar lifecycle cannot diverge.
- Preserve work submission outcomes `AlreadyQueued`, `Queued`, and
  `RequeuedAfterCurrent` exactly.
- Split work error mapping by operation.
- Retain synchronous self-deadlock detection without pretending it is native
  state.
- Redesign triggered work with explicit initial arm and replacement semantics.
- Preserve event-array lifetime until execution or cancellation and prevent
  mutation through the Solar type while armed.
- Replace the synthetic single `PollState` enum with a bitmask-capable state
  representation.
- Add queue/FIFO and pipe poll sources once their owning/reference wrappers are
  available.
- Preserve the useful `-EINTR` result in which cancelled poll events remain
  inspectable.

### Evidence

- Work and delayable-work state-transition tests match native outcomes.
- A workqueue can start, drain, plug, unplug, stop, and restart without private
  state manipulation or assertions.
- Trigger replacement, timeout firing, cancellation races, cross-queue errors,
  and handler self-operations are covered.
- Poll tests cover every supported source and combined native state bits.
- Repository search confirms `WorkQueue::abort()` and the old claimed-only
  submission restriction are absent.

### Exit gate

No Solar work operation leaves a native object in a state Solar cannot safely
own, reuse, or destroy.

## 13. Phase 7: Diagnostics, Fatal Handling, And Stop Tokens

### Work

- Remove all direct reads and writes of private `k_thread` layout.
- Use Zephyr public stack-safety APIs where available.
- Isolate the Zephyr 4.4 public/implementation symbol mismatch in one
  version-gated compatibility translation unit if required; do not expose the
  workaround through public headers.
- Distinguish owned lifecycle diagnostics from observable native diagnostics.
- Preserve optional results for features disabled by Kconfig.
- Exercise the fatal bridge through an actual controlled fatal test platform,
  including observer context restrictions and latched reason/status.
- Retain `StopSource` as a Solar companion abstraction, not a claimed Zephyr
  wrapper.
- Make stop generations one-shot: resetting a source creates a new generation,
  while tokens from the previous generation remain stopped.
- Document and test the borrowed lifetime requirement between source and token.

### Evidence

- Repository search finds no access to private Zephyr thread fields.
- Diagnostics build and run with every relevant Kconfig combination.
- The compatibility shim is version-guarded and tested against the pinned
  Zephyr tree.
- A fatal-path test observes and validates a real fatal reason before halt.
- Old stop tokens never become unstopped after source reset.

### Exit gate

Diagnostics use supported boundaries, and companion cancellation semantics are
truthful and deterministic.

## 14. Useful New Kernel Facilities

### Phase 8: Intrusive queues, FIFO, LIFO, and stack

#### Work

- Add an explicit intrusive node/value wrapper satisfying Zephyr's reserved
  first-word and alignment rules without relying on undocumented C++ base-class
  layout.
- Implement typed non-owning `Queue`, `Fifo`, and `Lifo` wrappers with append,
  prepend where applicable, remove, unique append, get, peek, cancel-wait, and
  batch/list operations supported by the native primitive.
- Make node ownership and simultaneous-membership rules explicit.
- Add `kernel::Stack<T, Capacity>` over `k_stack`, constrained to values that
  round-trip through `stack_data_t` without loss.
- Apply the same timeout, deadline, ISR, result, and borrowed-reference rules as
  existing communication primitives.

#### Evidence

- FIFO/LIFO ordering, arbitrary queue insertion/removal, cancellation, polling,
  ISR producers, and node reuse are tested.
- Compile-fail tests reject unsafe node layout and oversized stack values.
- Tests prove no payload allocation or copying occurs in intrusive containers.

#### Exit gate

Zephyr's zero-copy queue families and word stack have faithful typed wrappers.

### Phase 9: Heap, PMR, and storage statistics

#### Work

- Add fixed-storage `kernel::Heap<Bytes, Alignment>` over `k_heap` with aligned
  allocation, allocation, zeroed allocation, reallocation, free, timeout, and
  ISR rules matching Zephyr.
- Add a non-owning `HeapRef` for native heaps.
- Add an optional `kernel::HeapResource` adapter implementing
  `std::pmr::memory_resource` over a selected heap.
- Make PMR allocation failure behavior explicit for exception-disabled builds;
  do not hide a panic or null-return policy.
- Complete memory-slab and message-queue statistics begun in phase 5.

#### Evidence

- Alignment, exhaustion, timeout, realloc preservation, zeroing, and statistics
  tests pass.
- PMR containers use only the selected Zephyr heap and perform no system-heap
  allocation.
- Exception-disabled exhaustion behavior has a dedicated test and documented
  contract.

#### Exit gate

Bounded dynamic allocation is available without weakening Solar's
allocation-free default.

### Phase 10: Mailbox

#### Work

- Add a typed mailbox message descriptor preserving Zephyr's sender/receiver
  targeting, metadata, payload size, and deferred data-copy semantics.
- Implement synchronous send, receive, and data retrieval with timeout and
  deadline overloads.
- Implement asynchronous send only when the wrapper can express the required
  semaphore completion and message/payload lifetime safely.
- Provide a borrowed mailbox reference for Zephyr-owned mailboxes.
- Do not turn a mailbox into a copied `MessageQueue`; preserve its rendezvous
  and matching model.

#### Evidence

- Tests cover targeted and wildcard peers, metadata-only messages, immediate
  and deferred payload retrieval, truncation/size handling, timeouts, and
  asynchronous completion lifetime.
- Native comparison tests demonstrate the same matching and rendezvous
  behavior.

#### Exit gate

Mailbox is a genuine typed Zephyr mailbox wrapper, not a second queue API.

## 15. Deferred Advanced Integration

The following areas are explicitly deferred after this plan:

- user-mode thread creation, kernel-object permission grants, resource pools,
  and memory domains;
- dynamically allocated thread stacks;
- SMP CPU masks, CPU pinning, per-CPU enumeration, and IPI work;
- futexes;
- floating-point context enable/disable;
- CPU idle and atomic idle operations; and
- full kernel object-core and global runtime-statistics management beyond the
  diagnostics selected above.

The coverage matrix will link each deferred area to its native Zephyr API and
state that direct Zephyr use is the supported path for this release. No stub
wrapper or misleading availability constant will be added.

## 16. Phase 11: Application And Robot Migration

### Work

- Migrate Application service policies to exact preemptive/cooperative types.
- Select and document the Cockpit service priority intentionally.
- Update service runners, custom workqueues, direct threads, tests, examples,
  and robot firmware in the same commits as their API changes.
- Ensure generated Application explanations show the resolved native priority
  category, level, and integer value.
- Keep simulator and Teensy scheduling policy equivalent where their Zephyr
  configurations match.
- Remove compatibility aliases and migration-only adapters once all in-tree
  consumers use the final API.

### Evidence

- Application expansion static assertions prove the normalized priority and
  runner types.
- Native simulator behavior, Remote traffic, logging, streams, and Cockpit
  deadman behavior remain intact.
- Optimized/LTO Teensy firmware builds and boots with unchanged wire identity.
- Flash, RAM, stacks, boot time, and scheduling-sensitive behavior are compared
  with phase 0.

### Exit gate

No in-tree application uses semantic priority levels, raw Zephyr priority
macros where a Solar type exists, or removed kernel behavior.

## 17. Phase 12: Configuration Matrix And Hardening

### Required matrix

- `native_sim/native/64` full runtime suite;
- an ARM QEMU target for ISR and architecture-sensitive behavior;
- an SMP-capable target for spinlock and native-reference safety, without
  implementing the deferred affinity API;
- cooperative-only and preemptive-only configurations;
- small and non-default priority counts;
- poll/events enabled and disabled;
- deadline scheduling and time slicing enabled and disabled;
- diagnostics feature combinations;
- assertions enabled and disabled;
- system clock and supported no-clock compilation;
- multithreading and supported no-multithreading compilation;
- optimized Teensy build; and
- sanitizer-enabled host tests for companion state and typed containers.

### Work

- Expand Twister from native-only allowlists to the selected architecture
  matrix.
- Add compile-fail coverage for every invalid template argument, unavailable
  feature, context misuse expressible at compile time, and ownership violation.
- Add native-equivalence tests that execute the Solar and direct Zephyr forms
  against identical fixtures.
- Update the kernel subsystem guide, API reference, ISR guide, examples, and
  architecture boundary document.
- Generate or maintain the Zephyr coverage/interoperation matrix as reviewed
  documentation.
- Run formatting, documentation builds, static analysis, sanitizer suites,
  Twister, native simulation, and optimized firmware builds.

### Final gates

- All eleven fidelity completion gates in the companion audit pass.
- No private Zephyr structure access remains.
- No semantic priority ladder remains.
- No unsafe workqueue abort remains.
- Every public native handle is listed in the interoperation matrix.
- Existing and new kernel primitives have context and native-equivalence tests.
- Every deferred advanced area is documented with its supported native path.
- Public documentation examples compile and use exact final names.
- `git diff --check`, formatting, documentation, host, sanitizer, Twister,
  simulator, and optimized Teensy builds pass.

## 18. Commit And Evidence Discipline

Use small commits in this order:

1. baseline and test fixtures;
2. priorities;
3. ownership and native references;
4. context and errors;
5. threads and scheduling;
6. existing primitive repairs;
7. work and poll;
8. diagnostics and stop tokens;
9. queue families and stack;
10. heap and PMR;
11. mailbox;
12. Application/robot migration; and
13. documentation and final hardening.

Each verified phase appends an evidence record:

```text
Date:
Solar commit:
Project commit, if applicable:
Commands:
Platforms/configurations:
Artifacts or measurements:
Known limitations assigned to a later phase:
```

Do not mark a phase verified on compilation alone when its behavior depends on
thread scheduling, ISR context, cancellation, native object state, or object
lifetime.
